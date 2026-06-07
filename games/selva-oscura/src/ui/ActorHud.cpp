#include "ui/ActorHud.h"

#include "AppState.h"
#include "AppStateGlobal.h"
#include "Tunables.h"
#include "WallClock.h"
#include "anim/PoseSampler.h"
#include "anim/SkeletalAssets.h"
#include "anim/SkeletalMesh.h"
#include "anim/SkeletonJointMap.h"
#include "combat/ActorVolumes.h"
#include "combat/HitFeedback.h"
#include "combat/HitVolumes.h"
#include "debug/Flags.h"
#include "gameplay/Actor.h"
#include "gameplay/Enemies.h"
#include "gameplay/Perception.h"
#include "gameplay/PlayerState.h"
#include "gameplay/RomanNumeral.h"
#include "gameplay/SanguePulse.h"
#include "physics/PhysicsWorld.h"
#include "render/Camera.h"
#include "render/TerrainShader.h"
#include "world/Collision.h"
#include "world/CryptLayout.h"
#include "world/Region.h"
#include "world/StructureFootprints.h"
#include "world/Terrain.h"
#include "world/Territory.h"

#include <glm/geometric.hpp>
#include <glm/vec2.hpp>
#include <imgui.h>

#include <SDL.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <vector>

namespace selva::ui
{

// Shared layout constants for the player HUD panel (HP / STA / vessel
// counter). Single source of truth referenced by renderActorHud +
// sangueHudAnchor so the pulse target and the displayed counter
// always agree, even when the layout shifts.
namespace kActorHudLayout
{
constexpr float kMargin = 18.0f;
constexpr float kBarWidth = 260.0f;
constexpr float kHpHeight = 14.0f;
constexpr float kStaminaHeight = 10.0f;
constexpr float kBarGap = 4.0f;
constexpr float kVesselHeight = 16.0f;
constexpr float kPanelW = kBarWidth + 16.0f;
constexpr float kPanelH = kHpHeight + kStaminaHeight + kVesselHeight + (kBarGap * 2.0f) + 16.0f;
} // namespace kActorHudLayout

namespace
{
// In-world HP bar lifecycle:
//   - on damage, last_damage_time is set to now (resets the timer)
//   - bar stays fully visible for kEnemyHpBarHoldSeconds after the
//     last hit
//   - then fades to transparent over kEnemyHpBarFadeSeconds
// Total = hold + fade. Any new hit during fade snaps the bar back
// to full opacity and restarts the timer.
constexpr float kEnemyHpBarHoldSeconds = 3.5f;
constexpr float kEnemyHpBarFadeSeconds = 0.7f;
// On-death override: when the actor dies, the bar should flash empty
// and disappear almost immediately -- not linger 4s like the alive
// case (the kill is the resolution; the bar's "fading" is just decor).
// Brief hold + short fade reads as "kill registered, moving on."
constexpr float kEnemyHpBarDeathHoldSeconds = 0.1f;
constexpr float kEnemyHpBarDeathFadeSeconds = 0.2f;
// Height above the head joint (in world meters) for the HP bar anchor.
constexpr float kEnemyHpBarHeadOffset = 0.15f;
// Size of the in-world HP bar in pixels (camera-space billboard).
constexpr float kEnemyHpBarWidthPx = 90.0f;
constexpr float kEnemyHpBarHeightPx = 6.0f;
// Damage number font scale on top of the default ImGui font.
constexpr float kDamageNumberScale = 1.6f;

bool sShowHitVolumes = false;

// Project a world-space capsule (p0, p1 + radius) to a debug
// outline of two screen-space circles plus connecting lines. The
// circle's screen-space radius is approximated by projecting a
// point offset by (radius * camera_right) from the capsule center
// — close enough for a debug visualization that doesn't need to
// be pixel-perfect at oblique angles.
void drawCapsuleOutline(ImDrawList* draw, const glm::mat4& view_proj, const glm::vec3& p0,
                        const glm::vec3& p1, float world_radius, ImU32 color)
{
    glm::vec2 s0;
    glm::vec2 s1;
    const bool a_visible = selva::render::worldToScreen(view_proj, p0, s0);
    const bool b_visible = selva::render::worldToScreen(view_proj, p1, s1);
    if (!a_visible && !b_visible)
        return;

    // Use a midpoint + lateral offset to estimate screen-pixel radius.
    const glm::vec3 mid = 0.5f * (p0 + p1);
    glm::vec2 s_mid;
    if (!selva::render::worldToScreen(view_proj, mid, s_mid))
        return;
    // Get the view-up + view-right by inverting the view-proj column
    // basis is overkill — instead, sample two points offset along
    // world axes; pick whichever produces a non-degenerate screen
    // delta.
    glm::vec2 s_off_x;
    glm::vec2 s_off_z;
    const bool ox_ok =
        selva::render::worldToScreen(view_proj, mid + glm::vec3(world_radius, 0.0f, 0.0f), s_off_x);
    const bool oz_ok =
        selva::render::worldToScreen(view_proj, mid + glm::vec3(0.0f, 0.0f, world_radius), s_off_z);
    float px_radius = 0.0f;
    if (ox_ok)
        px_radius = std::max(px_radius, glm::length(s_off_x - s_mid));
    if (oz_ok)
        px_radius = std::max(px_radius, glm::length(s_off_z - s_mid));
    if (px_radius < 2.0f)
        px_radius = 2.0f;

    if (a_visible)
        draw->AddCircle(ImVec2(s0.x, s0.y), px_radius, color, 16, 1.5f);
    if (b_visible)
        draw->AddCircle(ImVec2(s1.x, s1.y), px_radius, color, 16, 1.5f);
    if (a_visible && b_visible)
    {
        // Perpendicular offset along the 2D segment normal so the
        // connecting lines sit at the capsule's edge.
        const glm::vec2 seg = s1 - s0;
        const float seg_len = glm::length(seg);
        if (seg_len > 1e-3f)
        {
            const glm::vec2 perp(-seg.y / seg_len, seg.x / seg_len);
            const glm::vec2 lo0 = s0 + perp * px_radius;
            const glm::vec2 lo1 = s1 + perp * px_radius;
            const glm::vec2 lo2 = s0 - perp * px_radius;
            const glm::vec2 lo3 = s1 - perp * px_radius;
            draw->AddLine(ImVec2(lo0.x, lo0.y), ImVec2(lo1.x, lo1.y), color, 1.5f);
            draw->AddLine(ImVec2(lo2.x, lo2.y), ImVec2(lo3.x, lo3.y), color, 1.5f);
        }
        else
        {
            draw->AddLine(ImVec2(s0.x, s0.y), ImVec2(s1.x, s1.y), color, 1.5f);
        }
    }
}
} // namespace

bool showHitVolumes()
{
    return sShowHitVolumes;
}

void setShowHitVolumes(bool enabled)
{
    sShowHitVolumes = enabled;
}

namespace
{

// Per-region law-domain OBBs drawn as wireframe boxes. Color by
// owning region so overlap is visually unambiguous (chapel's corridor
// nesting inside limbo's disc shows up as two colors at the seam).
// Each volume labeled with its debug_name at its center.
void drawTerritoryWireframes(ImDrawList* fg)
{
    const glm::mat4& vp = selva::render::lastViewProj();
    const int n = engine::world::territoryCount();
    for (int idx = 0; idx < n; ++idx)
    {
        const auto& t = engine::world::territoryAt(idx);
        std::uint32_t h = 2166136261u;
        for (char ch : t.owner_region_id)
            h = (h ^ static_cast<std::uint8_t>(ch)) * 16777619u;
        const ImU32 color = IM_COL32(80 + (h & 0xFF) / 2, 80 + ((h >> 8) & 0xFF) / 2,
                                     80 + ((h >> 16) & 0xFF) / 2, 220);
        const glm::vec3& c = t.center;
        const glm::vec3& hf = t.half_extents;
        const glm::vec3 local_corners[8] = {
            {-hf.x, -hf.y, -hf.z}, {hf.x, -hf.y, -hf.z}, {hf.x, -hf.y, hf.z}, {-hf.x, -hf.y, hf.z},
            {-hf.x, hf.y, -hf.z},  {hf.x, hf.y, -hf.z},  {hf.x, hf.y, hf.z},  {-hf.x, hf.y, hf.z},
        };
        glm::vec2 sp[8];
        bool ok[8];
        for (int i = 0; i < 8; ++i)
        {
            const glm::vec3 world_corner = c + (t.orientation * local_corners[i]);
            ok[i] = selva::render::worldToScreen(vp, world_corner, sp[i]);
        }
        const int edges[12][2] = {
            {0, 1}, {1, 2}, {2, 3}, {3, 0}, {4, 5}, {5, 6},
            {6, 7}, {7, 4}, {0, 4}, {1, 5}, {2, 6}, {3, 7},
        };
        for (auto& e : edges)
        {
            if (ok[e[0]] && ok[e[1]])
                fg->AddLine(ImVec2(sp[e[0]].x, sp[e[0]].y), ImVec2(sp[e[1]].x, sp[e[1]].y), color,
                            1.5f);
        }
        glm::vec2 lp;
        if (selva::render::worldToScreen(vp, c, lp))
        {
            const std::string label = t.owner_region_id + ":" + t.debug_name;
            fg->AddText(ImVec2(lp.x, lp.y - 14.0f), color, label.c_str());
        }
    }
}

struct AwarenessVisuals
{
    ImU32 color;
    const char* label;
};

AwarenessVisuals awarenessVisuals(selva::gameplay::Awareness a)
{
    switch (a)
    {
    case selva::gameplay::Awareness::Suspicious:
        return {IM_COL32(230, 220, 60, 220), "Suspicious"};
    case selva::gameplay::Awareness::Alerted:
        return {IM_COL32(245, 150, 40, 230), "Alerted"};
    case selva::gameplay::Awareness::Combat:
        return {IM_COL32(230, 50, 50, 240), "Combat"};
    case selva::gameplay::Awareness::Unaware:
        break;
    }
    return {IM_COL32(180, 180, 180, 180), "Unaware"};
}

// Draw one AI actor's vision cone + awareness label. Called per
// actor when the F1 debug overlay toggle is on. Cone is rendered
// on the ground plane (y=0.05) as three lines: left edge, right
// edge, closing arc. A faint white center line shows facing.
void drawActorPerceptionOverlay(ImDrawList* overlay, const glm::mat4& view_proj,
                                const selva::gameplay::Actor& e, float half_fov_rad, float range)
{
    const float yaw = e.yaw;
    const float fx = -std::sin(yaw);
    const float fz = -std::cos(yaw);
    const float lx = -std::sin(yaw + half_fov_rad);
    const float lz = -std::cos(yaw + half_fov_rad);
    const float rx = -std::sin(yaw - half_fov_rad);
    const float rz = -std::cos(yaw - half_fov_rad);
    const glm::vec3 origin_w(e.pos.x, 0.05f, e.pos.z);
    const glm::vec3 l_end(e.pos.x + lx * range, 0.05f, e.pos.z + lz * range);
    const glm::vec3 r_end(e.pos.x + rx * range, 0.05f, e.pos.z + rz * range);
    const glm::vec3 f_end(e.pos.x + fx * range, 0.05f, e.pos.z + fz * range);

    const AwarenessVisuals vis = awarenessVisuals(e.perception.awareness);

    glm::vec2 s_origin;
    glm::vec2 s_l_end;
    glm::vec2 s_r_end;
    glm::vec2 s_f_end;
    const bool ok_o = selva::render::worldToScreen(view_proj, origin_w, s_origin);
    const bool ok_l = selva::render::worldToScreen(view_proj, l_end, s_l_end);
    const bool ok_r = selva::render::worldToScreen(view_proj, r_end, s_r_end);
    const bool ok_f = selva::render::worldToScreen(view_proj, f_end, s_f_end);
    if (ok_o && ok_l)
        overlay->AddLine(ImVec2(s_origin.x, s_origin.y), ImVec2(s_l_end.x, s_l_end.y), vis.color,
                         1.5f);
    if (ok_o && ok_r)
        overlay->AddLine(ImVec2(s_origin.x, s_origin.y), ImVec2(s_r_end.x, s_r_end.y), vis.color,
                         1.5f);
    if (ok_l && ok_r)
        overlay->AddLine(ImVec2(s_l_end.x, s_l_end.y), ImVec2(s_r_end.x, s_r_end.y), vis.color,
                         1.0f);
    if (ok_o && ok_f)
        overlay->AddLine(ImVec2(s_origin.x, s_origin.y), ImVec2(s_f_end.x, s_f_end.y),
                         IM_COL32(255, 255, 255, 90), 1.0f);

    const glm::vec3 label_pos(e.pos.x, e.pos.y + 2.1f, e.pos.z);
    glm::vec2 s_label;
    if (selva::render::worldToScreen(view_proj, label_pos, s_label))
    {
        const ImVec2 ts = ImGui::CalcTextSize(vis.label);
        const ImVec2 tp(s_label.x - ts.x * 0.5f, s_label.y - ts.y);
        overlay->AddText(ImVec2(tp.x + 1.0f, tp.y + 1.0f), IM_COL32(0, 0, 0, 220), vis.label);
        overlay->AddText(tp, vis.color, vis.label);
    }
}

// Draw the in-world HP bar above each damaged enemy. Bar appears on
// hit, holds, then fades; any subsequent hit resets the timer.
void drawEnemyHpBars(ImDrawList* overlay, const glm::mat4& view_proj,
                     const std::vector<selva::gameplay::Actor*>& list, float now);

// Draw floating damage numbers spawned at hit-event time. Each drifts
// up and fades out over its lifetime.
void drawFloatingDamageNumbers(ImDrawList* overlay, const glm::mat4& view_proj);

// Draw in-flight sangue pulses (3-billboard trails from corpse to
// Vagrant). Forward decl mirrors drawFloatingDamageNumbers.
void drawSanguePulses(ImDrawList* overlay, const glm::mat4& view_proj);

// Draw the lock-on reticle on the player's current target. Phase A
// placeholder: 16×16 white square at chest-height. Phase B replaces
// with an iconographic sprite.
void drawLockOnReticle(ImDrawList* overlay, const glm::mat4& view_proj);

// Draw the second-death card overlay (full-screen, foreground).
// Appears when the player is dead and the post-clip hold has
// elapsed; lingers until respawn. Two registers of text: "NOT YET"
// (modern, large) and "THOU DOST NOT BELONG" (Early Modern, smaller).
// Reads death_time from the player Actor and tunables for timing.
void drawSecondDeathCard();

struct BarRect
{
    float x;
    float y;
    float w;
    float h;
};

struct BarColors
{
    ImU32 bg;
    ImU32 fg;
    ImU32 border;
};

void drawBar(ImDrawList* draw, const BarRect& r, float fill_fraction, const BarColors& cols,
             const char* label_text)
{
    const float fill_w = std::clamp(fill_fraction, 0.0f, 1.0f) * r.w;
    draw->AddRectFilled(ImVec2(r.x, r.y), ImVec2(r.x + r.w, r.y + r.h), cols.bg);
    if (fill_w > 0.0f)
        draw->AddRectFilled(ImVec2(r.x, r.y), ImVec2(r.x + fill_w, r.y + r.h), cols.fg);
    draw->AddRect(ImVec2(r.x, r.y), ImVec2(r.x + r.w, r.y + r.h), cols.border);
    if (label_text != nullptr && label_text[0] != '\0')
    {
        const ImU32 text_color = IM_COL32(230, 230, 230, 255);
        const ImVec2 ts = ImGui::CalcTextSize(label_text);
        draw->AddText(ImVec2(r.x + 6.0f, r.y + (r.h - ts.y) * 0.5f), text_color, label_text);
    }
}

void drawEnemyHpBars(ImDrawList* overlay, const glm::mat4& view_proj,
                     const std::vector<selva::gameplay::Actor*>& list, float now)
{
    for (const auto* ep : list)
    {
        const auto& e = *ep;
        // Dead actors use the on-death timer (death_time) with much
        // shorter hold + fade -- the kill IS the resolution, the
        // lingering bar is dead-actor-decor and shouldn't sit there
        // for 4s the way it does on a live actor taking damage.
        // Flow-spawned dead actors that haven't been "consumed" yet
        // have death_time == 0 (sentinel) per fireEnemyDeath; for
        // those, gate on is_dead alone and skip the bar entirely.
        const bool dead = e.is_dead;
        float age = 0.0f;
        float hold = kEnemyHpBarHoldSeconds;
        float fade = kEnemyHpBarFadeSeconds;
        if (dead)
        {
            if (e.death_time <= 0.0f)
                continue; // deferred-fade corpse: no HP bar at all
            age = now - e.death_time;
            hold = kEnemyHpBarDeathHoldSeconds;
            fade = kEnemyHpBarDeathFadeSeconds;
        }
        else
        {
            if (e.last_damage_time < 0.0f)
                continue;
            age = now - e.last_damage_time;
        }
        if (age > hold + fade)
            continue;
        const glm::vec3 anchor(e.pos.x, e.pos.y + 1.8f + kEnemyHpBarHeadOffset, e.pos.z);
        glm::vec2 sp;
        if (!selva::render::worldToScreen(view_proj, anchor, sp))
            continue;
        float alpha = 1.0f;
        if (age > hold)
            alpha = std::clamp(1.0f - (age - hold) / fade, 0.0f, 1.0f);
        const ImU32 bg = IM_COL32(20, 20, 20, static_cast<int>(220 * alpha));
        const ImU32 fg = IM_COL32(170, 30, 30, static_cast<int>(240 * alpha));
        const ImU32 bd = IM_COL32(0, 0, 0, static_cast<int>(220 * alpha));
        const float fraction =
            (e.hp.max > 0) ? static_cast<float>(e.hp.current) / static_cast<float>(e.hp.max) : 0.0f;
        const float bx = sp.x - kEnemyHpBarWidthPx * 0.5f;
        const float by = sp.y - kEnemyHpBarHeightPx * 0.5f;
        drawBar(overlay, BarRect{bx, by, kEnemyHpBarWidthPx, kEnemyHpBarHeightPx}, fraction,
                BarColors{bg, fg, bd}, nullptr);
    }
}

void drawLockOnReticle(ImDrawList* overlay, const glm::mat4& view_proj)
{
    const auto& p = selva::gameplay::player();
    const int idx = p.lock_target_idx;
    if (idx < 0)
        return;
    const auto& pool = selva::gameplay::actors();
    if (idx >= static_cast<int>(pool.size()))
        return;
    const auto& target = pool[idx];
    // Anchor to the lockon-point BONE. Point list is resolved per-
    // actor via actorLockOnPoints (archetype's authored list else
    // skeleton default). Player cycles between points with mouse
    // wheel; lock_point_idx names the active entry.
    const auto& points = selva::gameplay::actorLockOnPoints(target);
    if (points.empty())
        return;
    const int point_idx =
        (p.lock_point_idx >= 0 && p.lock_point_idx < static_cast<int>(points.size()))
            ? p.lock_point_idx
            : 0;
    const int joint_idx = target.sampler.findJoint(points[point_idx].joint.c_str());
    if (joint_idx < 0)
        return;
    const glm::mat4 model = selva::combat::buildActorModelMatrix(
        target.pos, target.yaw, selva::gameplay::actorFootOffsetY(target));
    const glm::vec4 world = model * glm::vec4(target.sampler.jointWorldPos(joint_idx), 1.0f);
    glm::vec2 sp;
    if (!selva::render::worldToScreen(view_proj, glm::vec3(world), sp))
        return;
    constexpr float kReticleHalfSize = 8.0f;
    const ImU32 white = IM_COL32(255, 255, 255, 230);
    overlay->AddRectFilled(ImVec2(sp.x - kReticleHalfSize, sp.y - kReticleHalfSize),
                           ImVec2(sp.x + kReticleHalfSize, sp.y + kReticleHalfSize), white);
}

// Mirror of PerFrameTick.cpp's kPlayerSecondDeath* constants. Both
// sites read the same timing — the lifecycle drives respawn off
// these numbers, the card UI drives visibility off the same.
constexpr float kPlayerSecondDeathClipHoldSeconds = 3.5f;
constexpr float kPlayerSecondDeathCardSeconds = 4.0f;

void drawSecondDeathCard()
{
    const auto& p = selva::gameplay::player();
    if (!p.is_dead || p.death_time < 0.0f)
        return;
    const float elapsed = selva::wallClock() - p.death_time;
    if (elapsed < kPlayerSecondDeathClipHoldSeconds)
        return;

    // Card alpha: fade-in over the first 0.4s after the hold elapses,
    // fade-out over the last 0.4s before respawn. The card occupies
    // the window [clip_hold, clip_hold + card_seconds].
    const float t_in = elapsed - kPlayerSecondDeathClipHoldSeconds;
    const float t_remaining = kPlayerSecondDeathCardSeconds - t_in;
    constexpr float kFadeSeconds = 0.4f;
    const float alpha_in = std::clamp(t_in / kFadeSeconds, 0.0f, 1.0f);
    const float alpha_out = std::clamp(t_remaining / kFadeSeconds, 0.0f, 1.0f);
    const float alpha = std::min(alpha_in, alpha_out);
    if (alpha <= 0.0f)
        return;

    ImDrawList* fg = ImGui::GetForegroundDrawList();
    const ImGuiViewport* vp = ImGui::GetMainViewport();
    const ImVec2 origin = vp->WorkPos;
    const ImVec2 size = vp->WorkSize;

    // Vignette: black wash over the screen at ~70% alpha. Sells the
    // "everything fades except the verdict" register.
    const int wash_a = static_cast<int>(180.0f * alpha);
    fg->AddRectFilled(origin, ImVec2(origin.x + size.x, origin.y + size.y),
                      IM_COL32(0, 0, 0, wash_a));

    const char* msg = "SECOND DEATH";

    // Default ImGui font size is 13px; scale up so the phrase
    // carries the screen as a single centered verdict.
    const float base_font = ImGui::GetFontSize();
    const float msg_size = base_font * 9.0f;

    ImFont* font = ImGui::GetFont();
    const ImVec2 msg_ts = font->CalcTextSizeA(msg_size, FLT_MAX, 0.0f, msg);

    const float center_x = origin.x + size.x * 0.5f;
    const float center_y = origin.y + size.y * 0.5f;
    const ImVec2 msg_pos(center_x - msg_ts.x * 0.5f, center_y - msg_ts.y * 0.5f);

    const int text_a = static_cast<int>(255.0f * alpha);
    const ImU32 white = IM_COL32(245, 240, 230, text_a);
    const ImU32 shadow = IM_COL32(0, 0, 0, text_a);

    for (int dx = -2; dx <= 2; ++dx)
        for (int dy = -2; dy <= 2; ++dy)
            if ((dx | dy) != 0)
                fg->AddText(
                    font, msg_size,
                    ImVec2(msg_pos.x + static_cast<float>(dx), msg_pos.y + static_cast<float>(dy)),
                    shadow, msg);
    fg->AddText(font, msg_size, msg_pos, white, msg);
}

// Per-tier visual scaling. Tier index aligns with kRomanTiers in
// RomanTiers.h: 0=I (denom 1), 1=V (5), 2=X (10), 3=L (50), 4=C (100),
// 5=D (500), 6=M (1000), then vinculum'd tiers 7+. Darker + slightly
// larger as denomination increases so the player reads magnitude at
// a glance. Color matches the aged-larva tint palette (deep sangue-red
// rather than bright blood).
struct SangueTierVisual
{
    int r;
    int g;
    int b;
    float radius_px;
};

constexpr SangueTierVisual kSangueTierVisuals[] = {
    {180, 60, 50, 3.0f}, //  0: I    -- 1, brightest, smallest
    {165, 50, 45, 3.4f}, //  1: V    -- 5
    {150, 40, 40, 3.8f}, //  2: X    -- 10
    {135, 35, 35, 4.2f}, //  3: L    -- 50
    {120, 30, 28, 4.7f}, //  4: C    -- 100
    {105, 25, 25, 5.2f}, //  5: D    -- 500
    {90, 22, 22, 5.8f},  //  6: M    -- 1000
    {78, 18, 18, 6.4f},  //  7: V_   -- 5000
    {65, 16, 16, 7.0f},  //  8: X_   -- 10000
    {55, 14, 14, 7.6f},  //  9: L_   -- 50000
    {48, 12, 12, 8.2f},  // 10: C_   -- 100000
    {42, 10, 10, 8.8f},  // 11: D_   -- 500000
    {36, 9, 9, 9.4f},    // 12: M_   -- 1000000
    {30, 8, 8, 10.0f},   // 13: V__  -- 5M
    {26, 7, 7, 10.6f},   // 14: X__  -- 10M
    {22, 6, 6, 11.2f},   // 15: L__  -- 50M
    {18, 5, 5, 11.8f},   // 16: C__  -- 100M
    {14, 4, 4, 12.4f},   // 17: D__  -- 500M
};
constexpr int kSangueTierVisualCount = sizeof(kSangueTierVisuals) / sizeof(kSangueTierVisuals[0]);

const SangueTierVisual& tierVisual(int tier_index)
{
    const int clamped = std::clamp(tier_index, 0, kSangueTierVisualCount - 1);
    return kSangueTierVisuals[clamped];
}

// Sangue pulses fly in 2D screen space from a corpse-projected point
// to the HUD vessel counter, curved through a control point offset
// toward screen-center so the path bulges then sucks into the HUD.
// Per [[project_crucible_censer_leveling_system]] auto-magnetization.
void drawSanguePulses(ImDrawList* overlay, const glm::mat4& /*view_proj*/)
{
    const auto& pulses = selva::gameplay::sanguePulses();
    if (pulses.empty())
        return;
    const ImGuiViewport* vp = ImGui::GetMainViewport();
    const float center_x = vp ? (vp->WorkPos.x + vp->WorkSize.x * 0.5f) : 0.0f;
    const float center_y = vp ? (vp->WorkPos.y + vp->WorkSize.y * 0.5f) : 0.0f;
    for (const auto& p : pulses)
    {
        if (p.elapsed < p.delay)
            continue; // still staging
        const float flight_t = std::clamp((p.elapsed - p.delay) / p.lifetime, 0.0f, 1.0f);
        // Speed curve: cubic ease-in, so the particle holds near the
        // corpse, then accelerates HARD into the HUD. Reads as suction.
        const float t_ease = flight_t * flight_t * flight_t;
        // Quadratic Bezier with control point biased toward screen center.
        // Bias scales down as the particle nears the target so the curve
        // collapses into a straight tight pull at the end.
        const float bezier_bias = 0.45f * (1.0f - t_ease);
        const glm::vec2 control = glm::mix((p.source_screen + p.target_screen) * 0.5f,
                                           glm::vec2(center_x, center_y), bezier_bias);
        const glm::vec2 a = glm::mix(p.source_screen, control, t_ease);
        const glm::vec2 b = glm::mix(control, p.target_screen, t_ease);
        const glm::vec2 pos = glm::mix(a, b, t_ease);
        const auto& vis = tierVisual(p.tier_index);
        // Lifecycle alpha: bright through 80% of flight, fades sharply
        // at the very end so the suck-in moment reads as "consumed"
        // rather than "vanished."
        const float fade_t = std::clamp((flight_t - 0.85f) / 0.15f, 0.0f, 1.0f);
        const float lifecycle_alpha = 1.0f - fade_t;
        const int alpha_byte = std::clamp(static_cast<int>(255.0f * lifecycle_alpha), 0, 255);
        const ImU32 core = IM_COL32(vis.r, vis.g, vis.b, alpha_byte);
        const ImU32 glow = IM_COL32(vis.r / 2, vis.g / 2, vis.b / 2, alpha_byte / 2);
        overlay->AddCircleFilled(ImVec2(pos.x, pos.y), vis.radius_px * 1.7f, glow, 16);
        overlay->AddCircleFilled(ImVec2(pos.x, pos.y), vis.radius_px, core, 16);
    }
}

void drawFloatingDamageNumbers(ImDrawList* overlay, const glm::mat4& view_proj)
{
    const auto& numbers = selva::combat::damageNumbers();
    for (const auto& d : numbers)
    {
        glm::vec2 sp;
        if (!selva::render::worldToScreen(view_proj, d.world_pos, sp))
            continue;
        const float t = std::clamp(d.elapsed / d.lifetime, 0.0f, 1.0f);
        const float alpha = 1.0f - t * t; // ease-out fade
        const int a = std::clamp(static_cast<int>(255.0f * alpha), 0, 255);
        const ImU32 text_color = d.crit ? IM_COL32(255, 220, 80, a) : IM_COL32(245, 245, 245, a);
        const ImU32 shadow_color = IM_COL32(0, 0, 0, a);
        char buf[16];
        std::snprintf(buf, sizeof(buf), "%d", d.amount);
        const ImVec2 ts = ImGui::CalcTextSize(buf);
        const float tx = sp.x - ts.x * 0.5f * kDamageNumberScale;
        const float ty = sp.y - ts.y * 0.5f * kDamageNumberScale;
        const float fs = ImGui::GetFontSize() * kDamageNumberScale;
        for (int dx = -1; dx <= 1; ++dx)
            for (int dy = -1; dy <= 1; ++dy)
                if ((dx | dy) != 0)
                    overlay->AddText(
                        ImGui::GetFont(), fs,
                        ImVec2(tx + static_cast<float>(dx), ty + static_cast<float>(dy)),
                        shadow_color, buf);
        overlay->AddText(ImGui::GetFont(), fs, ImVec2(tx, ty), text_color, buf);
    }
}

// Draw the vessel counter in Roman numerals with manual vinculum
// overbars. Per [[project_substance_has_no_in_game_name]] the
// substance has no label; the count alone speaks. The encoder
// returns a glyph string + parallel bar count (0 plain / 1 single /
// 2 double). We iterate per-glyph so 1 or 2 horizontal lines can be
// drawn above each barred character. The cosmological cap
// (999,999,999) renders cleanly with three groups (double, single,
// plain) so no overflow indicator is needed -- the renderer can
// express every value the vessel can hold.
void drawVesselCounter(ImDrawList* draw, const ImVec2& origin)
{
    using namespace kActorHudLayout;
    std::uint32_t vessel = 0u;
    PlayerClass cls = PlayerClass::None;
    if (const PlayerProfile* profile = activePlayerProfile())
    {
        vessel = profile->sangue_vessel;
        cls = profile->player_class;
    }
    const auto rendering = selva::gameplay::encodeRoman(vessel);
    if (rendering.glyphs.empty())
        return; // empty vessel: render nothing
    const float text_y = origin.y + kHpHeight + kStaminaHeight + (kBarGap * 2.0f);
    // Per-class color cue. Subtle: same warm-amber base, shifted toward
    // the path's cosmological flavor.
    //   None       -- neutral warm-amber (pre-Beat-4 state).
    //   Penitent   -- warmer + slightly redder (the bent walk).
    //   Heretic    -- warmer + bronzed (the crooked carry).
    //   Wretched   -- duller + cooler (the non-completion).
    //   Unburdened -- paler + cooler (the channel toward Beatrice, light-bound).
    ImU32 vessel_color = IM_COL32(220, 200, 180, 240);
    switch (cls)
    {
    case PlayerClass::Penitent:
        vessel_color = IM_COL32(225, 185, 155, 240);
        break;
    case PlayerClass::Heretic:
        vessel_color = IM_COL32(210, 175, 130, 240);
        break;
    case PlayerClass::Wretched:
        vessel_color = IM_COL32(190, 175, 165, 240);
        break;
    case PlayerClass::Unburdened:
        vessel_color = IM_COL32(200, 215, 220, 240);
        break;
    case PlayerClass::None:
        break;
    }
    ImFont* font = ImGui::GetFont();
    const float font_size = ImGui::GetFontSize();
    float pen_x = origin.x;
    // Bar positions. Single bar sits 2px above the glyph; double bar
    // adds a second line 2px above that. Double-bar means ×1,000,000;
    // the second stripe is the visual cue that we're in the millions.
    constexpr float kSingleBarOffsetY = 2.0f;
    constexpr float kDoubleBarStripeGapY = 2.5f;
    for (std::size_t i = 0; i < rendering.glyphs.size(); ++i)
    {
        const char buf[2] = {rendering.glyphs[i], '\0'};
        const ImVec2 size = font->CalcTextSizeA(font_size, FLT_MAX, 0.0f, buf);
        draw->AddText(font, font_size, ImVec2(pen_x, text_y), vessel_color, buf);
        const std::uint8_t bar_count = rendering.bars[i];
        if (bar_count >= 1u)
        {
            const float y1 = text_y - kSingleBarOffsetY;
            draw->AddLine(ImVec2(pen_x, y1), ImVec2(pen_x + size.x, y1), vessel_color, 1.0f);
        }
        if (bar_count >= 2u)
        {
            const float y2 = text_y - kSingleBarOffsetY - kDoubleBarStripeGapY;
            draw->AddLine(ImVec2(pen_x, y2), ImVec2(pen_x + size.x, y2), vessel_color, 1.0f);
        }
        pen_x += size.x;
    }
}

} // namespace

glm::vec2 sangueHudAnchor()
{
    const ImGuiViewport* vp = ImGui::GetMainViewport();
    if (vp == nullptr)
        return glm::vec2(0.0f, 0.0f);
    using namespace kActorHudLayout;
    // Mirror renderActorHud's layout: panel anchored at WorkPos +
    // kMargin, vessel text drawn at +kHpHeight + kStaminaHeight +
    // 2*kBarGap from origin. Aim the pulse target at the center of
    // the vessel text glyph row (approximate y center = text_y +
    // kVesselHeight*0.5). Slight x bias so multi-particle streams
    // arrive into the leading edge of the readout, not its center,
    // which reads better when the counter pumps up.
    const float origin_x = vp->WorkPos.x + kMargin;
    const float origin_y = vp->WorkPos.y + kMargin;
    const float vessel_text_y = origin_y + kHpHeight + kStaminaHeight + (kBarGap * 2.0f);
    return glm::vec2(origin_x + 8.0f, vessel_text_y + kVesselHeight * 0.5f);
}

void renderActorHud()
{
    const auto& p = selva::gameplay::player();

    // Soulslike layout: top-left, ~250px bars, HP above stamina.
    // Layout constants live at file scope (kActorHudLayout namespace
    // below) so other systems (sangueHudAnchor, sangue pulse target)
    // share one source of truth.
    using namespace kActorHudLayout;

    const ImGuiViewport* vp = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(ImVec2(vp->WorkPos.x + kMargin, vp->WorkPos.y + kMargin),
                            ImGuiCond_Always);
    ImGui::SetNextWindowSize(ImVec2(kPanelW, kPanelH), ImGuiCond_Always);
    ImGui::SetNextWindowBgAlpha(0.0f);
    ImGui::Begin("##ActorHUD", nullptr,
                 ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoNav |
                     ImGuiWindowFlags_NoFocusOnAppearing | ImGuiWindowFlags_NoInputs |
                     ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoBackground);

    auto* draw = ImGui::GetWindowDrawList();
    const ImVec2 origin = ImGui::GetCursorScreenPos();

    const ImU32 bar_bg = IM_COL32(20, 20, 20, 220);
    const ImU32 border = IM_COL32(0, 0, 0, 220);
    const ImU32 hp_fg = IM_COL32(170, 30, 30, 240);
    const ImU32 stamina_fg = IM_COL32(70, 150, 70, 240);

    const float hp_fraction =
        (p.hp.max > 0) ? static_cast<float>(p.hp.current) / static_cast<float>(p.hp.max) : 0.0f;
    const float stamina_fraction = (p.stamina.max > 0) ? static_cast<float>(p.stamina.current) /
                                                             static_cast<float>(p.stamina.max)
                                                       : 0.0f;

    char hp_label[32];
    std::snprintf(hp_label, sizeof(hp_label), "HP  %d / %d", p.hp.current, p.hp.max);
    char stamina_label[32];
    std::snprintf(stamina_label, sizeof(stamina_label), "STA %d / %d",
                  static_cast<int>(std::floor(p.stamina.current)),
                  static_cast<int>(std::floor(p.stamina.max)));
    drawBar(draw, BarRect{origin.x, origin.y, kBarWidth, kHpHeight}, hp_fraction,
            BarColors{bar_bg, hp_fg, border}, hp_label);
    drawBar(draw, BarRect{origin.x, origin.y + kHpHeight + kBarGap, kBarWidth, kStaminaHeight},
            stamina_fraction, BarColors{bar_bg, stamina_fg, border}, stamina_label);

    // Vessel readout: number only, no label. Per
    drawVesselCounter(draw, origin);

    ImGui::Dummy(ImVec2(kPanelW - 16.0f, kPanelH - 16.0f));
    ImGui::End();

    // In-world overlay: enemy HP bars above each damaged enemy +
    // floating damage numbers. Both project from world to viewport
    // using the most recent view-projection matrix cached by the
    // render pass.
    const glm::mat4& view_proj = selva::render::lastViewProj();
    const float now = selva::wallClock();

    ImGui::SetNextWindowPos(vp->WorkPos, ImGuiCond_Always);
    ImGui::SetNextWindowSize(vp->WorkSize, ImGuiCond_Always);
    ImGui::SetNextWindowBgAlpha(0.0f);
    ImGui::Begin("##WorldOverlay", nullptr,
                 ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoNav |
                     ImGuiWindowFlags_NoFocusOnAppearing | ImGuiWindowFlags_NoInputs |
                     ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoBackground |
                     ImGuiWindowFlags_NoBringToFrontOnFocus);
    auto* overlay = ImGui::GetWindowDrawList();

    const auto list = selva::gameplay::enemies();
    drawEnemyHpBars(overlay, view_proj, list, now);
    drawFloatingDamageNumbers(overlay, view_proj);
    drawSanguePulses(overlay, view_proj);
    drawLockOnReticle(overlay, view_proj);
    drawSecondDeathCard();

    // Debug overlay: AI vision cones + awareness label per AI actor.
    // Toggled by F1 panel checkbox (selva::debug::flags().ai_perception).
    if (selva::debug::flags().ai_perception)
    {
        const auto& tun_dbg = selva::tuning::current();
        const float half_fov_rad = 0.5f * tun_dbg.ai_vision_fov_degrees * 0.017453293f;
        const float range = tun_dbg.ai_vision_range_meters;
        for (const auto* ep : list)
            drawActorPerceptionOverlay(overlay, view_proj, *ep, half_fov_rad, range);
    }

    // Debug overlay: outline every hitbox + hurtbox in the pool.
    // Hurtboxes in yellow (per-actor capsules), hitboxes in cyan
    // (active swing volumes). Toggled from the F1 panel.
    if (sShowHitVolumes)
    {
        const ImU32 hurtbox_color = IM_COL32(255, 220, 60, 200);
        const ImU32 hitbox_color = IM_COL32(60, 220, 255, 220);
        for (const auto& hu : selva::combat::hurtboxes())
            drawCapsuleOutline(overlay, view_proj, hu.shape.p0, hu.shape.p1, hu.shape.radius,
                               hurtbox_color);
        for (const auto& hb : selva::combat::hitboxes())
            drawCapsuleOutline(overlay, view_proj, hb.shape.p0, hb.shape.p1, hb.shape.radius,
                               hitbox_color);
    }

    ImGui::End();

    // Save indicator: brief bottom-right "Saving..." chip after every
    // autosave. Non-intrusive corner position, fades after a short
    // window. Hidden while the pause menu is open (the chip belongs
    // to gameplay overlay, not menu overlay).
    //
    // Uses SDL_GetTicks64() rather than selva::wallClock() because
    // wallClock freezes when the gameplay tick is gated off during
    // pause; the indicator must keep counting down in real time even
    // if the player paused immediately after triggering the save.
    constexpr std::uint64_t kSaveIndicatorMs = 2000;
    constexpr std::uint64_t kSaveIndicatorFadeStartMs = 1200;
    const auto& ui = selva::uiState();
    const bool pause_open = ui.isScreenOpen();
    if (!pause_open && ui.last_save_ticks_ms > 0)
    {
        const std::uint64_t now_ms = SDL_GetTicks64();
        const std::uint64_t age_ms =
            (now_ms >= ui.last_save_ticks_ms) ? (now_ms - ui.last_save_ticks_ms) : 0;
        if (age_ms < kSaveIndicatorMs)
        {
            float alpha = 1.0f;
            if (age_ms > kSaveIndicatorFadeStartMs)
                alpha = 1.0f - static_cast<float>(age_ms - kSaveIndicatorFadeStartMs) /
                                   static_cast<float>(kSaveIndicatorMs - kSaveIndicatorFadeStartMs);

            constexpr float kIndicatorMargin = 24.0f;
            constexpr float kIndicatorW = 140.0f;
            constexpr float kIndicatorH = 32.0f;
            const ImVec2 pos(vp->WorkPos.x + vp->WorkSize.x - kIndicatorW - kIndicatorMargin,
                             vp->WorkPos.y + vp->WorkSize.y - kIndicatorH - kIndicatorMargin);
            ImGui::SetNextWindowPos(pos, ImGuiCond_Always);
            ImGui::SetNextWindowSize(ImVec2(kIndicatorW, kIndicatorH), ImGuiCond_Always);
            ImGui::SetNextWindowBgAlpha(0.0f);
            ImGui::Begin("##SaveIndicator", nullptr,
                         ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoNav |
                             ImGuiWindowFlags_NoFocusOnAppearing | ImGuiWindowFlags_NoInputs |
                             ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoBackground);
            auto* d = ImGui::GetWindowDrawList();
            const ImVec2 p0 = ImGui::GetWindowPos();
            const float cx = p0.x + 14.0f;
            const float cy = p0.y + kIndicatorH * 0.5f;
            const auto fade_u32 = [alpha](int r, int g, int b, int a)
            { return IM_COL32(r, g, b, static_cast<int>(static_cast<float>(a) * alpha)); };
            d->AddCircleFilled(ImVec2(cx, cy), 5.0f, fade_u32(200, 200, 220, 255));
            d->AddText(ImVec2(cx + 12.0f, p0.y + 8.0f), fade_u32(220, 220, 230, 255), "Saving...");
            ImGui::End();
        }
    }
}

void renderCompass()
{
    // Strip width + visible angular range.
    constexpr float kStripWidth = 360.0f;
    constexpr float kStripHeight = 26.0f;
    constexpr float kVisibleArc = 120.0f; // ±60° from center → 120° total
    constexpr float kPxPerDeg = kStripWidth / kVisibleArc;

    // Convert camera yaw to compass degrees (0=N, 90=E, 180=S, 270=W).
    // World convention: yaw=0 looks south (-Z); lookFwd derivation is
    //   lookFwd.x = -sin(yaw), lookFwd.z = -cos(yaw)
    // → yaw=π is north, yaw=3π/2 is east. Convert to standard compass:
    //   compass_deg = (yaw_deg + 180) mod 360
    constexpr float kPi = 3.14159265358979f;
    const float yaw_rad = selva::render::cameraYaw();
    const float yaw_deg = yaw_rad * (180.0f / kPi);
    float compass_deg = std::fmod(yaw_deg + 180.0f, 360.0f);
    if (compass_deg < 0.0f)
        compass_deg += 360.0f;

    const ImGuiViewport* vp = ImGui::GetMainViewport();
    const float center_x = vp->WorkPos.x + vp->WorkSize.x * 0.5f;
    const float top_y = vp->WorkPos.y + 14.0f;
    const float strip_left = center_x - kStripWidth * 0.5f;
    const float strip_right = center_x + kStripWidth * 0.5f;
    const float strip_top = top_y;
    const float strip_bot = top_y + kStripHeight;

    ImGui::SetNextWindowPos(ImVec2(strip_left - 4.0f, strip_top - 4.0f), ImGuiCond_Always);
    ImGui::SetNextWindowSize(ImVec2(kStripWidth + 8.0f, kStripHeight + 8.0f), ImGuiCond_Always);
    ImGui::SetNextWindowBgAlpha(0.0f);
    ImGui::Begin("##Compass", nullptr,
                 ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoNav |
                     ImGuiWindowFlags_NoFocusOnAppearing | ImGuiWindowFlags_NoInputs |
                     ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoBackground);
    auto* draw = ImGui::GetWindowDrawList();

    // Strip background — thin dark band, low opacity so it doesn't
    // dominate the view.
    constexpr ImU32 kStripBg = IM_COL32(0, 0, 0, 130);
    constexpr ImU32 kTickMinor = IM_COL32(180, 180, 180, 140);
    constexpr ImU32 kTickMajor = IM_COL32(220, 220, 220, 220);
    constexpr ImU32 kTextCard = IM_COL32(240, 240, 240, 235);
    constexpr ImU32 kTextInter = IM_COL32(200, 200, 200, 200);
    constexpr ImU32 kCenter = IM_COL32(255, 220, 120, 255);

    draw->AddRectFilled(ImVec2(strip_left, strip_top), ImVec2(strip_right, strip_bot), kStripBg,
                        2.0f);

    // Compass tick marks every 5° (minor) and 15° (major); cardinal
    // letters every 45°. Iterate ±60° around the heading so off-screen
    // ticks aren't computed.
    struct Marker
    {
        float deg;
        const char* label;
        ImU32 color;
        bool major;
    };
    // Note: world axes in this game are +X = west, -X = east (camera
    // yaw is set up so turning right from facing south points toward
    // -X). Swap the intercardinal labels accordingly so the compass
    // matches real-world convention from the player's POV (facing S,
    // turning right shows SW → W → NW → N at center).
    const Marker kMarkers[] = {
        {0.0f, "N", kTextCard, true},   {45.0f, "NW", kTextInter, true},
        {90.0f, "W", kTextCard, true},  {135.0f, "SW", kTextInter, true},
        {180.0f, "S", kTextCard, true}, {225.0f, "SE", kTextInter, true},
        {270.0f, "E", kTextCard, true}, {315.0f, "NE", kTextInter, true},
    };

    // Helper: map a compass-direction (deg) to an X pixel position
    // in the strip. Returns NaN if the direction is more than half
    // the visible arc away from the current heading.
    auto degToX = [&](float dir_deg) -> float
    {
        const float diff = std::fmod(dir_deg - compass_deg + 540.0f, 360.0f) - 180.0f;
        return center_x + diff * kPxPerDeg;
    };

    // Tick marks every 5°, sweep from -60 to +60 of center.
    for (int i = -12; i <= 12; ++i)
    {
        const float dir = std::fmod(compass_deg + static_cast<float>(i) * 5.0f + 360.0f, 360.0f);
        const float x = degToX(dir);
        if (x < strip_left || x > strip_right)
            continue;
        const bool major = (i % 3) == 0; // every 15°
        const float h = major ? 8.0f : 4.0f;
        draw->AddLine(ImVec2(x, strip_bot - h), ImVec2(x, strip_bot - 1.0f),
                      major ? kTickMajor : kTickMinor, 1.0f);
    }

    // Cardinal + intercardinal letters.
    for (const auto& m : kMarkers)
    {
        const float x = degToX(m.deg);
        if (x < strip_left - 16.0f || x > strip_right + 16.0f)
            continue;
        const ImVec2 tsz = ImGui::CalcTextSize(m.label);
        draw->AddText(ImVec2(x - tsz.x * 0.5f, strip_top + 3.0f), m.color, m.label);
    }

    // Center marker — a small downward-pointing chevron above the
    // strip top, plus a vertical line through the strip indicating
    // "you are facing this direction."
    const float cx = center_x;
    draw->AddTriangleFilled(ImVec2(cx, strip_top - 2.0f), ImVec2(cx - 5.0f, strip_top - 9.0f),
                            ImVec2(cx + 5.0f, strip_top - 9.0f), kCenter);
    draw->AddLine(ImVec2(cx, strip_top), ImVec2(cx, strip_bot), kCenter, 1.5f);

    ImGui::End();
}

namespace
{
// Draw one cylinder: top + bottom ring + spokes at quarter-turns +
// label above the top center.
void drawColliderCylinder(const selva::world::CylinderCollider& c, const glm::mat4& vp,
                          ImDrawList* overlay)
{
    constexpr int kCylSegments = 16;
    const ImU32 cyl_color = IM_COL32(255, 200, 80, 220);
    const float ground_y = selva::world::sampleHeight(c.center.x, c.center.z);
    const float top_y = ground_y + c.half_height * 2.0f;
    glm::vec2 prev_lo;
    glm::vec2 prev_hi;
    bool have_prev = false;
    for (int i = 0; i <= kCylSegments; ++i)
    {
        const float t = static_cast<float>(i) / static_cast<float>(kCylSegments);
        const float ang = t * 6.2831853f;
        const float x = c.center.x + std::cos(ang) * c.radius;
        const float z = c.center.z + std::sin(ang) * c.radius;
        glm::vec2 sp_lo;
        glm::vec2 sp_hi;
        const bool lo_ok = selva::render::worldToScreen(vp, glm::vec3(x, ground_y, z), sp_lo);
        const bool hi_ok = selva::render::worldToScreen(vp, glm::vec3(x, top_y, z), sp_hi);
        if (have_prev && lo_ok)
            overlay->AddLine(ImVec2(prev_lo.x, prev_lo.y), ImVec2(sp_lo.x, sp_lo.y), cyl_color,
                             1.0f);
        if (have_prev && hi_ok)
            overlay->AddLine(ImVec2(prev_hi.x, prev_hi.y), ImVec2(sp_hi.x, sp_hi.y), cyl_color,
                             1.0f);
        if (i % 4 == 0 && lo_ok && hi_ok)
            overlay->AddLine(ImVec2(sp_lo.x, sp_lo.y), ImVec2(sp_hi.x, sp_hi.y), cyl_color, 1.0f);
        if (lo_ok)
            prev_lo = sp_lo;
        if (hi_ok)
            prev_hi = sp_hi;
        have_prev = lo_ok && hi_ok;
    }
    if (c.name != nullptr)
    {
        glm::vec2 label_pos;
        if (selva::render::worldToScreen(vp, glm::vec3(c.center.x, top_y + 0.10f, c.center.z),
                                         label_pos))
            overlay->AddText(ImVec2(label_pos.x - 4.0f, label_pos.y - 14.0f), cyl_color, c.name);
    }
}

// Draw one box: 4 vertical edges + top/bottom rectangles, with
// walkable_top slope rendered as a sloped top quad. Color encodes
// type: cyan = standard, green = walkable_top, magenta = camera_only.
void drawColliderBox(const selva::world::BoxCollider& b, const glm::mat4& vp, ImDrawList* overlay)
{
    const ImU32 col_standard = IM_COL32(80, 200, 255, 220);
    const ImU32 col_walkable = IM_COL32(80, 255, 120, 220);
    const ImU32 col_camera = IM_COL32(255, 100, 200, 220);
    const float lo_y = b.y_base;
    const float hi_y_center = b.y_base + 2.0f * b.half_height_y;
    const glm::vec2 corners_xz[4] = {
        {b.center.x - b.half_extents.x, b.center.y - b.half_extents.y},
        {b.center.x + b.half_extents.x, b.center.y - b.half_extents.y},
        {b.center.x + b.half_extents.x, b.center.y + b.half_extents.y},
        {b.center.x - b.half_extents.x, b.center.y + b.half_extents.y},
    };
    glm::vec2 s_lo[4];
    glm::vec2 s_hi[4];
    bool ok_lo[4];
    bool ok_hi[4];
    for (int i = 0; i < 4; ++i)
    {
        const float dx = corners_xz[i].x - b.center.x;
        const float dz = corners_xz[i].y - b.center.y;
        const float hi_y_here = hi_y_center + b.top_slope.x * dx + b.top_slope.y * dz;
        ok_lo[i] = selva::render::worldToScreen(
            vp, glm::vec3(corners_xz[i].x, lo_y, corners_xz[i].y), s_lo[i]);
        ok_hi[i] = selva::render::worldToScreen(
            vp, glm::vec3(corners_xz[i].x, hi_y_here, corners_xz[i].y), s_hi[i]);
    }
    const ImU32 color = b.camera_only ? col_camera : (b.walkable_top ? col_walkable : col_standard);
    for (int i = 0; i < 4; ++i)
    {
        const int j = (i + 1) % 4;
        if (ok_lo[i] && ok_lo[j])
            overlay->AddLine(ImVec2(s_lo[i].x, s_lo[i].y), ImVec2(s_lo[j].x, s_lo[j].y), color,
                             1.5f);
        if (ok_hi[i] && ok_hi[j])
            overlay->AddLine(ImVec2(s_hi[i].x, s_hi[i].y), ImVec2(s_hi[j].x, s_hi[j].y), color,
                             1.5f);
        if (ok_lo[i] && ok_hi[i])
            overlay->AddLine(ImVec2(s_lo[i].x, s_lo[i].y), ImVec2(s_hi[i].x, s_hi[i].y), color,
                             1.5f);
    }
    if (b.name != nullptr)
    {
        glm::vec2 label_pos;
        if (selva::render::worldToScreen(vp, glm::vec3(b.center.x, hi_y_center + 0.10f, b.center.y),
                                         label_pos))
            overlay->AddText(ImVec2(label_pos.x - 4.0f, label_pos.y - 14.0f), color, b.name);
    }
}

// Draw every registered StructureFootprint that cuts the floor as a
// red rect overlay. Reading the registry directly means the overlay
// can't drift from what the terrain shader actually sees.
void drawStructureFootprintRects(const glm::mat4& vp, ImDrawList* overlay)
{
    using namespace selva::world::crypt_layout;
    const ImU32 discard_color = IM_COL32(255, 60, 60, 220);
    const float terrain_y = selva::world::sampleHeight(kCryptX + kHalfWidth + 2.0f, kCryptZ);
    auto drawRect = [&](const glm::vec2& center, const glm::vec2& half_extents, const char* label)
    {
        if (half_extents.x <= 0.0f || half_extents.y <= 0.0f)
            return;
        const float c[4][2] = {
            {center.x - half_extents.x, center.y - half_extents.y},
            {center.x + half_extents.x, center.y - half_extents.y},
            {center.x + half_extents.x, center.y + half_extents.y},
            {center.x - half_extents.x, center.y + half_extents.y},
        };
        glm::vec2 sp[4];
        bool ok[4];
        for (int i = 0; i < 4; ++i)
            ok[i] = selva::render::worldToScreen(vp, glm::vec3(c[i][0], terrain_y + 0.05f, c[i][1]),
                                                 sp[i]);
        for (int i = 0; i < 4; ++i)
        {
            const int j = (i + 1) % 4;
            if (ok[i] && ok[j])
                overlay->AddLine(ImVec2(sp[i].x, sp[i].y), ImVec2(sp[j].x, sp[j].y), discard_color,
                                 2.0f);
        }
        glm::vec2 lp;
        if (selva::render::worldToScreen(vp, glm::vec3(center.x, terrain_y + 0.05f, center.y), lp))
            overlay->AddText(ImVec2(lp.x - 30.0f, lp.y), discard_color, label);
    };
    const int fp_count = engine::world::structureFootprintCount();
    for (int i = 0; i < fp_count; ++i)
    {
        const auto& f = engine::world::structureFootprintAt(i);
        if (!f.cuts_floor)
            continue;
        const char* label = f.debug_name ? f.debug_name : "structure_footprint";
        drawRect(f.center_xz, f.half_extents_xz, label);
    }
}
} // namespace

void renderColliderDebug()
{
    if (!selva::debug::flags().show_colliders)
        return;
    const auto& region = selva::world::currentRegion();
    const glm::mat4& vp = selva::render::lastViewProj();
    ImDrawList* overlay = ImGui::GetForegroundDrawList();
    for (const auto& c : region.cylinders)
        drawColliderCylinder(c, vp, overlay);
    for (const auto& b : region.boxes)
        drawColliderBox(b, vp, overlay);
    drawStructureFootprintRects(vp, overlay);
}

void renderPhysicsBodyDebug()
{
    if (!selva::debug::flags().show_physics_bodies)
        return;

    const glm::mat4& vp = selva::render::lastViewProj();
    ImDrawList* overlay = ImGui::GetForegroundDrawList();

    // Enumerate every Jolt body (static trimeshes, static boxes,
    // character capsules) and draw its world AABB colored by
    // SurfaceTag. This is the ground truth for what physics sees —
    // if a mesh isn't in here, it's not collidable, period.
    using engine::physics::BodyDebugInfo;
    using engine::physics::BodyKind;
    using engine::physics::SurfaceTag;
    static std::vector<BodyDebugInfo> bodies;
    engine::physics::enumerateBodies(bodies);
    // Skip if too many bodies — drawing thousands of AABBs costs
    // perceptible frame time. ~900 bodies (chapel x2 + terrain) is
    // the current Selva ceiling; cap at 2000 to leave headroom.
    if (bodies.size() > 2000)
        return;

    for (const auto& info : bodies)
    {
        ImU32 c;
        switch (info.tag)
        {
        case SurfaceTag::Terrain:
            c = IM_COL32(180, 120, 60, 180);
            break;
        case SurfaceTag::Architecture:
            c = IM_COL32(80, 180, 255, 180);
            break;
        case SurfaceTag::Foliage:
            c = IM_COL32(100, 220, 100, 180);
            break;
        case SurfaceTag::Actor:
            c = IM_COL32(255, 200, 80, 220);
            break;
        default:
            c = IM_COL32(200, 200, 200, 160);
            break;
        }
        const glm::vec3 mn = info.world_aabb_min;
        const glm::vec3 mx = info.world_aabb_max;
        const glm::vec3 corners[8] = {
            {mn.x, mn.y, mn.z}, {mx.x, mn.y, mn.z}, {mx.x, mn.y, mx.z}, {mn.x, mn.y, mx.z},
            {mn.x, mx.y, mn.z}, {mx.x, mx.y, mn.z}, {mx.x, mx.y, mx.z}, {mn.x, mx.y, mx.z},
        };
        glm::vec2 sp[8];
        bool ok[8];
        for (int i = 0; i < 8; ++i)
            ok[i] = selva::render::worldToScreen(vp, corners[i], sp[i]);
        const int edges[12][2] = {
            {0, 1}, {1, 2}, {2, 3}, {3, 0}, // bottom
            {4, 5}, {5, 6}, {6, 7}, {7, 4}, // top
            {0, 4}, {1, 5}, {2, 6}, {3, 7}, // verticals
        };
        for (auto& e : edges)
        {
            if (ok[e[0]] && ok[e[1]])
                overlay->AddLine(ImVec2(sp[e[0]].x, sp[e[0]].y), ImVec2(sp[e[1]].x, sp[e[1]].y), c,
                                 1.0f);
        }
        // Label at the AABB's top-center so the user can see which
        // body is which. Skip if name is empty.
        const char* name = engine::physics::bodyDebugName(info.body);
        if (name != nullptr && name[0] != '\0')
        {
            const glm::vec3 label_pos((mn.x + mx.x) * 0.5f, mx.y + 0.05f, (mn.z + mx.z) * 0.5f);
            glm::vec2 lp;
            if (selva::render::worldToScreen(vp, label_pos, lp))
            {
                // Estimate text width to center the label.
                const float text_w = ImGui::CalcTextSize(name).x;
                overlay->AddText(ImVec2(lp.x - text_w * 0.5f, lp.y - 14.0f), c, name);
            }
        }
    }
}

void renderSceneOverlays()
{
    using namespace engine::world;
    const auto& dbg = selva::debug::flags();
    ImDrawList* fg = ImGui::GetForegroundDrawList();
    const ImGuiIO& io = ImGui::GetIO();

    // ---- Fade-to-black during transitions (always-on) ----
    const float fade = transitionFadeAlpha();
    if (fade > 0.001f)
    {
        const ImU32 col = IM_COL32(0, 0, 0, static_cast<int>(fade * 255.0f));
        fg->AddRectFilled(ImVec2(0, 0), ImVec2(io.DisplaySize.x, io.DisplaySize.y), col);
    }

    // ---- Region-name chip in top-right (debug-gated) ----
    using engine::world::Region;
    Region* cur = currentRegionPtr();
    if (dbg.show_region_chip)
    {
        const std::string chip_text =
            cur != nullptr ? ("region: " + cur->regionId()) : "region: (none)";
        const ImVec2 ts = ImGui::CalcTextSize(chip_text.c_str());
        const float pad = 6.0f;
        const ImVec2 chip_min(io.DisplaySize.x - ts.x - 2 * pad - 8.0f, 8.0f);
        const ImVec2 chip_max(io.DisplaySize.x - 8.0f, 8.0f + ts.y + 2 * pad);
        fg->AddRectFilled(chip_min, chip_max, IM_COL32(0, 0, 0, 180), 4.0f);
        fg->AddText(ImVec2(chip_min.x + pad, chip_min.y + pad), IM_COL32(255, 255, 255, 220),
                    chip_text.c_str());
    }

    // ---- Territory volume wireframes (gated by show_territories) ----
    // Per-region law-domain AABBs. Color by owning region so overlap
    // is visually unambiguous (chapel's corridor nesting inside
    // limbo's disc shows up as two colors at the seam). Each volume
    // labeled with its debug_name at its center.
    if (dbg.show_territories)
        drawTerritoryWireframes(fg);

    // ---- Trigger volume wireframes (gated by selva::debug::flags().show_colliders) ----
    if (dbg.show_colliders && cur != nullptr)
    {
        const glm::mat4& vp = selva::render::lastViewProj();
        const ImU32 trig_color = IM_COL32(255, 220, 80, 220);
        for (const auto& t : cur->triggers())
        {
            const glm::vec3& c = t.center;
            const glm::vec3& h = t.half_extents;
            const glm::vec3 corners[8] = {
                {c.x - h.x, c.y - h.y, c.z - h.z}, {c.x + h.x, c.y - h.y, c.z - h.z},
                {c.x + h.x, c.y - h.y, c.z + h.z}, {c.x - h.x, c.y - h.y, c.z + h.z},
                {c.x - h.x, c.y + h.y, c.z - h.z}, {c.x + h.x, c.y + h.y, c.z - h.z},
                {c.x + h.x, c.y + h.y, c.z + h.z}, {c.x - h.x, c.y + h.y, c.z + h.z},
            };
            glm::vec2 sp[8];
            bool ok[8];
            for (int i = 0; i < 8; ++i)
                ok[i] = selva::render::worldToScreen(vp, corners[i], sp[i]);
            const int edges[12][2] = {
                {0, 1}, {1, 2}, {2, 3}, {3, 0}, {4, 5}, {5, 6},
                {6, 7}, {7, 4}, {0, 4}, {1, 5}, {2, 6}, {3, 7},
            };
            for (auto& e : edges)
            {
                if (ok[e[0]] && ok[e[1]])
                    fg->AddLine(ImVec2(sp[e[0]].x, sp[e[0]].y), ImVec2(sp[e[1]].x, sp[e[1]].y),
                                trig_color, 1.5f);
            }
            glm::vec2 lp;
            if (selva::render::worldToScreen(vp, c, lp))
                fg->AddText(ImVec2(lp.x, lp.y - 14.0f), trig_color, t.debug_name.c_str());
        }
    }
}

} // namespace selva::ui
