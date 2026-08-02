#include "GameLoop.h"
#include "PausePage.h"

#include <catch2/catch_test_macros.hpp>

using pause_page::Action;

// step() is the pure input->state transition for the pause page (no SDL/GL).
// Controls: F = toggle/back, A/D = tabs, W/S = item select, Space = confirm.

namespace
{
// Every tab present unless a test says otherwise -- the pilgrim with his kit on him.
const pause_page::Tabs kAllTabs{};

PauseState opened()
{
    PauseState p;
    pause_page::step(p, kAllTabs, pause_page::Keys{.toggle = true});
    return p;
}

// Open and page to the System tab (Self -> Noticed -> Satchel -> Notebook ->
// System). System is the last tab; a single left from Self reaches it faster, but
// the tests exercise the forward path explicitly.
PauseState onSystem()
{
    PauseState p = opened();
    pause_page::step(p, kAllTabs, pause_page::Keys{.left = true}); // wrap back to System
    return p;
}
} // namespace

TEST_CASE("F opens the page from closed, on the Self tab, no sub-view", "[pause]")
{
    PauseState p;
    REQUIRE_FALSE(p.open);
    const Action a = pause_page::step(p, kAllTabs, pause_page::Keys{.toggle = true});
    REQUIRE(a == Action::None);
    REQUIRE(p.open);
    REQUIRE(p.tab == PauseState::Tab::Self);
    REQUIRE(p.view_stack.empty());
}

TEST_CASE("F while on the tabs closes the page", "[pause]")
{
    PauseState p = opened();
    const Action a = pause_page::step(p, kAllTabs, pause_page::Keys{.toggle = true});
    REQUIRE(a == Action::Resume);
    REQUIRE_FALSE(p.open);
}

TEST_CASE("A/D page through the tabs, wrapping", "[pause]")
{
    PauseState p = opened(); // Self
    pause_page::step(p, kAllTabs, pause_page::Keys{.right = true});
    REQUIRE(p.tab == PauseState::Tab::Satchel);
    pause_page::step(p, kAllTabs, pause_page::Keys{.right = true});
    REQUIRE(p.tab == PauseState::Tab::Craft);
    pause_page::step(p, kAllTabs, pause_page::Keys{.right = true});
    REQUIRE(p.tab == PauseState::Tab::Notebook);
    pause_page::step(p, kAllTabs, pause_page::Keys{.right = true});
    REQUIRE(p.tab == PauseState::Tab::System);
    pause_page::step(p, kAllTabs, pause_page::Keys{.right = true});
    REQUIRE(p.tab == PauseState::Tab::Self); // wrapped
    pause_page::step(p, kAllTabs, pause_page::Keys{.left = true});
    REQUIRE(p.tab == PauseState::Tab::System); // wrapped back
}

TEST_CASE("System tab: nothing selected until W/S; then move/wrap", "[pause]")
{
    // The menu is Controls / Leave to title / Quit to desktop.
    PauseState p = onSystem();
    REQUIRE(p.system_sel == -1); // nothing highlighted on arrival
    pause_page::step(p, kAllTabs, pause_page::Keys{.down = true});
    REQUIRE(p.system_sel == 0); // down from none -> first item (Controls)
    pause_page::step(p, kAllTabs, pause_page::Keys{.down = true});
    REQUIRE(p.system_sel == 1); // Settings
    pause_page::step(p, kAllTabs, pause_page::Keys{.down = true});
    REQUIRE(p.system_sel == 2); // Leave to title
    pause_page::step(p, kAllTabs, pause_page::Keys{.down = true});
    REQUIRE(p.system_sel == 3); // Quit to desktop
    pause_page::step(p, kAllTabs, pause_page::Keys{.down = true});
    REQUIRE(p.system_sel == 0); // wraps to first
    pause_page::step(p, kAllTabs, pause_page::Keys{.up = true});
    REQUIRE(p.system_sel == 3); // wraps to last
}

