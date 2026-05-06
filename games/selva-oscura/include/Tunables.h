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
    float move_speed = 4.0f;        // walk speed, units/second
    float sprint_multiplier = 1.8f; // sprint = move_speed * this
    float turn_rate = 9.0f;         // player rotation toward move dir, rad/s

    // ---- Mouse-look ----
    float mouse_sensitivity = 0.0025f; // radians per pixel
    float pitch_min = -1.45f;          // ~-83 deg
    float pitch_max = 1.45f;           // ~+83 deg

    // ---- Camera follow ----
    float follow_distance = 6.0f;
    float follow_height = 2.5f;
    float fov_degrees = 60.0f;

    // ---- Dodge — gameplay timing (state-machine durations) ----
    float roll_duration = 0.55f;     // seconds, roll commit phase
    float backstep_duration = 0.35f; // seconds, backstep commit phase
    float dodge_recovery = 0.20f;    // seconds, post-dodge lockout

    // Input buffering window. When Space is pressed while the player is
    // already committed (Rolling/Backstep/Recovering), the input is held
    // for this many seconds and fires the moment Idle is reached. Direction
    // is sampled at fire time, not press time — so you can press Space
    // mid-roll and rotate the stick to dodge in a different direction.
    // Souls/ER use ~0.15s. 0 disables buffering (legacy behavior).
    float dodge_buffer_window = 0.15f;

    // ---- Dodge — animation curve shape (read by ProceduralDriver) ----
    //
    // Each sub-curve runs in its own [start, end] window of the overall
    // roll's [0, 1] phase. Curves OUTSIDE their window contribute nothing
    // to the output. This is how Souls / Elden Ring rolls layer hop,
    // rotation, and forward motion: each "track" has its own keyframes
    // that don't have to span the whole clip.
    //
    // Defaults give the canonical Souls feel: hop happens early and is
    // mostly done by mid-roll, rotation kicks in slightly later and runs
    // briskly through the airborne window, translation overlaps both.
    //
    // The hop uses a true gravity arc (4t(1-t) — projectile motion), so
    // its peak is fixed at the temporal midpoint of [hop_start, hop_end]
    // and there's no peak-phase knob; any deviation would be unphysical
    // and look like it does. Control airborne duration with the window
    // size, height with hop_height. tumble_ease > 1 accelerates rotation
    // through its window (1 = linear, 2 = S-curve, 3 = aggressive).
    float roll_distance = 3.5f;          // total world units traveled in a roll
    float roll_hop_height = 0.45f;       // peak Y offset at apex
    float roll_hop_start = 0.0f;         // phase at which the hop begins
    float roll_hop_end = 0.65f;          // phase at which the hop has fully landed
    float roll_tumble_revs = 1.0f;       // visual revolutions over the tumble window
    float roll_tumble_ease = 2.0f;       // curve power (>1 = ease-in-out)
    float roll_tumble_start = 0.15f;     // phase at which the tumble starts
    float roll_tumble_end = 0.85f;       // phase at which the tumble completes
    float roll_translation_start = 0.0f; // phase at which forward motion begins
    float roll_translation_end = 0.85f;  // phase at which forward motion settles

    // Pre-tumble lean — separate forward-pitch posture that runs alongside
    // the hop. Composes additively with the tumble's pitch_offset, so the
    // character tips into the roll *before* full rotation kicks in. Animator's
    // "anticipation/follow-through overlap": posture starts orienting to
    // the next state during the current state.
    //   - lean_angle: peak forward pitch in radians (0.4 ≈ 23°). 0 disables.
    //   - lean_start/end: phase window for the lean parabola.
    //   - lean_peak_phase: peak position WITHIN the lean window [0,1].
    // Defaults aim to peak the lean alongside the hop apex so they read
    // as one fused "tucking into the airborne moment" gesture.
    float roll_lean_angle = 0.4f;       // radians; forward = positive (same axis as tumble)
    float roll_lean_start = 0.0f;       // phase at which the lean begins
    float roll_lean_end = 0.55f;        // phase at which the body unfolds back to upright
    float roll_lean_peak_phase = 0.45f; // peak position WITHIN the lean window [0,1]

    float backstep_distance = 2.0f; // total world units in a backstep
};

// JSON serialization — generates to_json / from_json for nlohmann::json
// at namespace scope (NON_INTRUSIVE keeps the struct itself macro-free).
// "_WITH_DEFAULT" means missing keys in the input fall back to the struct's
// default-initialized value, so older tunables.json files don't break when
// new fields are added.
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(
    Tunables, move_speed, sprint_multiplier, turn_rate, mouse_sensitivity, pitch_min, pitch_max,
    follow_distance, follow_height, fov_degrees, roll_duration, backstep_duration, dodge_recovery,
    dodge_buffer_window, roll_distance, roll_hop_height, roll_hop_start, roll_hop_end,
    roll_tumble_revs, roll_tumble_ease, roll_tumble_start, roll_tumble_end, roll_translation_start,
    roll_translation_end, roll_lean_angle, roll_lean_start, roll_lean_end, roll_lean_peak_phase,
    backstep_distance);

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
