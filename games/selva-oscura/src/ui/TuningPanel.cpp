#include "ui/TuningPanel.h"

#include "Engine.h"
#include "Tunables.h"
#include "WallClock.h"
#include "anim/AnimationClip.h"
#include "anim/ClipRegistry.h"
#include "anim/LocomotionConfig.h"
#include "anim/PoseSampler.h"
#include "anim/SkeletalAssets.h"
#include "anim/SkeletalMesh.h"
#include "combat/AttackChain.h"
#include "combat/AttackResolution.h"
#include "combat/ChainObserver.h"
#include "combat/CombatData.h"
#include "combat/CombatLog.h"
#include "combat/PlayerEquipment.h"
#include "combat/Weapon.h"
#include "combat/WeaponClass.h"
#include "gameplay/LocomotionStateMachine.h"
#include "gameplay/PlayerState.h"
#include "gameplay/TickState.h"
#include "ui/ComboHud.h"

#include <imgui.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

// Aliases so the transplanted ImGui code reads the same as it did in
// main.cpp.
using selva::combat::combatLog;
using selva::ui::renderComboHud;
namespace tickstate = selva::gameplay::tickstate;

// References to combat singletons + sampler etc � the panel reads them
// to populate sliders and writes back to debug arms.
static selva::anim::ClipRegistry& sClips = selva::anim::clips();
static selva::anim::PoseSampler& sSampler = selva::anim::sampler();
static selva::anim::LocomotionConfig& sLocomotionConfig = selva::anim::locomotionConfig();
static selva::combat::PlayerEquipment& sEquipment = selva::combat::equipment();
static selva::combat::WeaponClassRegistry& sWeaponClasses = selva::combat::weaponClasses();

// Resolve attack cancel-open times via the registry-aware impl. Re-bind
// to a 0-arg version to mirror main.cpp's earlier wrapper usage.
static void resolveAttackCancelOpenTimes()
{
    selva::combat::resolveAttackCancelOpenTimes(sWeaponClasses, sClips, sSampler);
}

// Tunables save path mirrors main.cpp.
static const std::string kTunablesPath = "config/tunables.json";

static bool& sShowTuningPanel = tickstate::showTuningPanel();
static std::string& sDebugClipName = tickstate::debugClipName();
static bool& sDebugLoop = tickstate::debugLoop();
static bool& sDebugRecording = tickstate::debugRecording();
static float& sDebugRecordElapsed = tickstate::debugRecordElapsed();
static float& sDebugRecordDuration = tickstate::debugRecordDuration();
static std::string& sDebugRecordClipName = tickstate::debugRecordClipName();
static bool& sDebugRecordArmedNextDodge = tickstate::debugRecordArmedNextDodge();
static bool& sFrameCaptureArmedNextDodge = tickstate::frameCaptureArmedNextDodge();
static bool& sFrameCaptureArmedNextChain = tickstate::frameCaptureArmedNextChain();

static void tunedSlider(const char* label, float* val, float min, float max, float step,
                        const char* fmt = "%.2f")
{
    if (ImGui::SliderFloat(label, val, min, max, fmt))
    {
        if (step > 0.0f)
            *val = std::round(*val / step) * step;
    }
}

static void renderDebugSection(selva::tuning::Tunables& tun)
{
    if (!ImGui::CollapsingHeader("Debug", ImGuiTreeNodeFlags_DefaultOpen))
        return;
    ImGui::SliderFloat("Time scale", &tun.time_scale, 0.05f, 2.0f, "%.2fx");
    ImGui::SameLine();
    if (ImGui::SmallButton("1x"))
        tun.time_scale = 1.0f;
    ImGui::SameLine();
    if (ImGui::SmallButton("0.25x"))
        tun.time_scale = 0.25f;
}

static void renderLocomotionSection(selva::tuning::Tunables& tun)
{
    if (!ImGui::CollapsingHeader("Locomotion", ImGuiTreeNodeFlags_DefaultOpen))
        return;
    tunedSlider("Turn rate (rad/s)", &tun.turn_rate, 1.0f, 30.0f, 0.5f, "%.1f");
}

