#pragma once

#include "Crafting.h"
#include "GameLoop.h"
#include "Growth.h"
#include "Observations.h"

#include <cstdint>
#include <functional>
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
    Leave,  // back to the title (the walk is saved on the way out)
    Quit,   // exit to desktop (the walk is saved on the way out)
    Craft   // the player confirmed a craft attempt (pause.craft_selected); the caller runs it
};

// Set the font once after FontManager loads it.
void init(FontHandle font);

// One Craft-tab material row: an item id + how many of it the pilgrim carries. The carried count
// lets the pure step() cap how many can be thrown in the pot without needing the whole satchel.
struct CraftMaterial
{
    std::string id;
    int carried = 0;
};

// The Craft tab's material rows: each throwable item the pilgrim carries (everything but key
// items) + its carried count, in the order the Craft tab draws + navigates them. The caller
// passes this to step() so the craft cursor lines up with the rendered rows.
std::vector<CraftMaterial> craftMaterials(const inventory::Satchel& satchel,
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
            const std::vector<CraftMaterial>& craftMats = {});

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

// Resolve an item's icon path to a GL texture id (the caller wraps the engine's
// TextureManager, so the page stays free of engine types). Returns 0 if unavailable -> the
// icon cell falls back to a plain rarity-tinted swatch.
using IconResolver = std::function<std::uint32_t(const std::string&)>;

// Draw the page (overlay + tab strip + content) and handle the mouse against the
// geometry it draws: hovering a tab brightens it, clicking a tab switches to it,
// clicking Quit on the System tab quits. `icon` resolves item icons to textures (the
// Satchel/Craft grids draw them). Mutates `pause` and returns any action a click committed
// (Quit). Keyboard is handled by step(). No-op if closed. Window-space, native resolution.
Action render(PauseState& pause, const growth::GrowthState& growth, const Content& content,
              const Mouse& mouse, const IconResolver& icon, int windowW, int windowH);

} // namespace pause_page
