#pragma once

#include <nlohmann/json.hpp>
#include <string>

// ---------------------------------------------------------------------------
// Tunables — every numeric "feel" parameter for selva-oscura, in one place.
// Loaded from JSON at startup (config/tunables.json), edited at runtime via
// the in-game ImGui panel (F1), saved back to disk on demand. Plain struct
// of floats; one global instance shared across gameplay code and the
// procedural animation driver.
//
// Adding a tunable:
//   1. Add a field below with a default value.
//   2. Add the field name to the NLOHMANN_DEFINE_TYPE_INTRUSIVE_WITH_DEFAULT
//      macro at the bottom (this generates JSON load + save).
//   3. Add a slider entry in selva::tuning::renderImGui (in main.cpp).
// That's it — no constexpr, no rebuild-to-tune.
//
// Industry context: same pattern as Rockstar's Tunables, Source's cvars,
// Unreal's GameUserSettings. Plain struct + JSON + ImGui scales cleanly
// to ~50 fields; revisit (sub-structs, reflection macro, user-scope file)
// when we cross that threshold. See conversation notes 2026-05-05.
// ---------------------------------------------------------------------------

namespace selva::tuning
{

struct Tunables
{
    // ---- Locomotion ----
    float turn_rate = 9.0f; // player rotation toward move dir, rad/s

    // ---- Mouse-look ----
    float mouse_sensitivity = 0.0025f; // radians per pixel
    float pitch_min = -1.45f;          // ~-83 deg
    float pitch_max = 1.45f;           // ~+83 deg

    // ---- Camera follow ----
    float follow_distance = 6.0f;
    float follow_height = 2.5f;
    float fov_degrees = 60.0f;

    // ---- Animation transitions ----
    // Cross-fade duration (seconds) between two clips when the active
    // selection changes (Idle ↔ Walk ↔ Run). 0 disables the fade and
    // snaps instantly. ~0.15-0.25s reads as "smooth" without making
    // the transition feel laggy.
    float anim_blend_seconds = 0.20f;
    // How long to remain in CombatReady stance after the last combat
    // input. The "stays braced for a moment" feel — long
    // enough that a quick re-engage doesn't snap back to peaceful;
    // short enough that walking away clearly drops the stance.
    float combat_idle_grace_seconds = 2.0f;
    // Delay (wall-clock seconds) between a Peaceful → Attack click and
    // the swing one-shot actually firing. During this window the
    // locomotion track crossfades from standard_idle to the combat-
    // stance idle clip, so the player visibly settles into a guard
    // pose BEFORE the first slash starts. Without this delay the
    // first attack from peaceful idle reads as a teleport-into-stance
    // (the attack clip's t=0 pose IS combat-stance, so the first
    // visible frame of the swing is already braced — no establishing
    // beat). Sized to roughly match the cross-fade duration into the
    // combat-stance loop. 0 disables the beat (instant-fire).
    float combat_entry_delay_seconds = 0.18f;

