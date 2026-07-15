#pragma once

#include "Growth.h"
#include "HudCanvas.h"
#include "Notebook.h"
#include "Observations.h"
#include "UIRenderer.h" // Color

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
    // Pen-on-paper sound for a NEW notebook entry (a new thought being written
    // down); plays in place of appear_sound when a fresh thought surfaces.
    std::string notebook_sound = "assets/audio/notebook_entry.ogg";
    float drop_in_secs = 0.22f;  // slide/fade-in duration
    float chars_per_sec = 42.0f; // typewriter reveal rate
    float fade_out_secs = 0.7f;  // fade-out on dismiss
    int blip_every = 2;          // play a blip every N revealed characters
};

// Set the fonts (larger body + smaller heading), feel config, and the HUD region
// rects (canvas fractions -- content renders into these fixed screen bands) once
// after they are loaded.
void init(FontHandle body_font, FontHandle heading_font, const Config& config,
          const hud::Regions& regions);

// Where a first-occurrence reading gets written down as it surfaces. The box is
// the one place that sees every line as it appears, so it is the recording
// chokepoint -- but it stays ignorant of satchel/clock: the GAME decides whether
// recording is enabled (notebook carried) and what day to stamp (watch carried, or
// 0 for undated) and hands both in.
struct RecordSink
{
    notebook::Record* record = nullptr; // where to append (null = don't record)
    bool enabled = false;               // notebook carried?
    int day = 0;                        // in-world day, or 0 if undated (no watch)
};

// Advance the box animation + typewriter at wall-clock rate; pulls the next Line
// from state.pending when idle (and re-surfaces an action menu when its result
// line has been read). Plays SFX at the right beats. Takes growth so a Menu can
// recompute its offered actions and act on them. Window dims size the fixed HUD
// region a loading line wraps to. `sink` records each NEW line as it surfaces (if
// enabled). The box never auto-dismisses.
void update(observations::State& state, const growth::GrowthState& growth, float dt, int windowW,
            int windowH, const RecordSink& sink);

// True while the box shows anything (a Line dropping in / typing / held, or a
// Menu). The game gates world input on this: no box -> Space observes; box up ->
// Space / W / S / back drive the box.
bool active();

// True while an action Menu is the current item (so the game routes W/S to it).
bool menuActive();

// True while a THOUGHT reading (not a plain observation / deed-result) is on
// screen; if so, out_color is its faculty hue. Drives the over-head thought bubble
// (head_marker) -- the bubble shows exactly while the thought is up, in its color.
bool activeThought(const growth::GrowthState& growth, Color& out_color);

// Enqueue the action menu for a spot (its offered deeds, shown after the reading
// lines are read). No-op if the spot has no offered actions.
void pushActionMenu(observations::State& state, const growth::GrowthState& growth,
                    const std::string& spot);

// The action menu is bound to being at the spot: call each frame with the
// currently-faced spot id (empty if none). If a menu is QUEUED (not yet open) for
// a different spot, it is dropped -- the menu never chases you after you walk
// away. The observation's reading + fired thoughts still play out.
void dropQueuedMenuIfLeft(const std::string& facedSpot);

// What a confirm did that the game must enact: EXP to bank, plus any item effects a
// taken deed declared (ids only -- the box, like observations, is inventory-ignorant; the
// GAME owns the satchel/loot and does the grant). Empty grants for a plain reading confirm.
struct ConfirmResult
{
    int earned = 0;                    // Spirit EXP from a thought the deed fired
    std::vector<std::string> granted;  // item ids a "pick up" deed granted
    std::vector<std::string> gathered; // loot table ids a "gather" deed rolled
};

// Input while the box is up:
//   confirm (Space) -- Line: reveal-all then dismiss; Menu: take the selected deed
//   moveUp/moveDown (W/S) -- Menu: move the selection
//   back (F / RMB) -- Menu: close it
// The RNG is threaded so taking a deed can run the ambient engine (deed -> flag
// -> a missed thought may fire). Returns what the caller must enact (see ConfirmResult).
ConfirmResult confirm(observations::State& state, const growth::GrowthState& growth,
                      const observations::RollRng& rng);
void moveUp();
void moveDown();
void back();

// Mouse over the action menu: hovering an option highlights it; `clicked` (a
// left press this frame) confirms the hovered option. No-op unless a menu is up.
// Mirrors the pause page's mouse handling; interchangeable with W/S + Space.
// Returns what the caller must enact (like confirm).
ConfirmResult menuMouse(observations::State& state, const growth::GrowthState& growth,
                        const observations::RollRng& rng, float mx, float my, bool clicked);

// Draw the box (if active), tinted via `growth`. Content renders into its fixed
// HUD region (thought vs observation, by the item's kind). Window-space.
void render(const growth::GrowthState& growth, int windowW, int windowH);

} // namespace thought_box