TEST_CASE("System tab: up from none picks the last item", "[pause]")
{
    PauseState p = onSystem();
    REQUIRE(p.system_sel == -1);
    pause_page::step(p, kAllTabs, pause_page::Keys{.up = true});
    REQUIRE(p.system_sel == 3); // up from none -> last item (Quit to desktop)
}

TEST_CASE("System tab: confirm with nothing selected does nothing", "[pause]")
{
    PauseState p = onSystem(); // system_sel == -1
    const Action a = pause_page::step(p, kAllTabs, pause_page::Keys{.confirm = true});
    REQUIRE(a == Action::None);
    REQUIRE(p.view_stack.empty());
    REQUIRE(p.open);
}

TEST_CASE("System tab: confirm on Controls pushes the Controls sub-view", "[pause]")
{
    PauseState p = onSystem();
    pause_page::step(p, kAllTabs, pause_page::Keys{.down = true}); // select Controls
    const Action a = pause_page::step(p, kAllTabs, pause_page::Keys{.confirm = true});
    REQUIRE(a == Action::None);
    REQUIRE(p.view_stack.size() == 1);
    REQUIRE(p.view_stack.back() == PauseState::View::Controls);
    REQUIRE(p.open);
}

TEST_CASE("F in the Controls sub-view pops to the tabs, not closing the page", "[pause]")
{
    PauseState p = onSystem();
    pause_page::step(p, kAllTabs, pause_page::Keys{.down = true});    // select Controls
    pause_page::step(p, kAllTabs, pause_page::Keys{.confirm = true}); // push Controls
    REQUIRE(p.view_stack.size() == 1);
    const Action a = pause_page::step(p, kAllTabs, pause_page::Keys{.toggle = true});
    REQUIRE(a == Action::None);
    REQUIRE(p.view_stack.empty()); // popped back to tabs
    REQUIRE(p.open);               // page still open
}

TEST_CASE("System tab: confirm on Leave to title returns Leave", "[pause]")
{
    // Leaving to the title and leaving the game are different acts -- an earlier version
    // routed every non-Controls item to Quit, which would have quit the game here.
    PauseState p = onSystem();
    pause_page::step(p, kAllTabs, pause_page::Keys{.down = true}); // Controls
    pause_page::step(p, kAllTabs, pause_page::Keys{.down = true}); // Settings
    pause_page::step(p, kAllTabs, pause_page::Keys{.down = true}); // Leave to title
    REQUIRE(p.system_sel == 2);
    const Action a = pause_page::step(p, kAllTabs, pause_page::Keys{.confirm = true});
    REQUIRE(a == Action::Leave);
    REQUIRE(p.open); // the caller acts on it
}

TEST_CASE("System tab: confirm on Settings returns Settings, leaving the page open", "[pause]")
{
    // Settings is a PHASE, not a sub-view: the page reports it and the caller owns the
    // move. The page stays open so backing out of settings returns here, not to a world
    // that quietly unpaused.
    PauseState p = onSystem();
    pause_page::step(p, kAllTabs, pause_page::Keys{.down = true}); // Controls
    pause_page::step(p, kAllTabs, pause_page::Keys{.down = true}); // Settings
    REQUIRE(p.system_sel == 1);
    const Action a = pause_page::step(p, kAllTabs, pause_page::Keys{.confirm = true});
    REQUIRE(a == Action::Settings);
    REQUIRE(p.open);
    REQUIRE(p.view_stack.empty()); // NOT pushed as a sub-view
}

TEST_CASE("System tab: confirm on Quit to desktop returns Quit", "[pause]")
{
    PauseState p = onSystem();
    pause_page::step(p, kAllTabs, pause_page::Keys{.up = true}); // up from none -> last
    REQUIRE(p.system_sel == 3);
    const Action a = pause_page::step(p, kAllTabs, pause_page::Keys{.confirm = true});
    REQUIRE(a == Action::Quit);
    REQUIRE(p.open); // caller acts on Quit
}