static void renderMouseLookSection(selva::tuning::Tunables& tun)
{
    if (!ImGui::CollapsingHeader("Mouse-look", ImGuiTreeNodeFlags_DefaultOpen))
        return;
    tunedSlider("Sensitivity", &tun.mouse_sensitivity, 0.0005f, 0.01f, 0.0005f, "%.4f");
    tunedSlider("Pitch min", &tun.pitch_min, -1.55f, 0.0f, 0.05f, "%.2f");
    tunedSlider("Pitch max", &tun.pitch_max, 0.0f, 1.55f, 0.05f, "%.2f");
}

static void renderCameraSection(selva::tuning::Tunables& tun)
{
    if (!ImGui::CollapsingHeader("Camera", ImGuiTreeNodeFlags_DefaultOpen))
        return;
    tunedSlider("Follow distance", &tun.follow_distance, 1.0f, 15.0f, 0.5f, "%.1f");
    tunedSlider("Follow height", &tun.follow_height, 0.0f, 8.0f, 0.5f, "%.1f");
    tunedSlider("FOV (deg)", &tun.fov_degrees, 30.0f, 110.0f, 5.0f, "%.0f");
}

static void renderAnimationSection(selva::tuning::Tunables& tun)
{
    if (!ImGui::CollapsingHeader("Animation", ImGuiTreeNodeFlags_DefaultOpen))
        return;
    tunedSlider("Cross-fade (s)", &tun.anim_blend_seconds, 0.0f, 0.50f, 0.025f, "%.3f");
    tunedSlider("Combat-idle grace (s)", &tun.combat_idle_grace_seconds, 0.0f, 5.0f, 0.25f, "%.2f");
    tunedSlider("Combat entry delay (s)", &tun.combat_entry_delay_seconds, 0.0f, 0.50f, 0.025f,
                "%.3f");
}

// Live slider for the equipped weapon's 1H light slot-1 chain-link
// start. Bypasses the auto-detect splice; value goes straight into
// resolved_chain_link_start_seconds. Iteration helper — once a good
// value is found, copy it into the JSON.
static void renderSlot1ChainLinkStartSlider(selva::tuning::Tunables& tun)
{
    if (sEquipment.right == nullptr || sEquipment.right->cls == nullptr)
        return;
    auto it = sWeaponClasses.by_id.find(sEquipment.right->cls->id);
    if (it == sWeaponClasses.by_id.end() || it->second.one_handed.light.empty())
        return;
    auto& chain_vec = it->second.one_handed.light[0].attacks;
    if (chain_vec.size() < 2)
        return;
    auto& slot1 = chain_vec[1];
    const auto* clip = sClips.get(slot1.clip);
    const float dur = (clip != nullptr && clip->isLoaded()) ? clip->duration() : 1.5f;
    float val = slot1.resolved_chain_link_start_seconds;
    if (ImGui::SliderFloat("Slot1 chain_link_start (s)", &val, 0.0f, dur, "%.3f"))
    {
        slot1.chain_link_start_seconds = val;
        slot1.resolved_chain_link_start_seconds = val;
    }
    float blend_val = (slot1.chain_link_blend_seconds >= 0.0f) ? slot1.chain_link_blend_seconds
                                                               : tun.combo_chain_blend_seconds;
    if (ImGui::SliderFloat("Slot1 chain_link_blend (s)", &blend_val, 0.05f, 0.80f, "%.3f"))
        slot1.chain_link_blend_seconds = blend_val;
}

