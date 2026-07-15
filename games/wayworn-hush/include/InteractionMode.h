#pragma once

#include "UIRenderer.h" // Color, FontHandle

#include <string>

// The player's interaction STANCE, surfaced so they always know which verb an interact will
// do. It's not a separate mode to toggle -- it IS the movement state: WALKING = Observe (slow
// down to notice), RUNNING (Shift) = Act (move with intent to do). A small persistent HUD
// badge shows the current stance; a distinct SFX plays on each transition (enter Act / return
// to Observe) so the change is unambiguous by ear. See docs/design/GAME-SYSTEMS.md.
namespace interaction_mode
{

// Feel/placement (config over constants). Tunable; edit config/interaction_mode.json.
struct Config
{
    // Badge placement: fractions of the window (top-left origin) + pixel size at 720p,
    // scaled by the HUD canvas scale so it holds across resolutions.
    float x = 0.02f;    // left, window-width fraction
    float y = 0.92f;    // top, window-height fraction (bottom-left corner)
    float pad = 0.008f; // badge inner padding, CANVAS FRACTION (x hud::scale -> px)
    // Labels + colors per stance.
    std::string observe_label = "Observe";
    std::string act_label = "Act";
    Color observe_color{0.62f, 0.80f, 0.86f, 1.0f}; // cool/quiet -- noticing
    Color act_color{0.92f, 0.74f, 0.46f, 1.0f};     // warm/firm -- doing
    Color panel{0.10f, 0.11f, 0.13f, 0.72f};        // badge backing
    // Transition SFX (placeholders until final cues are authored). enter_act on Shift-down,
    // enter_observe on Shift-up.
    std::string enter_act_sound = "assets/audio/observe.ogg";
    std::string enter_observe_sound = "assets/audio/observe.ogg";
    float sfx_volume = 0.6f;
};

// Runtime state: the last-seen stance, so update() can edge-detect a transition and play the
// right SFX exactly once per change.
struct State
{
    bool acting = false; // true = running/Act stance last frame
    bool primed = false; // false until the first update (so the first frame plays no SFX)
};

// Load the feel from config/interaction_mode.json (silent no-op -> defaults if missing).
void load(Config& cfg, const std::string& path);

// Set the fonts once after they're loaded (shares the HUD body font).
void init(FontHandle badge_font);

// Per-frame: update the stance from whether the player is RUNNING (Shift held), playing the
// enter-Act / enter-Observe SFX on a transition. Call once per frame.
void update(State& state, const Config& cfg, bool running);

// Draw the current-stance badge (window space). `acting` is the live stance.
void render(const State& state, const Config& cfg, int windowW, int windowH);

} // namespace interaction_mode
