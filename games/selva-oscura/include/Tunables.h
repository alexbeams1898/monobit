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
    // ---- Debug ----
    // Global time-scale multiplier for the per-frame dt fed into
    // selvaPerFrame. 1.0 = normal speed. <1.0 = slow-mo (every
    // animation, lockout, timer, sampler advance scales down). >1.0 =
    // fast-forward. Lets split-second animation transitions unfold
    // over more wall-clock time so smoothness issues become visible.
    // Doesn't affect input edge timing or render rate; only the
    // simulation dt that drives time-advancing systems.
    float time_scale = 1.0f;

    // ---- Locomotion (velocity-driven) ----
    float turn_rate = 9.0f; // player rotation toward move dir, rad/s

    // Legacy SM debounce — UNUSED in the velocity-driven model
    // (velocity itself is naturally smoothed by accel/decel). Kept
    // for the JSON schema during the refactor; will be removed
    // once the SM is fully deleted.
    float wasd_debounce_seconds = 0.10f;

    // Target walking speed (m/s) when WASD is held without sprint.
    // Calibrated to match the walking clip's authored hip travel so
    // the foot doesn't visibly skate (clip moves character N meters
    // per cycle; walk_speed should match that cycle rate).
    float walk_speed = 1.6f;

    // Target running speed (m/s) when sprinting + WASD held.
    // Calibrated to match the running clip's authored hip travel.
    float run_speed = 4.5f;

    // Acceleration toward target speed (m/s²). Higher = snappier
    // response to WASD press. Too high reads as "teleport into
    // motion"; too low feels sluggish.
    float locomotion_accel = 18.0f;

    // Deceleration toward zero when WASD released (m/s²). Separate
    // from accel because the feel is different — Souls-style
    // games often have faster decel than accel for responsive
    // stops. Too high: instant snap to idle (the "shoots back to
    // idle" complaint); too low: slidey overshoot.
    float locomotion_decel = 14.0f;

    // Clip-speed blend thresholds. velocity_magnitude < idle_to_walk
    // = pure idle; > walk_to_run = pure running; in between =
    // weighted blend. Avoids hard mode boundaries — a single
    // walking step doesn't snap to walking, but holding WASD long
    // enough does as velocity ramps up.
    float idle_to_walk_speed = 0.30f;
    float walk_to_run_speed = 3.0f;

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
    // Delay (wall-clock seconds) between a Peaceful → Block press
    // (RMB) and the block one-shot actually firing. During this
    // window the locomotion track crossfades from standard_idle to
    // the combat-stance idle clip, so the player visibly settles
    // into a guard pose BEFORE the shield raise. Sized to roughly
    // match the cross-fade duration into the combat-stance loop.
    // 0 disables the beat (instant-fire).
    //
    // Attacks fire instantly regardless of this delay; the per-joint
    // inertialization decay handles the standard_idle → combat-idle
    // handoff for attacks. Only blocks honor this delay because their
    // hold-RMB semantics demand the establishing beat — you wouldn't
    // want RMB to read as "I tapped block while still standing
    // relaxed."
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
    // Blend-in for the FIRST attack out of combat-idle. Larger = more
    // forgiving when idle pose is far from the action's t=0.
    float first_strike_blend_seconds = 0.10f;
    // Per-joint inertialization decay scaling. Each joint's individual
    // decay window = base + scale * |offset|. Joints with small offsets
    // (a finger 5° off) keep snappy decays; joints with large offsets
    // (a leg 90° off in a stance ↔ dodge splice) get proportionally
    // longer windows so their visible motion reads as smooth, not as
    // a snap. Without this, a single global decay duration is forced
    // to compromise: too short = leg snap, too long = whole body
    // sluggish.
    float inertialize_decay_base_seconds = 0.10f;
    float inertialize_decay_scale_per_radian = 0.30f;
    // Hard cap so an extreme offset (180° flip) doesn't produce a 10s
    // decay window. Anything past this clamps.
    float inertialize_decay_max_seconds = 0.45f;
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
    // Cancel fractions are now declared per-profile in TransitionProfile
    // (profiles::dodge(), profiles::jump(), etc.) so every commit-then-
    // recover one-shot uses the same mechanism (see PoseSampler::
    // isOneShotPastCancelFraction). dodge_cancel_fraction and
    // dodge_attack_cancel_fraction were both 0.65 and have been folded
    // into profiles::dodge().cancel_fraction.

    // Wall-clock seconds past cancel_window_close at which walking
    // becomes responsive again post-attack. Anchoring the lockout to
    // cancel_window_close (instead of the full clip-end + grace)
    // lets the player resume walking while the swing's recovery tail
    // animates underneath via the one-shot blend-out. The clip's
    // visible motion finishes naturally; the player isn't locked
    // through it. ~0.10s is a small breathing room past the rhythm
    // window so chain advances aren't fighting walking input.
    float attack_lockout_extension_seconds = 0.10f;

    // ---- Sprint-finisher (running attack) ----
};

// JSON serialization — generates to_json / from_json for nlohmann::json
// at namespace scope (NON_INTRUSIVE keeps the struct itself macro-free).
// "_WITH_DEFAULT" means missing keys in the input fall back to the struct's
// default-initialized value, so older tunables.json files don't break when
// new fields are added.
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(
    Tunables, time_scale, turn_rate, wasd_debounce_seconds, walk_speed, run_speed, locomotion_accel,
    locomotion_decel, idle_to_walk_speed, walk_to_run_speed, mouse_sensitivity, pitch_min,
    pitch_max, follow_distance, follow_height, fov_degrees, anim_blend_seconds,
    combat_idle_grace_seconds, combat_entry_delay_seconds, combo_reset_grace_seconds,
    combo_input_buffer_seconds, combo_chain_blend_seconds, first_strike_blend_seconds,
    inertialize_decay_base_seconds, inertialize_decay_scale_per_radian,
    inertialize_decay_max_seconds, attack_playback_rate, cancel_open_velocity_fraction,
    perfect_accuracy_threshold, roll_playback_rate, backstep_playback_rate, dodge_tap_window,
    dodge_steer_rate, attack_lockout_extension_seconds);

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
