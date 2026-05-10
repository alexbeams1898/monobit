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
using selva::ui::renderComboHud;
using selva::combat::combatLog;
namespace tickstate = selva::gameplay::tickstate;

// References to combat singletons + sampler etc � the panel reads them
// to populate sliders and writes back to debug arms.
static selva::anim::ClipRegistry& sClips = selva::anim::clips();
static selva::anim::PoseSampler& sSampler = selva::anim::sampler();
static selva::anim::LocomotionConfig& sLocomotionConfig = selva::anim::locomotionConfig();
static selva::combat::PlayerEquipment& sEquipment = selva::combat::equipment();
static selva::combat::WeaponClassRegistry& sWeaponClasses = selva::combat::weaponClasses();
static selva::combat::AttackChainState& sChainRight =
    selva::combat::chain(selva::combat::HandSide::Right);
static selva::combat::AttackChainState& sChainLeft =
    selva::combat::chain(selva::combat::HandSide::Left);

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

static void selvaRenderImGui(Engine& /*engine*/, EntityManager& /*em*/)
{
    renderComboHud();

    if (!sShowTuningPanel)
        return;

    auto& tun = selva::tuning::current();
    ImGui::Begin("Selva Oscura Tuning (F1)", &sShowTuningPanel);

    if (ImGui::CollapsingHeader("Debug", ImGuiTreeNodeFlags_DefaultOpen))
    {
        ImGui::SliderFloat("Time scale", &tun.time_scale, 0.05f, 2.0f, "%.2fx");
        ImGui::SameLine();
        if (ImGui::SmallButton("1x"))
            tun.time_scale = 1.0f;
        ImGui::SameLine();
        if (ImGui::SmallButton("0.25x"))
            tun.time_scale = 0.25f;
    }

    if (ImGui::CollapsingHeader("Locomotion", ImGuiTreeNodeFlags_DefaultOpen))
    {
        tunedSlider("Turn rate (rad/s)", &tun.turn_rate, 1.0f, 30.0f, 0.5f, "%.1f");
    }

    if (ImGui::CollapsingHeader("Mouse-look", ImGuiTreeNodeFlags_DefaultOpen))
    {
        tunedSlider("Sensitivity", &tun.mouse_sensitivity, 0.0005f, 0.01f, 0.0005f, "%.4f");
        tunedSlider("Pitch min", &tun.pitch_min, -1.55f, 0.0f, 0.05f, "%.2f");
        tunedSlider("Pitch max", &tun.pitch_max, 0.0f, 1.55f, 0.05f, "%.2f");
    }

    if (ImGui::CollapsingHeader("Camera", ImGuiTreeNodeFlags_DefaultOpen))
    {
        tunedSlider("Follow distance", &tun.follow_distance, 1.0f, 15.0f, 0.5f, "%.1f");
        tunedSlider("Follow height", &tun.follow_height, 0.0f, 8.0f, 0.5f, "%.1f");
        tunedSlider("FOV (deg)", &tun.fov_degrees, 30.0f, 110.0f, 5.0f, "%.0f");
    }

    if (ImGui::CollapsingHeader("Animation", ImGuiTreeNodeFlags_DefaultOpen))
    {
        tunedSlider("Cross-fade (s)", &tun.anim_blend_seconds, 0.0f, 0.50f, 0.025f, "%.3f");
        tunedSlider("Combat-idle grace (s)", &tun.combat_idle_grace_seconds, 0.0f, 5.0f, 0.25f,
                    "%.2f");
        tunedSlider("Combat entry delay (s)", &tun.combat_entry_delay_seconds, 0.0f, 0.50f, 0.025f,
                    "%.3f");
    }

    if (ImGui::CollapsingHeader("Combat", ImGuiTreeNodeFlags_DefaultOpen))
    {
        // Matched-technique indicator. ChainObserver tracks press history
        // and reports which named technique the recent presses match.
        const auto& obs = selva::combat::chainState();
        ImGui::Text("Chain: %s @ %d   acc=%.2f%s",
                    obs.technique_id ? obs.technique_id : "-", obs.step,
                    obs.last_press_accuracy, obs.last_press_perfect ? " PERFECT" : "");
        tunedSlider("Combo reset grace (s)", &tun.combo_reset_grace_seconds, 0.05f, 2.0f, 0.05f,
                    "%.2f");
        tunedSlider("Input buffer (s)", &tun.combo_input_buffer_seconds, 0.05f, 0.50f, 0.025f,
                    "%.3f");
        tunedSlider("First-strike blend (s)", &tun.first_strike_blend_seconds, 0.05f, 0.50f,
                    0.025f, "%.3f");
        tunedSlider("Chain blend (s)", &tun.combo_chain_blend_seconds, 0.05f, 0.50f, 0.025f,
                    "%.3f");
        tunedSlider("Attack playback rate", &tun.attack_playback_rate, 0.5f, 2.5f, 0.05f, "%.2f");
        // Auto-re-resolves on change so the slider is live.
        if (ImGui::SliderFloat("Cancel velocity frac", &tun.cancel_open_velocity_fraction, 0.05f,
                               0.95f, "%.2f"))
            resolveAttackCancelOpenTimes();
        if (ImGui::Button("Re-resolve attack windows"))
            resolveAttackCancelOpenTimes();
        // Master debug switch — gates the combo HUD, the per-fire
        // combatLog spam, the splice-distance joint samples, and the
        // chain-link velocity diag. Off by default in normal play
        // because the per-attack diagnostic work is heavy enough to
        // visibly drop frames (Tracy showed selvaPerFrame max=77ms
        // with it on vs ~1ms baseline). Flip on when iterating.
        bool combat_debug_enabled = selva::combat::isCombatDebugEnabled();
        if (ImGui::Checkbox("Combat debug overlay + diagnostics", &combat_debug_enabled))
            selva::combat::setCombatDebugEnabled(combat_debug_enabled);

        // Live tuning slider for the equipped weapon's 1H light chain
        // entry [1] (the "second swing" — slash_3 in the default
        // sword loadout). Bypasses the auto-detect motion_start - 0.05
        // splice; the value here goes straight into resolved_chain_
        // link_start_seconds. Only useful while iterating on a
        // specific transition; once the right value is found, copy
        // it into the chain entry's `chain_link_start_seconds` field
        // in the weapon-class JSON. Re-resolve is invoked
        // automatically on slider change so the next chain fire
        // picks up the new value immediately.
        if (sEquipment.right != nullptr && sEquipment.right->cls != nullptr)
        {
            // Mutate via the registry's mutable map (sEquipment.right
            // holds const pointers — its weapon -> cls path is const).
            // Look up the class entry by id and edit there.
            auto it = sWeaponClasses.by_id.find(sEquipment.right->cls->id);
            if (it != sWeaponClasses.by_id.end() && !it->second.one_handed.light.empty())
            {
                auto& chain_vec = it->second.one_handed.light[0].attacks;
                if (chain_vec.size() >= 2)
                {
                    auto& slot1 = chain_vec[1];
                    const auto* clip = sClips.get(slot1.clip);
                    const float dur =
                        (clip != nullptr && clip->isLoaded()) ? clip->duration() : 1.5f;
                    float val = slot1.resolved_chain_link_start_seconds;
                    if (ImGui::SliderFloat("Slot1 chain_link_start (s)", &val, 0.0f, dur, "%.3f"))
                    {
                        slot1.chain_link_start_seconds = val;
                        slot1.resolved_chain_link_start_seconds = val;
                    }
                    // Per-attack blend duration. Live-edit; the next
                    // chain fire reads chain_link_blend_seconds. Range
                    // 0.05–0.80s — too short snaps, too long is
                    // sluggish.
                    float blend_val = (slot1.chain_link_blend_seconds >= 0.0f)
                                          ? slot1.chain_link_blend_seconds
                                          : tun.combo_chain_blend_seconds;
                    if (ImGui::SliderFloat("Slot1 chain_link_blend (s)", &blend_val, 0.05f, 0.80f,
                                           "%.3f"))
                        slot1.chain_link_blend_seconds = blend_val;
                }
            }
        }
    }

    if (ImGui::CollapsingHeader("Dodge", ImGuiTreeNodeFlags_DefaultOpen))
    {
        tunedSlider("Roll playback rate", &tun.roll_playback_rate, 0.5f, 2.5f, 0.05f, "%.2f");
        tunedSlider("Backstep playback rate", &tun.backstep_playback_rate, 0.5f, 2.5f, 0.05f,
                    "%.2f");
        tunedSlider("Tap window (s)", &tun.dodge_tap_window, 0.05f, 0.40f, 0.025f, "%.3f");
        tunedSlider("Steer rate (rad/s)", &tun.dodge_steer_rate, 0.0f, 15.0f, 0.5f, "%.1f");
    }

    if (ImGui::CollapsingHeader("Post-Attack Lockout", ImGuiTreeNodeFlags_DefaultOpen))
    {
        tunedSlider("Extension past cancel window (s)", &tun.attack_lockout_extension_seconds,
                    0.0f, 1.50f, 0.025f, "%.3f");
    }

    if (ImGui::CollapsingHeader("Animation Debug", ImGuiTreeNodeFlags_DefaultOpen))
    {
        // Build a sorted list of clip names from the registry. Static
        // cache so we're not allocating per frame; rebuilt only when the
        // registry size changes (it doesn't change after startup, but
        // be resilient).
        static std::vector<std::string> clip_names_sorted;
        if (clip_names_sorted.size() != sClips.by_name.size())
        {
            clip_names_sorted.clear();
            clip_names_sorted.reserve(sClips.by_name.size());
            for (const auto& kv : sClips.by_name)
                clip_names_sorted.push_back(kv.first);
            std::sort(clip_names_sorted.begin(), clip_names_sorted.end());
        }

        ImGui::TextUnformatted("Override: play one clip in isolation,");
        ImGui::TextUnformatted("bypass gameplay state machine.");

        const char* current_label =
            sDebugClipName.empty() ? "(off — gameplay drives anim)" : sDebugClipName.c_str();
        if (ImGui::BeginCombo("Clip", current_label))
        {
            // First entry: turn debug off.
            const bool none_selected = sDebugClipName.empty();
            if (ImGui::Selectable("(off — gameplay drives anim)", none_selected))
                sDebugClipName.clear();
            ImGui::Separator();
            for (const auto& name : clip_names_sorted)
            {
                const bool selected = (name == sDebugClipName);
                if (ImGui::Selectable(name.c_str(), selected))
                    sDebugClipName = name;
                if (selected)
                    ImGui::SetItemDefaultFocus();
            }
            ImGui::EndCombo();
        }

        if (!sDebugClipName.empty())
        {
            const auto* dbg = sClips.get(sDebugClipName);
            if (dbg != nullptr && dbg->isLoaded())
            {
                ImGui::Text("duration: %.3fs   tracks: %d", dbg->duration(), dbg->trackCount());
            }
            if (ImGui::Button("Stop debug clip"))
                sDebugClipName.clear();

            // CSV recorder. Captures every frame's joint world-space
            // positions while the named clip plays in isolation, writes
            // to debug_bones_game_<clip>.csv next to the exe. The
            // browser previewer has a matching "Export bone CSV" button
            // that produces the same shape; a python diff script
            // compares them.
            ImGui::SameLine();
            if (sDebugRecording)
            {
                ImGui::Text("Recording... %.2f / %.2fs", sDebugRecordElapsed, sDebugRecordDuration);
            }
            else if (dbg != nullptr && dbg->isLoaded() && ImGui::Button("Record CSV"))
            {
                sDebugRecording = true;
                sDebugRecordElapsed = 0.0f;
                sDebugRecordDuration = dbg->duration();
                sDebugRecordClipName = sDebugClipName;
                tickstate::resetDebugRecordSamples(
                    static_cast<std::size_t>(dbg->duration() * 65.0f));
            }
        }

        // Dodge recorder: armed via this button, fires the next time
        // a dodge plays (Space tap). Lets us measure the playOneShot
        // path's bone trajectories — comparing against the F1 update()
        // recording of the same clip reveals whether the two paths
        // produce different poses for the same input clip.
        ImGui::Separator();
        if (sDebugRecordArmedNextDodge)
        {
            ImGui::TextUnformatted("Dodge recorder ARMED — tap Space to capture.");
            if (ImGui::Button("Cancel arm"))
                sDebugRecordArmedNextDodge = false;
        }
        else if (sDebugRecording && sDebugRecordClipName.rfind("dodge_", 0) == 0)
        {
            ImGui::Text("Recording dodge... %.2f / %.2fs", sDebugRecordElapsed,
                        sDebugRecordDuration);
        }
        else
        {
            if (ImGui::Button("Arm dodge recorder"))
                sDebugRecordArmedNextDodge = true;
        }

        // Frame capture: write a PNG per rendered frame for the next
        // dodge. PNGs land in build/bin/frame_capture/. Use to visually
        // diagnose what's happening on screen during the dodge instead
        // of relying on description.
        ImGui::Separator();
        if (sFrameCaptureArmedNextChain)
        {
            ImGui::TextUnformatted("Frame capture ARMED — click LMB to start chain.");
            if (ImGui::Button("Cancel chain frame arm"))
                sFrameCaptureArmedNextChain = false;
        }
        else if (sFrameCaptureArmedNextDodge)
        {
            ImGui::TextUnformatted("Frame capture ARMED — tap Space to capture.");
            if (ImGui::Button("Cancel frame arm"))
                sFrameCaptureArmedNextDodge = false;
        }
        else if (tickstate::frameCaptureActive())
        {
            ImGui::Text("Capturing frames... %.2f / %.2fs (%d frames)",
                        tickstate::frameCaptureElapsed(), tickstate::frameCaptureDuration(),
                        tickstate::frameCaptureCounter());
        }
        else
        {
            if (ImGui::Button("Arm frame capture (next dodge)"))
                sFrameCaptureArmedNextDodge = true;
            ImGui::SameLine();
            if (ImGui::Button("Arm frame capture (next chain)"))
                sFrameCaptureArmedNextChain = true;
        }
    }

    ImGui::Separator();
    if (ImGui::Button("Save to config/tunables.json"))
    {
        if (!selva::tuning::saveToFile(kTunablesPath))
            std::fprintf(stderr, "[Tuning] Failed to save %s\n", kTunablesPath.c_str());
    }
    ImGui::SameLine();
    if (ImGui::Button("Reload from disk"))
        selva::tuning::loadFromFile(kTunablesPath);

    ImGui::End();
}

namespace selva::ui
{
void selvaRenderImGui(::Engine& engine, ::EntityManager& em)
{
    ::selvaRenderImGui(engine, em);
}
} // namespace selva::ui

