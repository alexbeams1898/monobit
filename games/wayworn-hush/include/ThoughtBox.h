#pragma once

#include "Growth.h"
#include "Observations.h"

#include <string>

using FontHandle = int;

// The HUD surface -- one animated game-feel panel that renders whatever the
// game's systems want to say or offer, in one consistent voice (EarthBound /
// Undertale register). It consumes a queue of HudItems, each of a `kind`:
//   Line -- a reading / thought / deed-result: drops in, types out with a blip,
//           holds, and waits for Space to advance (Souls-like manual read).
//   Menu -- a list of options (e.g. the deeds you can do at a spot after
//           observing): W/S move the selection, Space confirms, F/RMB backs out.
// Systems stay pure -- they enqueue text Lines via observations::State.pending
// and Menus via pushActionMenu(); the box owns look, animation, and input.
// See docs/design/OBSERVATION-SYSTEM.md + ACTIONS.md + AESTHETIC.md.
namespace thought_box
{

// Tunable feel values (config over constants), loaded from JSON. Sound paths are
// placeholders until the OST pass supplies final SFX.
struct Config
{
    std::string blip_sound = "assets/audio/observe.ogg";   // per-character typing tick
    std::string appear_sound = "assets/audio/observe.ogg"; // box drop-in
    float drop_in_secs = 0.22f;                            // slide/fade-in duration
    float chars_per_sec = 42.0f;                           // typewriter reveal rate
    float fade_out_secs = 0.7f;                            // fade-out on dismiss
    int blip_every = 2;           // play a blip every N revealed characters
    float max_width_frac = 0.62f; // body wraps within this fraction of the window
};

// Set the fonts (larger body + smaller heading) and feel config once after they
// are loaded.
void init(FontHandle body_font, FontHandle heading_font, const Config& config);

// Advance the box animation + typewriter at wall-clock rate; pulls the next Line
// from state.pending when idle (and re-surfaces an action menu when its result
// line has been read). Plays SFX at the right beats. Takes growth so a Menu can
// recompute its offered actions and act on them. The box never auto-dismisses.
void update(observations::State& state, const growth::GrowthState& growth, float dt);

// True while the box shows anything (a Line dropping in / typing / held, or a
// Menu). The game gates world input on this: no box -> Space observes; box up ->
// Space / W / S / back drive the box.
bool active();

// True while an action Menu is the current item (so the game routes W/S to it).
bool menuActive();

// Enqueue the action menu for a spot (its offered deeds, shown after the reading
// lines are read). No-op if the spot has no offered actions.
void pushActionMenu(observations::State& state, const growth::GrowthState& growth,
                    const std::string& spot);

// The action menu is bound to being at the spot: call each frame with the
// currently-faced spot id (empty if none). If a menu is QUEUED (not yet open) for
// a different spot, it is dropped -- the menu never chases you after you walk
// away. The observation's reading + fired thoughts still play out.
void dropQueuedMenuIfLeft(const std::string& facedSpot);

// Input while the box is up:
//   confirm (Space) -- Line: reveal-all then dismiss; Menu: take the selected deed
//   moveUp/moveDown (W/S) -- Menu: move the selection
//   back (F / RMB) -- Menu: close it
// The RNG is threaded so taking a deed can run the ambient engine (deed -> flag
// -> a missed thought may fire).
// Returns Spirit EXP earned by this confirm (an action that fires a thought);
// 0 for a plain reading confirm. The caller banks it into growth.
int confirm(observations::State& state, const growth::GrowthState& growth,
            const observations::RollRng& rng);
void moveUp();
void moveDown();
void back();

// Mouse over the action menu: hovering an option highlights it; `clicked` (a
// left press this frame) confirms the hovered option. No-op unless a menu is up.
// Mirrors the pause page's mouse handling; interchangeable with W/S + Space.
// Returns EXP earned (like confirm) so the caller banks it.
int menuMouse(observations::State& state, const growth::GrowthState& growth,
              const observations::RollRng& rng, float mx, float my, bool clicked);

// Draw the box (if active), tinted via `growth`. Window-space, native resolution.
void render(const growth::GrowthState& growth, int windowW, int windowH);

// The box's top edge as a fraction of window height -- the single source of truth
// for where the HUD box sits, so other surfaces (e.g. notification toasts) anchor
// to it without a hand-synced copy of the constant.
float boxTopFrac();

} // namespace thought_box
