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

TEST_CASE("with no walk to continue, the first entry begins one", "[title]")
{
    fresh();
    REQUIRE(title_screen::step(false, false, /*confirm=*/true, /*has_save=*/false) ==
            Action::Begin);
}

TEST_CASE("with a walk to continue, the first entry continues it", "[title]")
{
    fresh();
    REQUIRE(title_screen::step(false, false, /*confirm=*/true, /*has_save=*/true) ==
            Action::Continue);
}

TEST_CASE("Continue simply does not exist when there is nothing to continue", "[title]")
{
    // The entry is absent rather than present-and-refused: stepping through every row
    // with no save must never yield Continue.
    fresh();
    for (int i = 0; i < 6; ++i)
    {
        const Action a = title_screen::step(false, false, /*confirm=*/true, /*has_save=*/false);
        REQUIRE(a != Action::Continue);
        title_screen::step(false, /*down=*/true, false, /*has_save=*/false);
    }
}

TEST_CASE("down moves through the entries and wraps", "[title]")
{
    fresh(); // no save: [Begin, Leave]
    REQUIRE(title_screen::step(false, false, true, false) == Action::Begin);

    title_screen::step(false, /*down=*/true, false, false);
    REQUIRE(title_screen::step(false, false, true, false) == Action::Quit);

    title_screen::step(false, /*down=*/true, false, false); // wraps to the top
    REQUIRE(title_screen::step(false, false, true, false) == Action::Begin);
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

TEST_CASE("the cursor survives the list shrinking when a walk is wiped", "[title]")
{
    // Sit on the third entry (Leave) with a save, then ask with no save: the list is now
    // two long, and the cursor must not point past its end.
    fresh();
    title_screen::step(false, /*down=*/true, false, /*has_save=*/true);
    title_screen::step(false, /*down=*/true, false, /*has_save=*/true); // -> Leave (index 2)

    const Action a = title_screen::step(false, false, /*confirm=*/true, /*has_save=*/false);
    REQUIRE(a == Action::Quit); // clamped onto the last valid entry, not out of bounds
}