TEST_CASE("Confirm on a read-only tab does nothing", "[pause]")
{
    PauseState p = opened(); // Self -- a plain readout, nothing to commit
    REQUIRE(pause_page::step(p, kAllTabs, pause_page::Keys{.confirm = true}) == Action::None);

    pause_page::step(p, kAllTabs, pause_page::Keys{.right = true}); // Satchel
    pause_page::step(p, kAllTabs, pause_page::Keys{.right = true}); // Craft
    pause_page::step(p, kAllTabs, pause_page::Keys{.right = true}); // Notebook
    REQUIRE(p.tab == PauseState::Tab::Notebook);
    REQUIRE(pause_page::step(p, kAllTabs, pause_page::Keys{.confirm = true}) == Action::None);
    REQUIRE(p.open);
}

TEST_CASE("Confirm on the Satchel offers to take the cursored thing up", "[pause]")
{
    // The page does not know which rows are tools -- it offers the gesture on every row and
    // the satchel refuses what cannot be held, so there is one rule and it lives in one place.
    PauseState p = opened();
    pause_page::step(p, kAllTabs, pause_page::Keys{.right = true}); // Satchel
    REQUIRE(p.tab == PauseState::Tab::Satchel);
    REQUIRE(pause_page::step(p, kAllTabs, pause_page::Keys{.confirm = true}) == Action::Hold);
}

TEST_CASE("Input while closed (no toggle) does nothing", "[pause]")
{
    PauseState p;
    const Action a = pause_page::step(p, kAllTabs, pause_page::Keys{.left = true, .confirm = true});
    REQUIRE(a == Action::None);
    REQUIRE_FALSE(p.open);
}

TEST_CASE("Craft tab: Space stacks a material into the pot; Combine returns Craft", "[pause]")
{
    using pause_page::CraftMaterial;
    // Straight to the Craft tab -- how it's REACHED is the tab-paging test's business
    // (above); counting D presses here would break this test whenever a tab is added.
    PauseState p = opened();
    p.tab = PauseState::Tab::Craft;

    // Carry 3 thyme + 1 water; a trailing Combine row. Cursor starts on row 0 (thyme).
    const std::vector<CraftMaterial> mats = {{"thyme", 3}, {"water", 1}};

    // Space throws one thyme in; each Space adds one more, up to how many are carried.
    pause_page::step(p, kAllTabs, pause_page::Keys{.confirm = true}, mats);
    REQUIRE(p.craft_selected["thyme"] == 1);
    pause_page::step(p, kAllTabs, pause_page::Keys{.confirm = true}, mats);
    pause_page::step(p, kAllTabs, pause_page::Keys{.confirm = true}, mats);
    REQUIRE(p.craft_selected["thyme"] == 3); // capped at the 3 carried

    // One more Space with the whole stack already in resets that material to none.
    pause_page::step(p, kAllTabs, pause_page::Keys{.confirm = true}, mats);
    REQUIRE(p.craft_selected.count("thyme") == 0);

    // W/S move over the rows; step down to the Combine row (index == mats.size()).
    pause_page::step(p, kAllTabs, pause_page::Keys{.confirm = true}, mats); // thyme x1
    pause_page::step(p, kAllTabs, pause_page::Keys{.down = true}, mats);    // -> water
    pause_page::step(p, kAllTabs, pause_page::Keys{.down = true}, mats);    // -> Combine
    REQUIRE(p.craft_sel == static_cast<int>(mats.size()));

    // Space on Combine returns Craft for the caller to enact.
    const Action a = pause_page::step(p, kAllTabs, pause_page::Keys{.confirm = true}, mats);
    REQUIRE(a == Action::Craft);
}

