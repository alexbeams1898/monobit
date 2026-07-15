#pragma once

#include "Crafting.h"
#include "GameLoop.h"
#include "Growth.h"
#include "Observations.h"

#include <string>
#include <vector>

using FontHandle = int;

// The pause page -- the game's on-demand record (the self + what's been
// noticed) plus a system tab, opened with F. No persistent HUD; this screen is
// where standing information lives (see docs/design/GAME-SYSTEMS.md). Drawn in
// wayworn's minimal register: a soft overlay, shadow-text, muted tab chrome.
namespace pause_page
{

// An action a frame of input committed.
enum class Action
{
    None,   // nothing this frame
    Resume, // close the page, unfreeze the world
    Quit,   // exit to desktop (no save system yet -- a clean exit)
    Craft   // the player confirmed a craft attempt (pause.craft_selected); the caller runs it
};

// Set the font once after FontManager loads it.
void init(FontHandle font);

// The Craft tab's material rows: the distinct Practical item ids the pilgrim carries, in the
// order the Craft tab draws + navigates them. The caller passes this to step() so the craft
// cursor lines up with the rendered rows.
std::vector<std::string> craftMaterials(const inventory::Satchel& satchel,
                                        const inventory::Registry& items);

// One frame of input while the page may be open. Controls stay in the left-hand
// WASD cluster (no Esc); edge-triggered inputs decoded by the caller.
//   toggle    = F       -- universal back/no: opens when closed; backs out of a
//                          sub-view to the tabs; else closes the page
//   left/right= A/D     -- previous / next tab
//   up/down   = W/S     -- move the selected item within the System tab
//   confirm   = Space   -- universal yes/interact: commits the selected item
//                          (System -> Controls opens its view; Quit quits)
// Space and F are the game's global yes/no; the world is frozen while the page
// is open, so they mean confirm/back here without conflict. `craftMats` is the Craft tab's
// material-row list (the caller builds it from the satchel; empty for other tabs) so the
// craft cursor + toggle line up with what's drawn. Mutates the PauseState and returns the
// action committed this frame (Quit / Craft are returned for the caller to act on). Pure --
// testable without SDL or GL.
Action step(PauseState& pause, bool toggle, bool left, bool right, bool up, bool down, bool confirm,
            const std::vector<std::string>& craftMats = {});

// Mouse state for one frame, decoded by the caller (position from SDL,
// `clicked` = a left-button press this frame). Passed in so the page module
// stays free of engine/input types.
struct Mouse
{
    float x = 0.0f;
    float y = 0.0f;
    bool clicked = false;
};

// The data the page's read-only tabs display, bundled so the render signature
// stays small as tabs are added: what's noticed (observations), what's carried
// (satchel + item defs for names/rarity), and the dated readings record (notebook).
struct Content
{
    const observations::State& observations;
    const inventory::Satchel& satchel;
    const inventory::Registry& items;
    const notebook::Record& notebook;
    const crafting::Registry& recipes;     // for the Craft tab (which recipes are realized)
    const crafting::State& crafting_state; // discovery state (known recipes)
};

// Draw the page (overlay + tab strip + content) and handle the mouse against the
// geometry it draws: hovering a tab brightens it, clicking a tab switches to it,
// clicking Quit on the System tab quits. Mutates `pause` (a tab click switches
// tabs) and returns any action a click committed (Quit). Keyboard is handled
// separately by step(); the two are interchangeable. No-op if the page is
// closed. Window-space, native resolution. GL/font -- integration-tested by
// running the game.
Action render(PauseState& pause, const growth::GrowthState& growth, const Content& content,
              const Mouse& mouse, int windowW, int windowH);

} // namespace pause_page
