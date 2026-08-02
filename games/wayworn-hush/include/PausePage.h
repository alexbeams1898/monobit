#pragma once

#include "Arcs.h"
#include "Crafting.h"
#include "GameLoop.h"
#include "Growth.h"
#include "Psyche.h"

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

using FontHandle = int;

// The pause page -- the game's on-demand record (the self + what he's come to
// understand) plus a system tab, opened with F. Where the STANDING record lives: the
// HUD carries what is true at a glance, this carries what takes reading (see
// docs/design/HUD.md). Drawn in wayworn's minimal register: a soft overlay,
// shadow-text, muted tab chrome.
namespace pause_page
{

// An action a frame of input committed.
enum class Action
{
    None,     // nothing this frame
    Resume,   // close the page, unfreeze the world
    Settings, // open the settings screen (the caller owns the phase; it returns here)
    Leave,    // back to the title (the walk is saved on the way out)
    Quit,     // exit to desktop (the walk is saved on the way out)
    Craft,    // the player confirmed a craft attempt (pause.craft_selected); the caller runs it
    Hold      // take up / put down the cursored Satchel item (the caller owns the satchel)
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

// Which tabs the page currently HAS. A faculty the pilgrim has no instrument for is not a
// dimmed tab, it is no tab -- the same rule the notebook follows (an instrument you carry is
// what makes the doing possible). ONE answer, read by both the keyboard's paging and the
// strip's drawing, so what A/D reaches and what the strip shows cannot disagree.
struct Tabs
{
    bool craft = true; // Mr Richards' kit, or whatever the world says opens making

    bool has(PauseState::Tab t) const
    {
        return t != PauseState::Tab::Craft || craft;
    }
};

// The Satchel's rows, as item ids in the order the tab draws and navigates them: tools first
// (what he acts with), then key items, keepsakes, materials. Public because the CALLER resolves
// the cursor against it when enacting Action::Hold -- one ordering, so the row the player is
// looking at is the row that gets taken up.
std::vector<std::string> satchelOrder(const inventory::Satchel& satchel,
                                      const inventory::Registry& items);

// One frame of input while the page may be open. Controls stay in the left-hand WASD cluster;
// edge-triggered inputs decoded by the caller. Bundled because six loose bools at a call site
// say nothing about which is which -- `Keys{.confirm = true}` reads, `false, false, true` does
// not. Space and F are the game's global yes/no; the world is frozen while the page is open, so
// they mean confirm/back here without conflict.
struct Keys
{
    bool toggle = false; // F / Esc / RMB -- opens when closed; backs out of a sub-view; else closes
    bool left = false;   // A -- previous tab
    bool right = false;  // D -- next tab
    bool up = false;     // W -- move the cursor within the showing tab
    bool down = false;   // S
    bool confirm = false; // Space -- commit the selected item
};

// Step the page from one frame of input. `craftMats` is the Craft tab's material-row list (the
// caller builds it from the satchel; empty for other tabs) so the craft cursor lines up with
// what's drawn. Mutates the PauseState and returns the action committed this frame (Quit /
// Craft are returned for the caller to act on). Pure -- testable without SDL or GL.
Action step(PauseState& pause, const Tabs& tabs, const Keys& keys,
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
// stays small as tabs are added: what's been observed and thought (psyche),
// what's carried (satchel + item defs for names/rarity), and when each thought was
// written down (notebook -- the thoughts themselves come from psyche).
struct Content
{
    const psyche::State& psyche;
    const inventory::Satchel& satchel;
    const inventory::Registry& items;
    const notebook::Record& notebook;
    const worldclock::WorldClock& clock;   // resolves a note's moment to its day + dateline
    const crafting::Registry& recipes;     // for the Craft tab (which recipes are realized)
    const crafting::State& crafting_state; // discovery state (known recipes)
    // What he owes, resolved by the caller against the world's flags and the hour. Sits at the
    // head of today's page in the Notebook -- the same book, a different kind of writing.
    std::vector<arcs::Item> agenda;
};

// Resolve an item's icon path to a GL texture id (the caller wraps the engine's
// TextureManager, so the page stays free of engine types). Returns 0 if unavailable -> the
// icon cell falls back to a plain rarity-tinted swatch.
// An icon image, resolved: the texture and its pixel size. The size is what lets
// a shared sheet's CELL be drawn (items live on one sheet like the rest of the
// game's art -- see ItemDef::iconUv); 0 = unavailable, draw the rarity swatch.
struct IconImage
{
    std::uint32_t tex = 0;
    int w = 0;
    int h = 0;
};
using IconResolver = std::function<IconImage(const std::string&)>;

// Draw the page (overlay + tab strip + content) and handle the mouse against the
// geometry it draws: hovering a tab brightens it, clicking a tab switches to it,
// clicking Quit on the System tab quits. `icon` resolves item icons to textures (the
// Satchel/Craft grids draw them). Mutates `pause` and returns any action a click committed
// (Quit). Keyboard is handled by step(). No-op if closed. Window-space, native resolution.
Action render(PauseState& pause, const Tabs& tabs, const growth::GrowthState& growth,
              const Content& content, const Mouse& mouse, const IconResolver& icon, int windowW,
              int windowH);

} // namespace pause_page
