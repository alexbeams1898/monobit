#include "ui/BossHud.h"

#include "AppStateGlobal.h"
#include "WallClock.h"
#include "gameplay/Actor.h"
#include "gameplay/Enemies.h"
#include "gameplay/EnemyArchetype.h"
#include "lang/Language.h"

#include <imgui.h>

#include <algorithm>
#include <cstdio>
#include <string>

namespace selva::ui
{

namespace
{

// State machine for the HUD's fade-in / fade-out + felled overlay.
// All times are wall-clock seconds. Driven entirely by detecting
// the GameState.active_boss_idx transitions; no external triggers.
struct BossHudState
{
    enum class Mode
    {
        Hidden,    // no boss; HUD invisible
        FadingIn,  // boss just engaged; alpha 0 -> 1
        Active,    // boss engaged; HP bar + name shown at full alpha
        Felled,    // boss just died; felled-message text fade
        FadingOut, // post-felled cleanup; HP bar fades out
    };
    Mode mode = Mode::Hidden;
    double mode_entered_at = 0.0;
    // Boss display data captured at engage. Survives the actor
    // disappearing from the pool so the Felled overlay still has its
    // text.
    // The cached name + felled-message are resolved fresh each draw
    // via the lang-map keys below (cached_boss_name_key /
    // cached_felled_message_key). Literal fallback values cached at
    // engage for archetypes without language-map keys authored.
    std::string cached_boss_name;
    std::string cached_boss_name_key;
    std::string cached_felled_message;
    std::string cached_felled_message_key;
    bool cached_show_felled_overlay = true;
    float cached_hp_max = 1.0f;
    float cached_hp_at_death = 0.0f;
    // Spawn-decl id of the boss we entered Active state on. Used to
    // distinguish "boss died" from "boss disengaged" when
    // gameState.active_boss_idx clears: scan the actor pool by id; if
    // the actor still exists and is_dead, the boss died -> Felled
    // overlay. If the actor still exists and is alive, the boss
    // disengaged -> straight to Hidden (no Felled overlay). If the
    // actor doesn't exist, treat as death (defensive).
    std::string cached_boss_id;
};

BossHudState& state()
{
    static BossHudState s;
    return s;
}

// Resolve the boss display name via lang map, falling back to the
// literal name cached at engage. Called each draw so tier promotions
// (e.g. knows_lupa firing on death) flip the HP-bar label live.
std::string resolveBossName(const BossHudState& s)
{
    if (!s.cached_boss_name_key.empty())
        return selva::lang::resolve(s.cached_boss_name_key);
    return s.cached_boss_name;
}

std::string resolveFelledMessage(const BossHudState& s)
{
    if (!s.cached_felled_message_key.empty())
        return selva::lang::resolve(s.cached_felled_message_key);
    return s.cached_felled_message;
}

// Animation tunings. Kept here (file-local) until the GUI ships in
// the smoke test -- expose as config if iteration shows the defaults
// are wrong.
constexpr float kFadeInSeconds = 0.5f;
constexpr float kFelledFreezeSeconds = 0.5f; // brief input-noticeable pause
constexpr float kFelledTextHoldSeconds = 2.0f;
constexpr float kFadeOutSeconds = 1.0f;

// Compute fade alpha given elapsed time + duration. Clamped 0..1.
float fadeAlpha(double now, double started_at, float duration_seconds)
{
    if (duration_seconds <= 0.0f)
        return 1.0f;
    const double t = (now - started_at) / static_cast<double>(duration_seconds);
    return static_cast<float>(std::clamp(t, 0.0, 1.0));
}

// Boss whose HUD should be visible: Engaged (active fight) OR Dying
// (scripted-death sequence, HP bar drains during pain clip).
const selva::gameplay::Actor* resolveHudBoss()
{
    const auto& pool = selva::gameplay::actors();
    for (const auto& a : pool)
    {
        if (a.boss_state == selva::gameplay::BossState::Engaged ||
            a.boss_state == selva::gameplay::BossState::Dying)
            return &a;
    }
    return nullptr;
}

// Find an actor by spawn_decl_id (stable across pool shifts). Used to
// look up the cached boss after it leaves Engaged, so we can read its
// CURRENT boss_state and decide death vs disengage.
const selva::gameplay::Actor* findActorBySpawnDeclId(const std::string& spawn_decl_id)
{
    if (spawn_decl_id.empty())
        return nullptr;
    const auto& pool = selva::gameplay::actors();
    for (const auto& a : pool)
    {
        if (a.spawn_decl_id == spawn_decl_id)
            return &a;
    }
    return nullptr;
}

// Draw the HP bar + name. Position: bottom-center of screen, ~10%
// from bottom. Width ~40% of viewport.
void drawHpBarAndName(const std::string& name, float hp_norm, float alpha)
{
    if (alpha <= 0.0f)
        return;
    const ImGuiViewport* viewport = ImGui::GetMainViewport();
    const float vw = viewport->Size.x;
    const float vh = viewport->Size.y;
    const float bar_w = vw * 0.40f;
    const float bar_h = 12.0f;
    const float bar_x = (vw - bar_w) * 0.5f;
    const float bar_y = vh * 0.85f;

    ImDrawList* dl = ImGui::GetForegroundDrawList();

    // Boss name -- centered above the bar.
    const ImVec2 text_size = ImGui::CalcTextSize(name.c_str());
    const ImVec2 text_pos((vw - text_size.x) * 0.5f, bar_y - text_size.y - 8.0f);
    const ImU32 text_col = ImGui::GetColorU32(ImVec4(1.0f, 1.0f, 1.0f, alpha));
    // Drop shadow for readability over any background.
    dl->AddText(ImVec2(text_pos.x + 1.0f, text_pos.y + 1.0f),
                ImGui::GetColorU32(ImVec4(0.0f, 0.0f, 0.0f, alpha * 0.8f)), name.c_str());
    dl->AddText(text_pos, text_col, name.c_str());

    // HP bar background (dim).
    dl->AddRectFilled(ImVec2(bar_x, bar_y), ImVec2(bar_x + bar_w, bar_y + bar_h),
                      ImGui::GetColorU32(ImVec4(0.10f, 0.05f, 0.05f, alpha * 0.85f)));
    // HP fill.
    const float fill_w = bar_w * std::clamp(hp_norm, 0.0f, 1.0f);
    dl->AddRectFilled(ImVec2(bar_x, bar_y), ImVec2(bar_x + fill_w, bar_y + bar_h),
                      ImGui::GetColorU32(ImVec4(0.55f, 0.10f, 0.10f, alpha)));
    // Border.
    dl->AddRect(ImVec2(bar_x, bar_y), ImVec2(bar_x + bar_w, bar_y + bar_h),
                ImGui::GetColorU32(ImVec4(0.90f, 0.85f, 0.75f, alpha * 0.7f)));
}

// Draw the boss-felled overlay -- centered "X FELLED" text that
// fades over kFelledTextHoldSeconds. Tragic register: no music sting,
// no background overlay, no celebratory framing. Brief, mournful.
void drawFelledOverlay(const std::string& boss_name, const std::string& felled_message, float alpha)
{
    if (alpha <= 0.0f)
        return;
    const ImGuiViewport* viewport = ImGui::GetMainViewport();
    const float vw = viewport->Size.x;
    const float vh = viewport->Size.y;

    // Use felled_message if authored, else generic "<NAME> FELLED".
    const std::string display = felled_message.empty() ? (boss_name + " FELLED") : felled_message;

    ImDrawList* dl = ImGui::GetForegroundDrawList();
    const ImVec2 text_size = ImGui::CalcTextSize(display.c_str());
    const ImVec2 text_pos((vw - text_size.x) * 0.5f, vh * 0.45f);
    const ImU32 text_col = ImGui::GetColorU32(ImVec4(0.95f, 0.92f, 0.88f, alpha));
    // Drop shadow for readability.
    dl->AddText(ImVec2(text_pos.x + 2.0f, text_pos.y + 2.0f),
                ImGui::GetColorU32(ImVec4(0.0f, 0.0f, 0.0f, alpha * 0.85f)), display.c_str());
    dl->AddText(text_pos, text_col, display.c_str());
}

} // namespace

namespace
{
const char* modeName(BossHudState::Mode m)
{
    switch (m)
    {
    case BossHudState::Mode::Hidden:
        return "Hidden";
    case BossHudState::Mode::FadingIn:
        return "FadingIn";
    case BossHudState::Mode::Active:
        return "Active";
    case BossHudState::Mode::Felled:
        return "Felled";
    case BossHudState::Mode::FadingOut:
        return "FadingOut";
    }
    return "?";
}
} // namespace

void resetBossHud()
{
    auto& s = state();
    std::fprintf(stderr, "[boss-hud-reset] mode=%s cached_id='%s' -> Hidden, all cache cleared\n",
                 modeName(s.mode), s.cached_boss_id.c_str());
    std::fflush(stderr);
    s.mode = BossHudState::Mode::Hidden;
    s.mode_entered_at = 0.0;
    s.cached_boss_name.clear();
    s.cached_boss_name_key.clear();
    s.cached_felled_message.clear();
    s.cached_felled_message_key.clear();
    s.cached_show_felled_overlay = true;
    s.cached_hp_max = 1.0f;
    s.cached_hp_at_death = 0.0f;
    s.cached_boss_id.clear();
}

namespace
{
void bossHudPulseLog(const BossHudState& s, double now, const selva::gameplay::Actor* boss)
{
    static double s_last_pulse = 0.0;
    if (now - s_last_pulse < 1.0)
        return;
    const auto* cached = findActorBySpawnDeclId(s.cached_boss_id);
    std::fprintf(stderr,
                 "[boss-hud-pulse] mode=%s engaged_actor=%s cached_id='%s' "
                 "cached_actor_state=%s\n",
                 modeName(s.mode), boss != nullptr ? boss->spawn_decl_id.c_str() : "(none)",
                 s.cached_boss_id.c_str(),
                 cached ? selva::gameplay::bossStateName(cached->boss_state) : "(no actor)");
    std::fflush(stderr);
    s_last_pulse = now;
}

// Boss left visible state (Engaged or Dying). Felled cached actor ->
// Felled overlay; anything else (Disengaged / Dormant / missing) ->
// silent FadingOut.
void leftVisibleTransition(BossHudState& s, double now)
{
    const auto* cached = findActorBySpawnDeclId(s.cached_boss_id);
    const bool felled =
        (cached != nullptr) && cached->boss_state == selva::gameplay::BossState::Felled;
    const auto* cached_state_name =
        cached ? selva::gameplay::bossStateName(cached->boss_state) : "(no actor)";
    const auto prev_mode = s.mode;
    if (felled && s.cached_show_felled_overlay)
    {
        s.cached_hp_at_death = 0.0f;
        s.mode = BossHudState::Mode::Felled;
    }
    else
    {
        s.mode = BossHudState::Mode::FadingOut;
    }
    s.mode_entered_at = now;
    std::fprintf(stderr, "[boss-hud] leftVisible: prev=%s cached_id='%s' cached_state=%s -> %s\n",
                 modeName(prev_mode), s.cached_boss_id.c_str(), cached_state_name,
                 modeName(s.mode));
    std::fflush(stderr);
}

void cacheBossForFadeIn(BossHudState& s, double now, const selva::gameplay::Actor& boss)
{
    s.mode = BossHudState::Mode::FadingIn;
    s.mode_entered_at = now;
    s.cached_boss_name = (boss.archetype != nullptr) ? boss.archetype->boss_name : std::string{};
    s.cached_boss_name_key =
        (boss.archetype != nullptr) ? boss.archetype->boss_name_key : std::string{};
    s.cached_felled_message =
        (boss.archetype != nullptr) ? boss.archetype->felled_message : std::string{};
    s.cached_felled_message_key =
        (boss.archetype != nullptr) ? boss.archetype->felled_message_key : std::string{};
    s.cached_show_felled_overlay =
        (boss.archetype != nullptr) ? boss.archetype->show_felled_overlay : true;
    s.cached_hp_max = static_cast<float>(boss.hp.max);
    s.cached_boss_id = boss.spawn_decl_id;
    std::fprintf(stderr, "[boss-hud] Hidden -> FadingIn (engaged='%s' hp.max=%.0f)\n",
                 s.cached_boss_id.c_str(), s.cached_hp_max);
    std::fflush(stderr);
}

void advanceBossHudState(BossHudState& s, double now, const selva::gameplay::Actor* boss)
{
    const bool boss_visible = (boss != nullptr);
    switch (s.mode)
    {
    case BossHudState::Mode::Hidden:
        if (boss_visible)
            cacheBossForFadeIn(s, now, *boss);
        return;
    case BossHudState::Mode::FadingIn:
        if (!boss_visible)
            leftVisibleTransition(s, now);
        else if (now - s.mode_entered_at >= kFadeInSeconds)
        {
            s.mode = BossHudState::Mode::Active;
            s.mode_entered_at = now;
            std::fprintf(stderr, "[boss-hud] FadingIn -> Active\n");
            std::fflush(stderr);
        }
        return;
    case BossHudState::Mode::Active:
        if (!boss_visible)
            leftVisibleTransition(s, now);
        return;
    case BossHudState::Mode::Felled:
        if (now - s.mode_entered_at >= kFelledFreezeSeconds + kFelledTextHoldSeconds)
        {
            s.mode = BossHudState::Mode::FadingOut;
            s.mode_entered_at = now;
            std::fprintf(stderr, "[boss-hud] Felled -> FadingOut\n");
            std::fflush(stderr);
        }
        return;
    case BossHudState::Mode::FadingOut:
        if (now - s.mode_entered_at >= kFadeOutSeconds)
        {
            s.mode = BossHudState::Mode::Hidden;
            s.cached_boss_name.clear();
            s.cached_boss_name_key.clear();
            s.cached_felled_message.clear();
            s.cached_felled_message_key.clear();
            s.cached_show_felled_overlay = true;
            s.cached_boss_id.clear();
            std::fprintf(stderr, "[boss-hud] FadingOut -> Hidden (cache cleared)\n");
            std::fflush(stderr);
        }
        return;
    }
}

void drawBossHudFelledMode(const BossHudState& s, double now)
{
    // Resolve once per call; both the HP-bar and the overlay use
    // the same tier-gated name. When the insight node for THIS boss
    // fires on death, this is the frame the name flips from "???"
    // to the real name in front of the player.
    const std::string name = resolveBossName(s);
    const std::string felled = resolveFelledMessage(s);
    const double elapsed = now - s.mode_entered_at;
    if (elapsed < kFelledFreezeSeconds)
    {
        drawHpBarAndName(name, 0.0f, 1.0f);
        return;
    }
    drawHpBarAndName(name, 0.0f, 1.0f);
    const double text_elapsed = elapsed - kFelledFreezeSeconds;
    const float text_alpha = static_cast<float>(std::clamp(text_elapsed / 0.4, 0.0, 1.0));
    drawFelledOverlay(name, felled, text_alpha);
}

void drawBossHudByMode(const BossHudState& s, double now, float hp_norm)
{
    const std::string name = resolveBossName(s);
    switch (s.mode)
    {
    case BossHudState::Mode::Hidden:
        return;
    case BossHudState::Mode::FadingIn:
        drawHpBarAndName(name, hp_norm, fadeAlpha(now, s.mode_entered_at, kFadeInSeconds));
        return;
    case BossHudState::Mode::Active:
        drawHpBarAndName(name, hp_norm, 1.0f);
        return;
    case BossHudState::Mode::Felled:
        drawBossHudFelledMode(s, now);
        return;
    case BossHudState::Mode::FadingOut:
        drawHpBarAndName(name, 0.0f, 1.0f - fadeAlpha(now, s.mode_entered_at, kFadeOutSeconds));
        return;
    }
}
} // namespace

void renderBossHud()
{
    auto& s = state();
    const double now = selva::wallClock();
    const auto* boss = resolveHudBoss();
    bossHudPulseLog(s, now, boss);
    advanceBossHudState(s, now, boss);
    const float hp_norm =
        (boss != nullptr && boss->hp.max > 0)
            ? static_cast<float>(boss->hp.current) / static_cast<float>(boss->hp.max)
            : 0.0f;
    drawBossHudByMode(s, now, hp_norm);
}

} // namespace selva::ui
