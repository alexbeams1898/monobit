#include "gl/PixelRenderTarget.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

using Catch::Approx;
using engine::gl::BlitRect;
using engine::gl::computeBlitRect;

// The blit now aspect-fits at a FRACTIONAL scale (the sharp-bilinear shader keeps
// pixels crisp), so it fills the screen -- only the ASPECT remainder letterboxes,
// never an integer remainder. These pin that behavior.

TEST_CASE("exact integer multiples fill the whole window, no bars", "[blit]")
{
    // 1280x720 x2 = 2560x1440 (1440p) and x3 = 3840x2160 (4K): edge to edge.
    BlitRect r = computeBlitRect(1280, 720, 2560, 1440);
    REQUIRE(r.width == 2560);
    REQUIRE(r.height == 1440);
    REQUIRE(r.x == 0);
    REQUIRE(r.y == 0);

    r = computeBlitRect(1280, 720, 3840, 2160);
    REQUIRE(r.width == 3840);
    REQUIRE(r.height == 2160);
}

TEST_CASE("a non-integer 16:9 window fills fully (the 2048x1152 bug)", "[blit]")
{
    // 2048x1152 is 16:9 but 2048/1280 = 1.6 (non-integer). The OLD integer-floor
    // blit fell to 1x -> 1280x720 in a 2048 window (tiny, huge letterbox). The
    // fractional aspect-fit fills it edge to edge.
    const BlitRect r = computeBlitRect(1280, 720, 2048, 1152);
    REQUIRE(r.width == 2048);
    REQUIRE(r.height == 1152);
    REQUIRE(r.x == 0);
    REQUIRE(r.y == 0);
}

TEST_CASE("a wider-than-16:9 window pillarboxes (aspect remainder only)", "[blit]")
{
    // 21:9 ultrawide: height-bound, centered, bars left/right.
    const BlitRect r = computeBlitRect(1280, 720, 3440, 1440);
    REQUIRE(r.height == 1440);                               // fills vertically
    REQUIRE(r.width == Approx(1440 * 16.0 / 9.0).margin(1)); // 16:9 width
    REQUIRE(r.x > 0);                                        // centered pillarbox
    REQUIRE(r.y == 0);
}

TEST_CASE("a taller-than-16:9 window letterboxes (aspect remainder only)", "[blit]")
{
    // 16:10 laptop: width-bound, bars top/bottom.
    const BlitRect r = computeBlitRect(1280, 720, 1920, 1200);
    REQUIRE(r.width == 1920);                                 // fills horizontally
    REQUIRE(r.height == Approx(1920 * 9.0 / 16.0).margin(1)); // 16:9 height
    REQUIRE(r.x == 0);
    REQUIRE(r.y > 0); // centered letterbox
}

TEST_CASE("degenerate internal size is handled", "[blit]")
{
    const BlitRect r = computeBlitRect(0, 0, 1920, 1080);
    REQUIRE(r.width == 0);
    REQUIRE(r.height == 0);
}
