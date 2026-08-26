#pragma once

#include "Growth.h"
#include "HudCanvas.h"
#include "Notebook.h"
#include "Psyche.h"
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
// Systems stay pure -- they enqueue text Lines via psyche::State.pending
// and Menus via openDeedMenu(); the box owns look, animation, and input.
// See docs/design/PSYCHE.md + ACTIONS.md + AESTHETIC.md.
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

// Advance the box animation + typewriter at wall-clock rate; pulls the next Line
// from state.pending when idle (and re-surfaces an action menu when its result
// line has been read). Plays SFX at the right beats. Takes growth so a Menu can
// recompute its offered actions and act on them. Window dims size the fixed HUD
// region a loading line wraps to. The box never auto-dismisses.
//
// Purely a display. What a landed thought MEANS -- that it belongs in the notebook,
// on a particular day -- is the game's business, decided from
// psyche::ObserveResult::landed at the moment it fires.
void update(psyche::State& state, const growth::GrowthState& growth, float dt, int windowW,
            int windowH);

// True while the box shows anything (a Line dropping in / typing / held, or a
// Menu). The game gates world input on this: no box -> Space observes; box up ->
// Space / W / S / back drive the box.
bool active();

// True while an action Menu is the current item (so the game routes W/S to it).
bool menuActive();

// True when the box is fully at rest: nothing showing AND no menu re-open queued.
// Waiters (a scene's blocking box step) ask THIS, not active() -- the box's own
// lifecycle has one-frame gaps where nothing is on screen mid-exchange.
bool settled();

// True while a Line that earned Spirit is on screen (its "+N Spirit" toast lands
// with it). The tutorial pump reads this to teach the reward at the moment it is
// visible.
bool activeLineEarnedSpirit();

// The kind of the Line on screen, if one is (false when the box shows nothing or
// a menu). The tutorial pump maps this to its first-time events -- observation,
// impression, remark, thought are taught as distinct things.
bool activeLineKind(psyche::LineKind& out);

// True when the current item is COMPLETELY on screen: a line's typewriter has
// finished (this page fully revealed), or a menu is open and holding. The
// tutorial pump waits for this, so a card stops a finished moment -- never a
// half-rendered one.
bool fullyShown();

// True while a THOUGHT reading (not a plain observation / deed-result) is on
// screen; if so, out_color is its faculty hue. Drives the over-head thought bubble
// (head_marker) -- the bubble shows exactly while the thought is up, in its color.
bool activeThought(const growth::GrowthState& growth, Color& out_color);

// Run a spot's OBSERVE reading (the EarthBound "Check"): queues the reading lines; update()
// drains them. Returns the observation's whole result -- the EXP to bank, and any thoughts
// that landed for the caller to write down. The box passes it through untouched.
// `impression` marks the reading as pressed rather than chosen (a scene firing it
// at the player) -- see psyche::LineKind.
psyche::ObserveResult pushObserve(psyche::State& state, const growth::GrowthState& growth,
                                  const std::string& spot, const psyche::RollRng& rng,
                                  bool impression = false);

// Open a spot's deed menu (the RUNNING/Act stance). Always shows, even with no deeds (a
// "Leave"-only menu), so a run-interact is never a dead press. The menu re-opens after
// each deed's result line until a deed CONSUMES the spot (the moment has passed) or the
// player leaves. `must_choose` is a scene's decision beat: no Leave, no backing out --
// the deeds repeat until a consuming one is taken.
void openDeedMenu(const psyche::State& state, const growth::GrowthState& growth,
                  const std::string& spot, bool must_choose = false);

// What a confirm did that the game must enact: EXP to bank, plus any item effects a
// taken deed declared (ids only -- the box, like observations, is inventory-ignorant; the
// GAME owns the satchel and does the grant). Empty grants for a plain reading confirm.
struct ConfirmResult
{
    int earned = 0;                    // Spirit EXP from a thought the deed fired
    std::vector<std::string> granted;  // item ids a "pick up" deed granted
    std::vector<std::string> gathered; // yield table ids a "gather" deed rolled
    std::vector<std::string> taught;   // recipe ids a deed taught (the game marks them known)
    std::vector<std::string> landed;   // thought ids that landed (the game writes them down)
    std::vector<std::pair<std::string, int>> stat_gains; // faculty EXP a landed thought earned
    std::string consumed_spot;                           // observable id to despawn (consumes_spot)
    // True when a DEED was actually taken (vs a line dismissed / menu left) --
    // distinct from the effect fields, which can all be empty for a deed that
    // only speaks. Doing is what costs the hour (the participation clock).
    bool acted = false;
    // In-world minutes the deed declared (0 = none authored -> the game's
    // default deed cost applies).
    double minutes = 0.0;
};

// Input while the box is up:
//   confirm (Space) -- Line: reveal-all then dismiss; Menu: take the selected deed
//   moveUp/moveDown (W/S) -- Menu: move the selection
//   back (F / RMB) -- Menu: close it
// The RNG is threaded so taking a deed can run the ambient engine (deed -> flag
// -> a missed thought may fire). Returns what the caller must enact (see ConfirmResult).
ConfirmResult confirm(psyche::State& state, const growth::GrowthState& growth,
                      const psyche::RollRng& rng);
// Forget whatever is on screen: a line mid-typewriter, an open deed menu, a queued
// re-open. The box holds this between frames, so a walk that ends with a reading up would
// otherwise carry it into the next one. Call when a world goes away.
void reset();

void moveUp();
void moveDown();
void back();

// Mouse over the action menu: hovering an option highlights it; `clicked` (a
// left press this frame) confirms the hovered option. No-op unless a menu is up.
// Mirrors the pause page's mouse handling; interchangeable with W/S + Space.
// Returns what the caller must enact (like confirm).
ConfirmResult menuMouse(psyche::State& state, const growth::GrowthState& growth,
                        const psyche::RollRng& rng, float mx, float my, bool clicked);

// Draw the box (if active), tinted via `growth`. Content renders into its fixed
// HUD region (thought vs observation, by the item's kind). Window-space.
void render(const growth::GrowthState& growth, int windowW, int windowH);

} // namespace thought_box