    // ---- Attack combos ----
    // After a chain attack lands, players have this much wall-clock
    // time to press attack again (within the active clip's cancel
    // window) before the chain resets to step 0. Sized so a slow
    // press still extends the chain, but walking-away-then-attacking
    // clearly restarts at slash 1.
    float combo_reset_grace_seconds = 0.5f;
    // If the player presses attack BEFORE the current swing's cancel
    // window opens, that press is held in a buffer and auto-fires when
    // the window opens. Buffered presses expire after this many
    // wall-clock seconds — long enough that mashing players don't get
    // inputs eaten, short enough that a stale press doesn't trigger
    // an attack the player no longer wants.
    float combo_input_buffer_seconds = 0.20f;
    // Cross-fade duration between chain steps. The first attack of a
    // chain uses 0.10s (snappy first-strike); subsequent steps use
    // this longer value so the body has time to morph from one pose
    // to the next instead of visibly snapping. 0.20-0.25s is a
    // reasonable starting range; iteration may move it.
    float combo_chain_blend_seconds = 0.22f;
    // Playback-rate multiplier applied to attack one-shots. >1 plays
    // faster, scaling clip duration by 1/rate. Mixamo sword-and-shield
    // attacks were authored at a deliberate combat pace — fine for
    // demonstration but reads as slow in a snappier context.
    // Beyond ~1.6 the windup disappears and reads as a teleport-swing.
    float attack_playback_rate = 1.3f;
    // Threshold (fraction of peak hand-velocity) at which the auto-
    // detected cancel window opens. Applied at clip-load time when
    // resolving each attack's cancel-open seconds.
    //   * 0.10 = full settle: hand has returned to stance and stopped
    //     moving. Pure pose-bridge inertialization with no
    //     velocity continuity.
    //   * 0.50 = mid-follow-through: hand is past contact and still
    //     decelerating but not at rest yet. The next swing's blend
    //     starts from a still-moving pose, producing visible
    //     velocity continuity and a less "stop-and-restart" feel.
    //   * 0.80 = near peak: cancel almost on contact. Very snappy,
    //     visibly cuts the swing short.
    // 0.50 is the practical sweet spot for Mixamo-style clips that
    // weren't authored with bookend matching: cancel mid-follow-through
    // (hand past contact, still in motion) so the chain-link splice
    // enters the next swing with continuous velocity rather than
    // restarting from rest. 0.10 = full settle, slower rhythm.
    // Above ~0.70 cancels too
    // close to contact and the swings visibly cut short.
    float cancel_open_velocity_fraction = 0.50f;
    // Threshold for a "perfect" chain press. The accuracy score is
    // 1.0 at dead-center of the rhythm window and 0.0 at the edges.
    // A press at >= this threshold is considered perfect. 0.85 = a
    // press within 7.5% of either edge of the window from center
    // (i.e., the middle 70% of the window). Tighten toward 1.0 for
    // a stricter "bullseye" feel; loosen toward 0.5 to make perfect
    // routine.
    float perfect_accuracy_threshold = 0.85f;

    // ---- Dodge ----
    // Playback-rate multiplier applied to the dodge/roll one-shot
    // clip. >1 = snappier, lighter roll feel; 1.0 = real-time.
    // The clip's authored hip motion drives world translation (root
    // motion); rate scales how fast the clip plays AND therefore how
    // quickly the character covers the clip's authored distance.
    float roll_playback_rate = 1.3f;
    float backstep_playback_rate = 1.3f;
    // How long Space can be held before it commits to "sprint" instead
    // of "dodge" (seconds). Tap shorter than this → dodge fires on
    // release. Hold longer than this → sprint state engages.
    float dodge_tap_window = 0.20f;
    // Max angular rate (rad/s) the player can re-aim their roll
    // mid-dodge by turning the camera. We re-derive direction from
    // the current camera-relative WASD intent and steer toward it
    // with this clamp. ~5 rad/s ≈ ~285°/s, which lets a 0.6s roll
    // bend ~170° at most — sharp enough to feel responsive, capped
    // enough that you can't pirouette out of a swing.
    float dodge_steer_rate = 5.0f;
};

// JSON serialization — generates to_json / from_json for nlohmann::json
// at namespace scope (NON_INTRUSIVE keeps the struct itself macro-free).
// "_WITH_DEFAULT" means missing keys in the input fall back to the struct's
// default-initialized value, so older tunables.json files don't break when
// new fields are added.
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(Tunables, turn_rate, mouse_sensitivity, pitch_min,
                                                pitch_max, follow_distance, follow_height,
                                                fov_degrees, anim_blend_seconds,
                                                combat_idle_grace_seconds,
                                                combat_entry_delay_seconds,
                                                combo_reset_grace_seconds,
                                                combo_input_buffer_seconds,
                                                combo_chain_blend_seconds, attack_playback_rate,
                                                cancel_open_velocity_fraction,
                                                perfect_accuracy_threshold, roll_playback_rate,
                                                backstep_playback_rate, dodge_tap_window,
                                                dodge_steer_rate);

// Single global instance. Both gameplay code and the procedural driver
// read from this; the ImGui panel edits it in place. Keep it global rather
// than threading it through every function — there's exactly one game.
Tunables& current();

// JSON I/O. Path is relative to the working directory (typically
// build/bin/, which has its own copy synced from the source tree). Loads
// silently fall back to defaults if the file is missing or malformed —
// the game must remain runnable on a fresh checkout.
bool loadFromFile(const std::string& path);

// Saves to a relative path. Returns true on success. Used by the "Save"
// button in the ImGui panel; not called automatically.
bool saveToFile(const std::string& path);

} // namespace selva::tuning