static void renderCombatSection(selva::tuning::Tunables& tun)
{
    if (!ImGui::CollapsingHeader("Combat", ImGuiTreeNodeFlags_DefaultOpen))
        return;
    // Matched-technique indicator. ChainObserver tracks press history.
    const auto& obs = selva::combat::chainState();
    ImGui::Text("Chain: %s @ %d   acc=%.2f%s", obs.technique_id ? obs.technique_id : "-", obs.step,
                obs.last_press_accuracy, obs.last_press_perfect ? " PERFECT" : "");
    tunedSlider("Combo reset grace (s)", &tun.combo_reset_grace_seconds, 0.05f, 2.0f, 0.05f,
                "%.2f");
    tunedSlider("Input buffer (s)", &tun.combo_input_buffer_seconds, 0.05f, 0.50f, 0.025f, "%.3f");
    tunedSlider("First-strike blend (s)", &tun.first_strike_blend_seconds, 0.05f, 0.50f, 0.025f,
                "%.3f");
    tunedSlider("Chain blend (s)", &tun.combo_chain_blend_seconds, 0.05f, 0.50f, 0.025f, "%.3f");
    tunedSlider("Attack playback rate", &tun.attack_playback_rate, 0.5f, 2.5f, 0.05f, "%.2f");
    if (ImGui::SliderFloat("Cancel velocity frac", &tun.cancel_open_velocity_fraction, 0.05f, 0.95f,
                           "%.2f"))
        resolveAttackCancelOpenTimes();
    if (ImGui::Button("Re-resolve attack windows"))
        resolveAttackCancelOpenTimes();
    bool combat_debug_enabled = selva::combat::isCombatDebugEnabled();
    if (ImGui::Checkbox("Combat debug overlay + diagnostics", &combat_debug_enabled))
        selva::combat::setCombatDebugEnabled(combat_debug_enabled);
    renderSlot1ChainLinkStartSlider(tun);
}

static void renderDodgeSection(selva::tuning::Tunables& tun)
{
    if (!ImGui::CollapsingHeader("Dodge", ImGuiTreeNodeFlags_DefaultOpen))
        return;
    tunedSlider("Roll playback rate", &tun.roll_playback_rate, 0.5f, 2.5f, 0.05f, "%.2f");
    tunedSlider("Backstep playback rate", &tun.backstep_playback_rate, 0.5f, 2.5f, 0.05f, "%.2f");
    tunedSlider("Tap window (s)", &tun.dodge_tap_window, 0.05f, 0.40f, 0.025f, "%.3f");
    tunedSlider("Steer rate (rad/s)", &tun.dodge_steer_rate, 0.0f, 15.0f, 0.5f, "%.1f");
}

static void renderPostAttackLockoutSection(selva::tuning::Tunables& tun)
{
    if (!ImGui::CollapsingHeader("Post-Attack Lockout", ImGuiTreeNodeFlags_DefaultOpen))
        return;
    tunedSlider("Extension past cancel window (s)", &tun.attack_lockout_extension_seconds, 0.0f,
                1.50f, 0.025f, "%.3f");
}

// Build / cache a sorted clip-name list. Static cache so we don't
// allocate per frame; rebuilt when the registry size changes.
static const std::vector<std::string>& sortedClipNames()
{
    static std::vector<std::string> cache;
    if (cache.size() != sClips.by_name.size())
    {
        cache.clear();
        cache.reserve(sClips.by_name.size());
        for (const auto& kv : sClips.by_name)
            cache.push_back(kv.first);
        std::sort(cache.begin(), cache.end());
    }
    return cache;
}

static void renderClipPicker()
{
    const auto& names = sortedClipNames();
    const char* current_label =
        sDebugClipName.empty() ? "(off — gameplay drives anim)" : sDebugClipName.c_str();
    if (!ImGui::BeginCombo("Clip", current_label))
        return;
    if (ImGui::Selectable("(off — gameplay drives anim)", sDebugClipName.empty()))
        sDebugClipName.clear();
    ImGui::Separator();
    for (const auto& name : names)
    {
        const bool selected = (name == sDebugClipName);
        if (ImGui::Selectable(name.c_str(), selected))
            sDebugClipName = name;
        if (selected)
            ImGui::SetItemDefaultFocus();
    }
    ImGui::EndCombo();
}

static void renderClipDebugRecorder(const selva::anim::AnimationClip* dbg)
{
    if (sDebugRecording)
    {
        ImGui::Text("Recording... %.2f / %.2fs", sDebugRecordElapsed, sDebugRecordDuration);
        return;
    }
    if (dbg == nullptr || !dbg->isLoaded())
        return;
    if (!ImGui::Button("Record CSV"))
        return;
    sDebugRecording = true;
    sDebugRecordElapsed = 0.0f;
    sDebugRecordDuration = dbg->duration();
    sDebugRecordClipName = sDebugClipName;
    tickstate::resetDebugRecordSamples(static_cast<std::size_t>(dbg->duration() * 65.0f));
}

