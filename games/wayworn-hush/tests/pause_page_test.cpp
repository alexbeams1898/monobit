#include "GameLoop.h"
#include "PausePage.h"

#include <catch2/catch_test_macros.hpp>

using pause_page::Action;

// step() is the pure input->state transition for the pause page (no SDL/GL).
// Signature: step(pause, toggle, left, right, up, down, confirm).
// Controls: F = toggle/back, A/D = tabs, W/S = item select, Space = confirm.

namespace
{
PauseState opened()
{
    PauseState p;
    pause_page::step(p, /*toggle=*/true, false, false, false, false, false);
    return p;
}

// Open and page to the System tab (Self -> Noticed -> Satchel -> Notebook ->
// System). System is the last tab; a single left from Self reaches it faster, but
// the tests exercise the forward path explicitly.
PauseState onSystem()
{
    PauseState p = opened();
    pause_page::step(p, false, /*left=*/true, false, false, false, false); // wrap back to System
    return p;
}
} // namespace

TEST_CASE("F opens the page from closed, on the Self tab, no sub-view", "[pause]")
{
    PauseState p;
    REQUIRE_FALSE(p.open);
    const Action a = pause_page::step(p, /*toggle=*/true, false, false, false, false, false);
    REQUIRE(a == Action::None);
    REQUIRE(p.open);
    REQUIRE(p.tab == PauseState::Tab::Self);
    REQUIRE(p.view_stack.empty());
}

TEST_CASE("F while on the tabs closes the page", "[pause]")
{
    PauseState p = opened();
    const Action a = pause_page::step(p, /*toggle=*/true, false, false, false, false, false);
    REQUIRE(a == Action::Resume);
    REQUIRE_FALSE(p.open);
}

TEST_CASE("A/D page through the tabs, wrapping", "[pause]")
{
    PauseState p = opened(); // Self
    pause_page::step(p, false, false, /*right=*/true, false, false, false);
    REQUIRE(p.tab == PauseState::Tab::Noticed);
    pause_page::step(p, false, false, /*right=*/true, false, false, false);
    REQUIRE(p.tab == PauseState::Tab::Satchel);
    pause_page::step(p, false, false, /*right=*/true, false, false, false);
    REQUIRE(p.tab == PauseState::Tab::Craft);
    pause_page::step(p, false, false, /*right=*/true, false, false, false);
    REQUIRE(p.tab == PauseState::Tab::Notebook);
    pause_page::step(p, false, false, /*right=*/true, false, false, false);
    REQUIRE(p.tab == PauseState::Tab::System);
    pause_page::step(p, false, false, /*right=*/true, false, false, false);
    REQUIRE(p.tab == PauseState::Tab::Self); // wrapped
    pause_page::step(p, false, /*left=*/true, false, false, false, false);
    REQUIRE(p.tab == PauseState::Tab::System); // wrapped back
}

TEST_CASE("System tab: nothing selected until W/S; then move/wrap", "[pause]")
{
    PauseState p = onSystem();
    REQUIRE(p.system_sel == -1); // nothing highlighted on arrival
    pause_page::step(p, false, false, false, false, /*down=*/true, false);
    REQUIRE(p.system_sel == 0); // down from none -> first item (Controls)
    pause_page::step(p, false, false, false, false, /*down=*/true, false);
    REQUIRE(p.system_sel == 1); // Quit
    pause_page::step(p, false, false, false, /*up=*/true, false, false);
    REQUIRE(p.system_sel == 0); // Controls
    pause_page::step(p, false, false, false, /*up=*/true, false, false);
    REQUIRE(p.system_sel == 1); // wraps to last
}

TEST_CASE("System tab: up from none picks the last item", "[pause]")
{
    PauseState p = onSystem();
    REQUIRE(p.system_sel == -1);
    pause_page::step(p, false, false, false, /*up=*/true, false, false);
    REQUIRE(p.system_sel == 1); // up from none -> last item (Quit)
}

TEST_CASE("System tab: confirm with nothing selected does nothing", "[pause]")
{
    PauseState p = onSystem(); // system_sel == -1
    const Action a = pause_page::step(p, false, false, false, false, false, /*confirm=*/true);
    REQUIRE(a == Action::None);
    REQUIRE(p.view_stack.empty());
    REQUIRE(p.open);
}

