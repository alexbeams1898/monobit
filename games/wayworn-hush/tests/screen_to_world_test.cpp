#include "ScreenToWorld.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

using Catch::Approx;
using screen_to_world::map;

// Internal render res used by the game (kInternalWidth/Height).
constexpr int kIW = 1280;
constexpr int kIH = 720;

TEST_CASE("Window center maps to the camera position", "[screen_to_world]")
{
    // A 1:1 blit (window == internal, no letterbox), camera at (500,400). The window
    // center pixel is the camera center in world space.
    const auto r = map(1280.0f / 2, 720.0f / 2, 0, 0, 1280, 720, kIW, kIH, 500.0f, 400.0f);
    REQUIRE(r.x == Approx(500.0f));
    REQUIRE(r.y == Approx(400.0f));
}

TEST_CASE("Offsetting the cursor moves the world point by the same internal px (1:1 blit)",
          "[screen_to_world]")
{
    // At 1:1, a pixel offset from center is a world offset of the same magnitude.
    const auto r = map(1280.0f / 2 + 100, 720.0f / 2 - 40, 0, 0, 1280, 720, kIW, kIH, 0.0f, 0.0f);
    REQUIRE(r.x == Approx(100.0f));
    REQUIRE(r.y == Approx(-40.0f));
}

TEST_CASE("A 2x upscaled blit halves the window-px offset into internal px",
          "[screen_to_world]")
{
    // Window is 2560x1440, internal 1280x720 -> blit fills at 2x (no letterbox). A cursor
    // 200 window-px right of center is 100 internal-px = 100 world units right of camera.
    const auto r = map(2560.0f / 2 + 200, 1440.0f / 2, 0, 0, 2560, 1440, kIW, kIH, 0.0f, 0.0f);
    REQUIRE(r.x == Approx(100.0f));
    REQUIRE(r.y == Approx(0.0f));
}

TEST_CASE("Letterbox offset is subtracted before scaling", "[screen_to_world]")
{
    // Window 1280x800 (taller than 16:9): the 1280x720 image blits at 1x, letterboxed
    // with a 40px bar top+bottom (blit_y=40). A click at the image's top-left (0,40)
    // window px is internal (0,0) -> world = cam - internalHalf.
    const auto r = map(0.0f, 40.0f, 0, 40, 1280, 720, kIW, kIH, 0.0f, 0.0f);
    REQUIRE(r.x == Approx(-640.0f)); // -1280/2
    REQUIRE(r.y == Approx(-360.0f)); // -720/2
}

TEST_CASE("A zero-size blit (pre-first-resize) maps to the camera center", "[screen_to_world]")
{
    const auto r = map(123.0f, 456.0f, 0, 0, 0, 0, kIW, kIH, 700.0f, 300.0f);
    REQUIRE(r.x == Approx(700.0f));
    REQUIRE(r.y == Approx(300.0f));
}
