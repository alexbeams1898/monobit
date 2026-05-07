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

    // ---- Animation transitions ----
    // Cross-fade duration (seconds) between two clips when the active
    // selection changes (Idle ↔ Walk ↔ Run). 0 disables the fade and
    // snaps instantly. ~0.15-0.25s reads as "smooth" without making
    // the transition feel laggy.
    float anim_blend_seconds = 0.20f;
};

// JSON serialization — generates to_json / from_json for nlohmann::json
// at namespace scope (NON_INTRUSIVE keeps the struct itself macro-free).
// "_WITH_DEFAULT" means missing keys in the input fall back to the struct's
// default-initialized value, so older tunables.json files don't break when
// new fields are added.
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(Tunables, move_speed, sprint_multiplier, turn_rate,
                                                mouse_sensitivity, pitch_min, pitch_max,
                                                follow_distance, follow_height, fov_degrees,
                                                anim_blend_seconds);

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
