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

TEST_CASE("loadRegions defaults to Auto visibility when no file / no key", "[hud]")
{
    hud::Regions r;
    hud::loadRegions(r, "does_not_exist.json"); // silent no-op, keeps defaults
    REQUIRE(r.visibility == hud::Visibility::Auto);

    const hud::Regions noKey = loadFrom(R"({"pad_x": 0.02})");
    REQUIRE(noKey.visibility == hud::Visibility::Auto);
}

TEST_CASE("loadRegions parses the visibility mode", "[hud]")
{
    REQUIRE(loadFrom(R"({"visibility":"auto"})").visibility == hud::Visibility::Auto);
    REQUIRE(loadFrom(R"({"visibility":"on"})").visibility == hud::Visibility::On);
    REQUIRE(loadFrom(R"({"visibility":"off"})").visibility == hud::Visibility::Off);
    // An unrecognized value falls back to Auto (the safe default).
    REQUIRE(loadFrom(R"({"visibility":"nonsense"})").visibility == hud::Visibility::Auto);
}

TEST_CASE("loadRegions reads region rects, keeping defaults for missing fields", "[hud]")
{
    const hud::Regions r =
        loadFrom(R"({"thought":{"x":0.1,"y":0.2,"w":0.5,"h":0.3},"pad_x":0.05})");
    REQUIRE(r.thought.x == Approx(0.1f));
    REQUIRE(r.thought.y == Approx(0.2f));
    REQUIRE(r.thought.w == Approx(0.5f));
    REQUIRE(r.thought.h == Approx(0.3f));
    REQUIRE(r.pad_x == Approx(0.05f));
    // observation was absent -> its struct default survives.
    REQUIRE(r.observation.x == Approx(0.20f));
    REQUIRE(r.pad_y == Approx(0.022f));
}
