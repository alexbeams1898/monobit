#include "TitleScreen.h"

#include <catch2/catch_test_macros.hpp>

// The title's keyboard logic is pure -- render (and its mouse) needs a font/GL context and
// is integration-tested by running the game. These pin the part that decides things.

using title_screen::Action;

namespace
{
// Each test starts with the cursor at the top; the screen is a file-static.
void fresh()
{
    title_screen::reset();
}
} // namespace

TEST_CASE("with nobody walked, the first entry is New Game", "[title]")
{
    fresh();
    REQUIRE(title_screen::step(false, false, /*confirm=*/true, /*has_save=*/false) ==
            Action::NewGame);
}

TEST_CASE("with a walk to return to, Load Game sits under New Game", "[title]")
{
    fresh();
    REQUIRE(title_screen::step(false, false, /*confirm=*/true, /*has_save=*/true) ==
            Action::NewGame); // still first

    title_screen::step(false, /*down=*/true, false, /*has_save=*/true);
    REQUIRE(title_screen::step(false, false, /*confirm=*/true, /*has_save=*/true) ==
            Action::LoadGame);
}

TEST_CASE("Load Game can't be chosen when nobody has walked", "[title]")
{
    // The entry is still THERE (the menu never changes shape) -- it just can't be
    // committed. Stepping through every row with no save must never yield Load Game.
    fresh();
    for (int i = 0; i < 6; ++i)
    {
        const Action a = title_screen::step(false, false, /*confirm=*/true, /*has_save=*/false);
        REQUIRE(a != Action::LoadGame);
        title_screen::step(false, /*down=*/true, false, /*has_save=*/false);
    }
}

TEST_CASE("the cursor steps OVER the disabled Load Game", "[title]")
{
    // W/S must never park on a row Space does nothing on: with no save, down from New Game
    // lands on Settings, skipping the disabled Load Game entirely.
    fresh();
    title_screen::step(false, /*down=*/true, false, /*has_save=*/false);
    REQUIRE(title_screen::step(false, false, /*confirm=*/true, /*has_save=*/false) ==
            Action::Settings);

    // And back up again -- the skip works in both directions.
    title_screen::step(/*up=*/true, false, false, /*has_save=*/false);
    REQUIRE(title_screen::step(false, false, /*confirm=*/true, /*has_save=*/false) ==
            Action::NewGame);
}

TEST_CASE("down moves through the entries and wraps", "[title]")
{
    fresh(); // with a save, every row is live: [New Game, Load Game, Settings, Quit]
    REQUIRE(title_screen::step(false, false, true, /*has_save=*/true) == Action::NewGame);

    title_screen::step(false, /*down=*/true, false, true);
    REQUIRE(title_screen::step(false, false, true, true) == Action::LoadGame);

    title_screen::step(false, /*down=*/true, false, true);
    REQUIRE(title_screen::step(false, false, true, true) == Action::Settings);

    title_screen::step(false, /*down=*/true, false, true);
    REQUIRE(title_screen::step(false, false, true, true) == Action::Quit);

    title_screen::step(false, /*down=*/true, false, true); // wraps to the top
    REQUIRE(title_screen::step(false, false, true, true) == Action::NewGame);
}

TEST_CASE("up from the top wraps to the last entry", "[title]")
{
    fresh();
    title_screen::step(/*up=*/true, false, false, /*has_save=*/false);
    REQUIRE(title_screen::step(false, false, true, false) == Action::Quit);
}

TEST_CASE("no confirm commits nothing", "[title]")
{
    fresh();
    REQUIRE(title_screen::step(false, /*down=*/true, /*confirm=*/false, false) == Action::None);
}

TEST_CASE("a cursor resting on Load Game goes dead when the last walk is forgotten", "[title]")
{
    // Sit on Load Game with a save, then ask with none -- the row it's on just became
    // unpressable, and confirming it must do NOTHING rather than open an empty roster.
    fresh();
    title_screen::step(false, /*down=*/true, false, /*has_save=*/true); // -> Load Game

    REQUIRE(title_screen::step(false, false, /*confirm=*/true, /*has_save=*/false) == Action::None);
}

TEST_CASE("the menu keeps its shape whether or not anyone has walked", "[title]")
{
    // Load Game is disabled, never removed: the rows below it must not slide up when the
    // last walk is forgotten. Two downs is Settings in both worlds -- if the entry
    // vanished, the same keystrokes would land somewhere else.
    fresh();
    title_screen::step(false, /*down=*/true, false, /*has_save=*/true);
    title_screen::step(false, /*down=*/true, false, /*has_save=*/true);
    REQUIRE(title_screen::step(false, false, true, /*has_save=*/true) == Action::Settings);
}
