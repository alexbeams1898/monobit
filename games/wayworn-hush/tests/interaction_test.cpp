#include "Interaction.h"

#include <catch2/catch_test_macros.hpp>

using interaction::Candidate;
using interaction::Intent;
using interaction::resolve;

namespace
{
// A 32x32 box centered at (x,y).
Candidate box(float x, float y)
{
    return {x, y, 32.0f, 32.0f};
}
} // namespace

TEST_CASE("Nothing targeted when the player is out of reach and no hover", "[interaction]")
{
    const std::vector<Candidate> items = {box(500, 500)};
    Intent it;
    it.px = 0;
    it.py = 0;
    const auto r = resolve(items, it, 40.0f);
    REQUIRE(r.index == -1);
    REQUIRE_FALSE(r.fire);
}

TEST_CASE("Proximity targets the nearest box within reach", "[interaction]")
{
    const std::vector<Candidate> items = {box(0, 0), box(200, 0)};
    Intent it;
    it.px = 30;
    it.py = 0; // near box 0 (edge at 16), far from box 1
    const auto r = resolve(items, it, 40.0f);
    REQUIRE(r.index == 0);
}

TEST_CASE("Pressing fires the proximity target", "[interaction]")
{
    const std::vector<Candidate> items = {box(0, 0)};
    Intent it;
    it.px = 0;
    it.py = 0;
    it.pressed = true;
    const auto r = resolve(items, it, 40.0f);
    REQUIRE(r.index == 0);
    REQUIRE(r.fire);
}

TEST_CASE("Hover beats proximity: the cursor overrides where you stand", "[interaction]")
{
    // Player stands in box 0's reach; cursor hovers box 1 (also within player reach).
    const std::vector<Candidate> items = {box(0, 0), box(40, 0)};
    Intent it;
    it.px = 0;
    it.py = 0;
    it.mouse_valid = true;
    it.mouse_x = 40; // inside box 1
    it.mouse_y = 0;
    const auto r = resolve(items, it, 60.0f);
    REQUIRE(r.index == 1); // hover wins
}

TEST_CASE("A click fires only when it landed on a hovered interactable, not empty space",
          "[interaction]")
{
    const std::vector<Candidate> items = {box(0, 0)};
    Intent it;
    it.px = 0;
    it.py = 0;
    it.clicked = true;

    SECTION("click over the box (hovering) -> fires")
    {
        it.mouse_valid = true;
        it.mouse_x = 0;
        it.mouse_y = 0; // inside the box
        const auto r = resolve(items, it, 40.0f);
        REQUIRE(r.index == 0);
        REQUIRE(r.fire);
    }
    SECTION("click in empty space (no hover) -> targets by proximity but does NOT fire")
    {
        it.mouse_valid = true;
        it.mouse_x = 500;
        it.mouse_y = 500; // not over any box
        const auto r = resolve(items, it, 40.0f);
        REQUIRE(r.index == 0); // still the proximity target
        REQUIRE_FALSE(r.fire); // but a click in the void doesn't fire it
    }
}

TEST_CASE("A cursor hovering something out of the player's reach does not target it",
          "[interaction]")
{
    // Cursor is over box 1, but the player is far from it -> hover is gated by reach.
    const std::vector<Candidate> items = {box(0, 0), box(1000, 0)};
    Intent it;
    it.px = 0;
    it.py = 0; // near box 0 only
    it.mouse_valid = true;
    it.mouse_x = 1000;
    it.mouse_y = 0; // over box 1, far from player
    const auto r = resolve(items, it, 40.0f);
    REQUIRE(r.index == 0); // falls back to the in-reach proximity target
}