TEST_CASE("System tab: confirm on Controls pushes the Controls sub-view", "[pause]")
{
    PauseState p = onSystem();
    pause_page::step(p, false, false, false, false, /*down=*/true, false); // select Controls
    const Action a = pause_page::step(p, false, false, false, false, false, /*confirm=*/true);
    REQUIRE(a == Action::None);
    REQUIRE(p.view_stack.size() == 1);
    REQUIRE(p.view_stack.back() == PauseState::View::Controls);
    REQUIRE(p.open);
}

TEST_CASE("F in the Controls sub-view pops to the tabs, not closing the page", "[pause]")
{
    PauseState p = onSystem();
    pause_page::step(p, false, false, false, false, /*down=*/true, false);    // select Controls
    pause_page::step(p, false, false, false, false, false, /*confirm=*/true); // push Controls
    REQUIRE(p.view_stack.size() == 1);
    const Action a = pause_page::step(p, /*toggle=*/true, false, false, false, false, false);
    REQUIRE(a == Action::None);
    REQUIRE(p.view_stack.empty()); // popped back to tabs
    REQUIRE(p.open);               // page still open
}

TEST_CASE("System tab: confirm on Quit returns Quit", "[pause]")
{
    PauseState p = onSystem();
    pause_page::step(p, false, false, false, /*up=*/true, false, false); // up from none -> Quit
    REQUIRE(p.system_sel == 1);
    const Action a = pause_page::step(p, false, false, false, false, false, /*confirm=*/true);
    REQUIRE(a == Action::Quit);
    REQUIRE(p.open); // caller acts on Quit
}

TEST_CASE("Confirm on a read-only tab does nothing", "[pause]")
{
    PauseState p = opened(); // Self
    REQUIRE(pause_page::step(p, false, false, false, false, false, /*confirm=*/true) ==
            Action::None);
    pause_page::step(p, false, false, /*right=*/true, false, false, false); // Noticed
    REQUIRE(pause_page::step(p, false, false, false, false, false, /*confirm=*/true) ==
            Action::None);
    REQUIRE(p.open);
}

TEST_CASE("Input while closed (no toggle) does nothing", "[pause]")
{
    PauseState p;
    const Action a =
        pause_page::step(p, false, /*left=*/true, false, false, false, /*confirm=*/true);
    REQUIRE(a == Action::None);
    REQUIRE_FALSE(p.open);
}

TEST_CASE("Craft tab: Space toggles a material; Combine returns Craft", "[pause]")
{
    // Navigate to the Craft tab (Self -> Noticed -> Satchel -> Craft).
    PauseState p = opened();
    for (int i = 0; i < 3; ++i)
        pause_page::step(p, false, false, /*right=*/true, false, false, false);
    REQUIRE(p.tab == PauseState::Tab::Craft);

    // Two materials + a trailing Combine row. Cursor starts on row 0.
    const std::vector<std::string> mats = {"thyme", "water"};

    // Space on row 0 toggles "thyme" into the attempt (not a Craft action yet).
    Action a = pause_page::step(p, false, false, false, false, false, /*confirm=*/true, mats);
    REQUIRE(a == Action::None);
    REQUIRE(p.craft_selected.count("thyme") == 1);

    // Space again toggles it back off.
    pause_page::step(p, false, false, false, false, false, /*confirm=*/true, mats);
    REQUIRE(p.craft_selected.count("thyme") == 0);

    // W/S move over the rows; step down to the Combine row (index == mats.size()).
    pause_page::step(p, false, false, false, false, false, /*confirm=*/true, mats); // re-add thyme
    pause_page::step(p, false, false, false, false, /*down=*/true, false, mats);    // -> water
    pause_page::step(p, false, false, false, false, /*down=*/true, false, mats);    // -> Combine
    REQUIRE(p.craft_sel == static_cast<int>(mats.size()));

    // Space on Combine returns Craft for the caller to enact.
    a = pause_page::step(p, false, false, false, false, false, /*confirm=*/true, mats);
    REQUIRE(a == Action::Craft);
}