TEST_CASE("Satchel tab: W/S move the detail cursor", "[pause]")
{
    PauseState p = opened();
    p.tab = PauseState::Tab::Satchel;

    // S advances the cursor, W steps it back. (Clamping to the item count happens at render
    // time against the live satchel; step only nudges. Mouse hover selection lives in render(),
    // which needs a font/GL context -- integration-tested by running the game.)
    REQUIRE(p.satchel_sel == 0);
    pause_page::step(p, kAllTabs, pause_page::Keys{.down = true});
    REQUIRE(p.satchel_sel == 1);
    pause_page::step(p, kAllTabs, pause_page::Keys{.up = true});
    REQUIRE(p.satchel_sel == 0);
}

TEST_CASE("Notebook tab: W/S move the detail cursor", "[pause]")
{
    PauseState p = opened();
    p.tab = PauseState::Tab::Notebook;

    // Same read-only cursor the Satchel has: step nudges, render clamps to the row count.
    REQUIRE(p.notebook_sel == 0);
    pause_page::step(p, kAllTabs, pause_page::Keys{.down = true});
    REQUIRE(p.notebook_sel == 1);
    pause_page::step(p, kAllTabs, pause_page::Keys{.up = true});
    REQUIRE(p.notebook_sel == 0);
}

TEST_CASE("W/S on a tab with no cursor does nothing", "[pause]")
{
    // Self is a plain readout -- W/S must not quietly drive another tab's cursor.
    PauseState p = opened();
    pause_page::step(p, kAllTabs, pause_page::Keys{.down = true});
    REQUIRE(p.satchel_sel == 0);
    REQUIRE(p.notebook_sel == 0);
}

// --- Tabs the pilgrim has no instrument for ------------------------------------------

TEST_CASE("without the kit there is no Craft tab to page onto", "[pause]")
{
    // MAKING IS THE KIT. A faculty he has no instrument for is not a dimmed tab, it is no tab
    // -- so A/D must step OVER it rather than landing on a page nobody can see.
    const pause_page::Tabs noKit{/*craft=*/false};
    PauseState p;
    pause_page::step(p, noKit, pause_page::Keys{.toggle = true});
    REQUIRE(p.tab == PauseState::Tab::Self);

    pause_page::step(p, noKit, pause_page::Keys{.right = true});
    REQUIRE(p.tab == PauseState::Tab::Satchel);
    pause_page::step(p, noKit, pause_page::Keys{.right = true});
    REQUIRE(p.tab == PauseState::Tab::Notebook); // Craft skipped
    pause_page::step(p, noKit, pause_page::Keys{.right = true});
    REQUIRE(p.tab == PauseState::Tab::System);
}

TEST_CASE("paging backwards skips the absent tab too", "[pause]")
{
    const pause_page::Tabs noKit{/*craft=*/false};
    PauseState p;
    pause_page::step(p, noKit, pause_page::Keys{.toggle = true});
    pause_page::step(p, noKit, pause_page::Keys{.right = true}); // Satchel
    pause_page::step(p, noKit, pause_page::Keys{.right = true}); // Notebook
    pause_page::step(p, noKit, pause_page::Keys{.left = true});
    REQUIRE(p.tab == PauseState::Tab::Satchel); // not Craft
}

TEST_CASE("a tab that vanishes underfoot puts the page back on Self", "[pause]")
{
    // He hands the kit back while the Craft page is open: the keys must not go on walking a
    // list nobody can see.
    PauseState p = opened();
    pause_page::step(p, kAllTabs, pause_page::Keys{.right = true}); // Satchel
    pause_page::step(p, kAllTabs, pause_page::Keys{.right = true}); // Craft
    REQUIRE(p.tab == PauseState::Tab::Craft);

    const pause_page::Tabs noKit{/*craft=*/false};
    pause_page::step(p, noKit, pause_page::Keys{});
    REQUIRE(p.tab == PauseState::Tab::Self);
}

TEST_CASE("with the kit the Craft tab is reachable as before", "[pause]")
{
    PauseState p = opened();
    pause_page::step(p, kAllTabs, pause_page::Keys{.right = true});
    pause_page::step(p, kAllTabs, pause_page::Keys{.right = true});
    REQUIRE(p.tab == PauseState::Tab::Craft);
}
