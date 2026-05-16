#include "ui/ActorHud.h"

#include "Tunables.h"
#include "WallClock.h"
#include "anim/PoseSampler.h"
#include "anim/SkeletalAssets.h"
#include "anim/SkeletalMesh.h"
#include "combat/ActorVolumes.h"
#include "combat/HitFeedback.h"
#include "combat/HitVolumes.h"
#include "gameplay/Enemies.h"
#include "gameplay/Perception.h"
#include "gameplay/PlayerState.h"
#include "render/Camera.h"

#include <glm/geometric.hpp>
#include <glm/vec2.hpp>
#include <imgui.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <vector>

namespace selva::ui
{

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

// Draw the lock-on reticle on the player's current target. Phase A
// placeholder: 16×16 white square at chest-height. Phase B replaces
// with an iconographic sprite.
void drawLockOnReticle(ImDrawList* overlay, const glm::mat4& view_proj);

void drawBar(ImDrawList* draw, float x, float y, float w, float h, float fill_fraction,
             ImU32 bg_color, ImU32 fg_color, ImU32 border_color, const char* label_text)
{
    const float fill_w = std::clamp(fill_fraction, 0.0f, 1.0f) * w;
    draw->AddRectFilled(ImVec2(x, y), ImVec2(x + w, y + h), bg_color);
    if (fill_w > 0.0f)
        draw->AddRectFilled(ImVec2(x, y), ImVec2(x + fill_w, y + h), fg_color);
    draw->AddRect(ImVec2(x, y), ImVec2(x + w, y + h), border_color);
    if (label_text != nullptr && label_text[0] != '\0')
    {
        const ImU32 text_color = IM_COL32(230, 230, 230, 255);
        const ImVec2 ts = ImGui::CalcTextSize(label_text);
        draw->AddText(ImVec2(x + 6.0f, y + (h - ts.y) * 0.5f), text_color, label_text);
    }
}

void drawEnemyHpBars(ImDrawList* overlay, const glm::mat4& view_proj,
                     const std::vector<selva::gameplay::Actor*>& list, float now)
{
    for (const auto* ep : list)
    {
        const auto& e = *ep;
        if (e.last_damage_time < 0.0f)
            continue;
        const float age = now - e.last_damage_time;
        if (age > kEnemyHpBarHoldSeconds + kEnemyHpBarFadeSeconds)
            continue;
        const glm::vec3 anchor(e.pos.x, e.pos.y + 1.8f + kEnemyHpBarHeadOffset, e.pos.z);
        glm::vec2 sp;
        if (!selva::render::worldToScreen(view_proj, anchor, sp))
            continue;
        float alpha = 1.0f;
        if (age > kEnemyHpBarHoldSeconds)
            alpha = std::clamp(1.0f - (age - kEnemyHpBarHoldSeconds) / kEnemyHpBarFadeSeconds, 0.0f,
                               1.0f);
        const ImU32 bg = IM_COL32(20, 20, 20, static_cast<int>(220 * alpha));
        const ImU32 fg = IM_COL32(170, 30, 30, static_cast<int>(240 * alpha));
        const ImU32 bd = IM_COL32(0, 0, 0, static_cast<int>(220 * alpha));
        const float fraction =
            (e.hp.max > 0) ? static_cast<float>(e.hp.current) / static_cast<float>(e.hp.max) : 0.0f;
        const float bx = sp.x - kEnemyHpBarWidthPx * 0.5f;
        const float by = sp.y - kEnemyHpBarHeightPx * 0.5f;
        drawBar(overlay, bx, by, kEnemyHpBarWidthPx, kEnemyHpBarHeightPx, fraction, bg, fg, bd,
                nullptr);
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
    // Anchor to the chest BONE, not actor pos + Y. During dynamic
    // poses (knockdown, getting up, hit reacts) the rig decouples
    // from actor.pos — pos stays standing while the body drops to
    // the floor. Same pattern as ActorVolumes (jointWorld helper).
    const int chest_idx = target.sampler.findJoint("mixamorig:Spine2");
    if (chest_idx < 0)
        return;
    const glm::mat4 model = selva::combat::buildActorModelMatrix(
        target.pos, target.yaw, selva::anim::playerMesh().foot_offset_y);
    const glm::vec4 world = model * glm::vec4(target.sampler.jointWorldPos(chest_idx), 1.0f);
    glm::vec2 sp;
    if (!selva::render::worldToScreen(view_proj, glm::vec3(world), sp))
        return;
    constexpr float kReticleHalfSize = 8.0f;
    const ImU32 white = IM_COL32(255, 255, 255, 230);
    overlay->AddRectFilled(ImVec2(sp.x - kReticleHalfSize, sp.y - kReticleHalfSize),
                           ImVec2(sp.x + kReticleHalfSize, sp.y + kReticleHalfSize), white);
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

} // namespace

void renderActorHud()
{
    const auto& p = selva::gameplay::player();

    // Soulslike layout: top-left, ~250px bars, HP above stamina.
    constexpr float kMargin = 18.0f;
    constexpr float kBarWidth = 260.0f;
    constexpr float kHpHeight = 14.0f;
    constexpr float kStaminaHeight = 10.0f;
    constexpr float kBarGap = 4.0f;
    constexpr float kPanelW = kBarWidth + 16.0f;
    constexpr float kPanelH = kHpHeight + kStaminaHeight + kBarGap + 16.0f;

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
    drawBar(draw, origin.x, origin.y, kBarWidth, kHpHeight, hp_fraction, bar_bg, hp_fg, border,
            hp_label);
    drawBar(draw, origin.x, origin.y + kHpHeight + kBarGap, kBarWidth, kStaminaHeight,
            stamina_fraction, bar_bg, stamina_fg, border, nullptr);

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
    drawLockOnReticle(overlay, view_proj);

    // Debug overlay: AI vision cones + awareness label per AI actor.
    // Toggled by F1 panel checkbox debug_ai_perception.
    if (selva::tuning::current().debug_ai_perception)
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
}

} // namespace selva::ui
