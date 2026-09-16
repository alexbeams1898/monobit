#include "HudCanvas.h"

#include <cstdio>
#include <fstream>
#include <string>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

using Catch::Approx;

namespace
{
// Write JSON to a scratch file and load it, returning the resolved Regions. The
// path is relative to the test working dir (the game source root); it's removed
// after loading so tests leave no residue.
hud::Regions loadFrom(const std::string& json)
{
    const std::string path = "hud_canvas_test.tmp.json";
    {
        std::ofstream f(path);
        f << json;
    }
    hud::Regions r;
    hud::loadRegions(r, path);
    std::remove(path.c_str());
    return r;
}

// The same, for the authored default visibility (which is a preference's seed, not part of
// the layout -- see hud::loadVisibility).
hud::Visibility visibilityFrom(const std::string& json,
                               hud::Visibility fallback = hud::Visibility::Auto)
{
    const std::string path = "hud_canvas_vis_test.tmp.json";
    {
        std::ofstream f(path);
        f << json;
    }
    const hud::Visibility v = hud::loadVisibility(path, fallback);
    std::remove(path.c_str());
    return v;
}
} // namespace

TEST_CASE("safeArea fits a 16:9 rect centered in the window", "[hud]")
{
    SECTION("an exactly-16:9 window is filled edge to edge, no bars")
    {
        const hud::Rect s = hud::safeArea(2560, 1440);
        REQUIRE(s.x == Approx(0.0f));
        REQUIRE(s.y == Approx(0.0f));
        REQUIRE(s.w == Approx(2560.0f));
        REQUIRE(s.h == Approx(1440.0f));
    }
    SECTION("a wider-than-16:9 window pillarboxes (bars left/right)")
    {
        const hud::Rect s = hud::safeArea(3440, 1440); // 21:9 ultrawide
        REQUIRE(s.h == Approx(1440.0f));               // height-bound
        REQUIRE(s.w == Approx(1440.0f * 16.0f / 9.0f));
        REQUIRE(s.x == Approx((3440.0f - s.w) * 0.5f)); // centered
        REQUIRE(s.y == Approx(0.0f));
    }
    SECTION("a taller-than-16:9 window letterboxes (bars top/bottom)")
    {
        const hud::Rect s = hud::safeArea(1600, 1200); // 4:3
        REQUIRE(s.w == Approx(1600.0f));               // width-bound
        REQUIRE(s.h == Approx(1600.0f / (16.0f / 9.0f)));
        REQUIRE(s.x == Approx(0.0f));
        REQUIRE(s.y == Approx((1200.0f - s.h) * 0.5f)); // centered
    }
}

TEST_CASE("resolve maps a canvas-fraction rect into the safe area", "[hud]")
{
    // On an exact-16:9 window the safe area is the whole window, so a 0.20,0.10
    // fraction lands at 0.20*W, 0.10*H with 0.60*W x 0.22*H size.
    const hud::Rect r = hud::resolve({0.20f, 0.10f, 0.60f, 0.22f}, 2560, 1440);
    REQUIRE(r.x == Approx(0.20f * 2560.0f));
    REQUIRE(r.y == Approx(0.10f * 1440.0f));
    REQUIRE(r.w == Approx(0.60f * 2560.0f));
    REQUIRE(r.h == Approx(0.22f * 1440.0f));
}

TEST_CASE("scale is the safe-area width (uniform HUD scale)", "[hud]")
{
    REQUIRE(hud::scale(2560, 1440) == Approx(2560.0f));
    REQUIRE(hud::scale(3440, 1440) == Approx(1440.0f * 16.0f / 9.0f)); // pillarboxed width
}

TEST_CASE("loadVisibility keeps the fallback when there's no file / no key", "[hud]")
{
    // Missing config must not overwrite what the caller already had -- it is seeding a
    // preference, not asserting one.
    REQUIRE(hud::loadVisibility("does_not_exist.json", hud::Visibility::On) == hud::Visibility::On);
    REQUIRE(visibilityFrom(R"({"pad_x": 0.02})", hud::Visibility::Off) == hud::Visibility::Off);
}

TEST_CASE("loadVisibility parses the authored default", "[hud]")
{
    REQUIRE(visibilityFrom(R"({"visibility":"auto"})") == hud::Visibility::Auto);
    REQUIRE(visibilityFrom(R"({"visibility":"on"})") == hud::Visibility::On);
    REQUIRE(visibilityFrom(R"({"visibility":"off"})") == hud::Visibility::Off);
    // An unrecognized value keeps the fallback rather than guessing.
    REQUIRE(visibilityFrom(R"({"visibility":"nonsense"})", hud::Visibility::On) ==
            hud::Visibility::On);
}

TEST_CASE("the HUD layout does not carry a visibility of its own", "[hud]")
{
    // Regions is authored LAYOUT; visibility is a player PREFERENCE (settings::Settings).
    // Two homes for it would be two answers to "is the HUD on" -- this pins that the config
    // read for one doesn't quietly populate the other.
    const hud::Regions r = loadFrom(R"({"visibility":"off","pad_x":0.05})");
    REQUIRE(r.pad_x == Approx(0.05f)); // the layout still loads; the mode simply isn't here
}

TEST_CASE("loadRegions reads region rects, keeping defaults for missing fields", "[hud]")
{
    const hud::Regions r = loadFrom(R"({"upper":{"x":0.1,"y":0.2,"w":0.5,"h":0.3},"pad_x":0.05})");
    REQUIRE(r.upper.x == Approx(0.1f));
    REQUIRE(r.upper.y == Approx(0.2f));
    REQUIRE(r.upper.w == Approx(0.5f));
    REQUIRE(r.upper.h == Approx(0.3f));
    REQUIRE(r.pad_x == Approx(0.05f));
    // content was absent -> its struct default survives.
    REQUIRE(r.content.x == Approx(0.20f));
    REQUIRE(r.pad_y == Approx(0.022f));
}
