#include "SettingsScreen.h"

#include <catch2/catch_test_macros.hpp>

// The screen's keyboard logic is pure -- render (and its mouse) needs a font/GL context and
// is integration-tested by running the game. These pin the part that decides things.

using settings_screen::Action;

namespace
{
void fresh()
{
    settings_screen::reset();
}

// Move the cursor down `n` rows on the showing page.
void down(settings::Settings& s, int n)
{
    for (int i = 0; i < n; ++i)
        settings_screen::step(s, false, /*down=*/true, false, false, false, false);
}

Action confirm(settings::Settings& s)
{
    return settings_screen::step(s, false, false, false, false, /*confirm=*/true, false);
}

Action back(settings::Settings& s)
{
    return settings_screen::step(s, false, false, false, false, false, /*back=*/true);
}

Action right(settings::Settings& s)
{
    return settings_screen::step(s, false, false, false, /*right=*/true, false, false);
}

// Open the HUD category from a fresh screen (it is the first row of the top page).
void openHud(settings::Settings& s)
{
    fresh();
    confirm(s);
}
} // namespace

TEST_CASE("the screen opens on the category list, not on a setting", "[settings_screen]")
{
    // Settings grows by category; landing straight in a flat list of every option is what
    // the top page exists to prevent.
    fresh();
    settings::Settings s;
    const settings::Settings before = s;

    // Right on a category does nothing -- a category goes somewhere, it isn't a value.
    right(s);
    REQUIRE(s.hud.visibility == before.hud.visibility);
    REQUIRE(s.hud.show_time == before.hud.show_time);
}

TEST_CASE("back from the top page leaves the screen", "[settings_screen]")
{
    fresh();
    settings::Settings s;
    REQUIRE(back(s) == Action::Back);
}

TEST_CASE("back from a category returns to the top page, NOT out of settings", "[settings_screen]")
{
    // One back per level. Backing out of HUD must not dump the player back to the title --
    // they asked to leave the category, not the screen.
    settings::Settings s;
    openHud(s);
    REQUIRE(back(s) == Action::None); // stayed in settings
    REQUIRE(back(s) == Action::Back); // now on the top page, so this leaves
}

TEST_CASE("confirm opens the HUD category and its settings become reachable", "[settings_screen]")
{
    settings::Settings s;
    s.hud.visibility = hud::Visibility::Auto;
    openHud(s);

    right(s); // the first HUD row is the mode
    REQUIRE(s.hud.visibility == hud::Visibility::On);
}

TEST_CASE("the HUD mode cycles, wrapping", "[settings_screen]")
{
    settings::Settings s;
    s.hud.visibility = hud::Visibility::Auto;
    openHud(s);

    right(s);
    REQUIRE(s.hud.visibility == hud::Visibility::On);
    right(s);
    REQUIRE(s.hud.visibility == hud::Visibility::Off);
    right(s); // wraps
    REQUIRE(s.hud.visibility == hud::Visibility::Auto);
}

TEST_CASE("left cycles the HUD mode the other way", "[settings_screen]")
{
    settings::Settings s;
    s.hud.visibility = hud::Visibility::Auto;
    openHud(s);
    settings_screen::step(s, false, false, /*left=*/true, false, false, false);
    REQUIRE(s.hud.visibility == hud::Visibility::Off); // wraps backward, not forward
}

TEST_CASE("each toggle row flips only its own piece", "[settings_screen]")
{
    // The rows are a table shared by step() and render(); a row reaching the wrong setting
    // is exactly what that table exists to prevent.
    settings::Settings s;
    s.hud.show_time = true; // start both on, so a flip is visible either way
    s.hud.show_stance = true;
    openHud(s);

    down(s, 1); // the time
    right(s);
    REQUIRE_FALSE(s.hud.show_time);
    REQUIRE(s.hud.show_stance); // untouched

    down(s, 1); // the stance badge
    right(s);
    REQUIRE_FALSE(s.hud.show_stance);
    REQUIRE_FALSE(s.hud.show_time); // stays where it was put
}

TEST_CASE("a toggle goes both ways whichever direction is pressed", "[settings_screen]")
{
    // Two values: either direction is simply "the other one".
    settings::Settings s;
    s.hud.show_time = true;
    openHud(s);
    down(s, 1);

    right(s);
    REQUIRE_FALSE(s.hud.show_time);
    settings_screen::step(s, false, false, /*left=*/true, false, false, false);
    REQUIRE(s.hud.show_time);
}

TEST_CASE("the cursor wraps around a category's rows", "[settings_screen]")
{
    // Up from the top lands on the LAST row, not off the end. Asserted against whatever the
    // last row currently is (the Spirit readout) -- the wrap is the subject, not the row.
    settings::Settings s;
    s.hud.show_spirit = true;
    openHud(s);
    settings_screen::step(s, /*up=*/true, false, false, false, false, false);
    right(s);
    REQUIRE_FALSE(s.hud.show_spirit);
}

TEST_CASE("the cursor resets when stepping into and out of a category", "[settings_screen]")
{
    // A cursor left three rows down would land somewhere arbitrary on a page of a
    // different length -- and on the row the player last touched, not the one they expect.
    settings::Settings s;
    s.hud.show_stance = true;
    openHud(s);
    down(s, 2); // sit on the last HUD row
    back(s);    // out to the top page

    // The cursor is back at the first category, so confirm opens HUD rather than reading
    // past the end of a one-entry list.
    confirm(s);
    right(s); // the first HUD row is the mode, not the stance badge
    REQUIRE(s.hud.show_stance);
}

TEST_CASE("moving the cursor changes no value", "[settings_screen]")
{
    settings::Settings s;
    const settings::Settings before = s;
    openHud(s);
    down(s, 2);
    settings_screen::step(s, /*up=*/true, false, false, false, false, false);
    REQUIRE(s.hud.visibility == before.hud.visibility);
    REQUIRE(s.hud.show_time == before.hud.show_time);
    REQUIRE(s.hud.show_stance == before.hud.show_stance);
}

TEST_CASE("the time is off by default -- an unhurried walk doesn't watch a clock",
          "[settings_screen]")
{
    const settings::Settings fresh_settings;
    REQUIRE_FALSE(fresh_settings.hud.show_time);
    REQUIRE(fresh_settings.hud.show_stance); // the stance badge teaches the core verb
}