static void renderDodgeRecorder()
{
    if (sDebugRecordArmedNextDodge)
    {
        ImGui::TextUnformatted("Dodge recorder ARMED — tap Space to capture.");
        if (ImGui::Button("Cancel arm"))
            sDebugRecordArmedNextDodge = false;
        return;
    }
    if (sDebugRecording && sDebugRecordClipName.rfind("dodge_", 0) == 0)
    {
        ImGui::Text("Recording dodge... %.2f / %.2fs", sDebugRecordElapsed, sDebugRecordDuration);
        return;
    }
    if (ImGui::Button("Arm dodge recorder"))
        sDebugRecordArmedNextDodge = true;
}

static void renderFrameCaptureRecorder()
{
    if (sFrameCaptureArmedNextChain)
    {
        ImGui::TextUnformatted("Frame capture ARMED — click LMB to start chain.");
        if (ImGui::Button("Cancel chain frame arm"))
            sFrameCaptureArmedNextChain = false;
        return;
    }
    if (sFrameCaptureArmedNextDodge)
    {
        ImGui::TextUnformatted("Frame capture ARMED — tap Space to capture.");
        if (ImGui::Button("Cancel frame arm"))
            sFrameCaptureArmedNextDodge = false;
        return;
    }
    if (tickstate::frameCaptureActive())
    {
        ImGui::Text("Capturing frames... %.2f / %.2fs (%d frames)",
                    tickstate::frameCaptureElapsed(), tickstate::frameCaptureDuration(),
                    tickstate::frameCaptureCounter());
        return;
    }
    if (ImGui::Button("Arm frame capture (next dodge)"))
        sFrameCaptureArmedNextDodge = true;
    ImGui::SameLine();
    if (ImGui::Button("Arm frame capture (next chain)"))
        sFrameCaptureArmedNextChain = true;
}

static void renderAnimationDebugSection()
{
    if (!ImGui::CollapsingHeader("Animation Debug", ImGuiTreeNodeFlags_DefaultOpen))
        return;
    ImGui::TextUnformatted("Override: play one clip in isolation,");
    ImGui::TextUnformatted("bypass gameplay state machine.");
    renderClipPicker();
    if (!sDebugClipName.empty())
    {
        const auto* dbg = sClips.get(sDebugClipName);
        if (dbg != nullptr && dbg->isLoaded())
            ImGui::Text("duration: %.3fs   tracks: %d", dbg->duration(), dbg->trackCount());
        if (ImGui::Button("Stop debug clip"))
            sDebugClipName.clear();
        ImGui::SameLine();
        renderClipDebugRecorder(dbg);
    }
    ImGui::Separator();
    renderDodgeRecorder();
    ImGui::Separator();
    renderFrameCaptureRecorder();
}

static void renderSaveLoadButtons()
{
    ImGui::Separator();
    if (ImGui::Button("Save to config/tunables.json"))
    {
        if (!selva::tuning::saveToFile(kTunablesPath))
            std::fprintf(stderr, "[Tuning] Failed to save %s\n", kTunablesPath.c_str());
    }
    ImGui::SameLine();
    if (ImGui::Button("Reload from disk"))
        selva::tuning::loadFromFile(kTunablesPath);
}

static void selvaRenderImGui(Engine& /*engine*/, EntityManager& /*em*/)
{
    renderComboHud();
    if (!sShowTuningPanel)
        return;
    auto& tun = selva::tuning::current();
    ImGui::Begin("Selva Oscura Tuning (F1)", &sShowTuningPanel);
    renderDebugSection(tun);
    renderLocomotionSection(tun);
    renderMouseLookSection(tun);
    renderCameraSection(tun);
    renderAnimationSection(tun);
    renderCombatSection(tun);
    renderDodgeSection(tun);
    renderPostAttackLockoutSection(tun);
    renderAnimationDebugSection();
    renderSaveLoadButtons();
    ImGui::End();
}

namespace selva::ui
{
void selvaRenderImGui(::Engine& engine, ::EntityManager& em)
{
    ::selvaRenderImGui(engine, em);
}
} // namespace selva::ui
