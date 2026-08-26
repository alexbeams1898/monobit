#include "screens/ScreenInput.h"
#include "screens/ScreenStyle.h"

#include <catch2/catch_test_macros.hpp>

// The page anatomy's contract. Only the horizontal half is asserted here: row and tab HEIGHTS
// come from the loaded face, which needs a window, while widths are spacing units and hold at
// any ladder step -- every measure below is linear in the scale, so what passes at k=1 passes
// at every k.
namespace
{
constexpr int kW = 1280;
constexpr int kH = 720;
const float kCx = static_cast<float>(kW) * 0.5f;

bool spansWithin(float x, float w, const screen_style::Rect& outer)
{
    return x >= outer.x && x + w <= outer.x + outer.w;
}
} // namespace

TEST_CASE("the page's frame contains everything the anatomy lays on it")
{
    const screen_style::Rect panel = screen_style::pagePanelRect(kW, kH);

    SECTION("the page sits centred in the window, on both axes")
    {
        REQUIRE(panel.x == static_cast<float>(kW) - (panel.x + panel.w));
        REQUIRE(panel.y == static_cast<float>(kH) - (panel.y + panel.h));
    }

    SECTION("the frame is evenly padded, and the heading sits at the top of its field")
    {
        const float left = screen_style::pageColumnLeft(kCx) - panel.x;
        const float right =
            (panel.x + panel.w) - (screen_style::pageColumnLeft(kCx) + screen_style::pageColumnW());
        REQUIRE(left == right);
        // Where the heading lands is the frame's padding, not a number of its own.
        REQUIRE(screen_style::pageHeadingY(kH) - panel.y == left);
        // The gap down to content is line heights, which are zero with no face loaded.
        REQUIRE(screen_style::pageContentY(kH) >= screen_style::pageHeadingY(kH));
    }

    SECTION("a row never reaches past its page")
    {
        const screen_style::Rect row = screen_style::pageRow(kCx, 200.0f);
        REQUIRE(spansWithin(row.x, row.w, panel));
    }

    SECTION("a tab strip never reaches past its page, at any count")
    {
        for (const int count : {1, 2, 3, 4, 6, 8})
            for (int i = 0; i < count; ++i)
            {
                const screen_style::Rect tab = screen_style::pageTab(kCx, 200.0f, i, count);
                REQUIRE(spansWithin(tab.x, tab.w, panel));
            }
    }

    SECTION("a card never reaches past its page, on any edge")
    {
        const screen_style::Rect card =
            screen_style::pageBand(kW, 300.0f, screen_style::pageBottom(kH));
        REQUIRE(spansWithin(card.x, card.w, panel));
        REQUIRE(card.y >= panel.y);
        REQUIRE(card.y + card.h <= panel.y + panel.h);
    }
}

TEST_CASE("the column is the one horizontal measure")
{
    SECTION("tabs tile the column exactly -- no gap, no overlap")
    {
        constexpr int kCount = 4;
        const screen_style::Rect first = screen_style::pageTab(kCx, 0.0f, 0, kCount);
        const screen_style::Rect last = screen_style::pageTab(kCx, 0.0f, kCount - 1, kCount);
        REQUIRE(first.x == screen_style::pageColumnLeft(kCx));
        REQUIRE(last.x + last.w == screen_style::pageColumnLeft(kCx) + screen_style::pageColumnW());
        for (int i = 1; i < kCount; ++i)
        {
            const screen_style::Rect prev = screen_style::pageTab(kCx, 0.0f, i - 1, kCount);
            const screen_style::Rect here = screen_style::pageTab(kCx, 0.0f, i, kCount);
            REQUIRE(here.x == prev.x + prev.w);
        }
    }

    SECTION("a row and the card are cut from the same column")
    {
        const screen_style::Rect row = screen_style::pageRow(kCx, 200.0f);
        const screen_style::Rect card =
            screen_style::pageBand(kW, 200.0f, screen_style::pageBottom(kH));
        REQUIRE(row.x == card.x);
        REQUIRE(row.w == card.w);
        REQUIRE(row.w == screen_style::pageColumnW());
    }

    SECTION("a stop lands inside the row it belongs to")
    {
        const screen_style::Rect row = screen_style::pageRow(kCx, 200.0f);
        REQUIRE(screen_style::pageStop(row, 0.0f) == row.x);
        REQUIRE(screen_style::pageStop(row, 1.0f) == row.x + row.w);
        REQUIRE(screen_style::pageStop(row, 0.5f) == kCx);
    }
}

TEST_CASE("a list opens with nothing chosen")
{
    constexpr int kNone = shell_input::kNoChoice;

    SECTION("the first key press lands on an end rather than resuming a choice")
    {
        REQUIRE(shell_input::step(kNone, 4, /*up=*/false, /*down=*/true) == 0);
        REQUIRE(shell_input::step(kNone, 4, /*up=*/true, /*down=*/false) == 3);
    }

    SECTION("from a real choice it walks and wraps")
    {
        REQUIRE(shell_input::step(0, 4, false, true) == 1);
        REQUIRE(shell_input::step(3, 4, false, true) == 0);
        REQUIRE(shell_input::step(0, 4, true, false) == 3);
    }

    SECTION("an empty list has nothing to choose")
    {
        REQUIRE(shell_input::step(kNone, 0, false, true) == kNone);
        REQUIRE(shell_input::step(2, 0, true, false) == kNone);
    }

    SECTION("no keys, no movement")
    {
        REQUIRE(shell_input::step(2, 4, false, false) == 2);
        REQUIRE(shell_input::step(kNone, 4, false, false) == kNone);
    }
}

TEST_CASE("the pointer folds into the same cursor")
{
    constexpr int kNone = shell_input::kNoChoice;

    SECTION("what it is over wins")
    {
        REQUIRE(shell_input::hover(kNone, 2, /*moved=*/true) == 2);
        REQUIRE(shell_input::hover(0, 2, /*moved=*/false) == 2);
    }

    SECTION("moving off everything clears the choice")
    {
        REQUIRE(shell_input::hover(2, kNone, /*moved=*/true) == kNone);
    }

    SECTION("a pointer sitting still cannot wipe a choice made at the keyboard")
    {
        REQUIRE(shell_input::hover(2, kNone, /*moved=*/false) == 2);
    }
}

TEST_CASE("the register's window")
{
    constexpr int kNone = shell_input::kNoChoice;
    constexpr int kRows = 20;
    constexpr int kVisible = 6;

    SECTION("a list shorter than its window never scrolls")
    {
        REQUIRE(shell_input::scrollTop(0, 3, kVisible, /*wheel=*/-5, kNone) == 0);
        REQUIRE(shell_input::scrollTop(0, 3, kVisible, /*wheel=*/5, kNone) == 0);
    }

    SECTION("the wheel moves the view and stops at both ends")
    {
        REQUIRE(shell_input::scrollTop(4, kRows, kVisible, /*wheel=*/1, kNone) == 3);
        REQUIRE(shell_input::scrollTop(0, kRows, kVisible, /*wheel=*/3, kNone) == 0);
        REQUIRE(shell_input::scrollTop(13, kRows, kVisible, /*wheel=*/-3, kNone) == 14);
        REQUIRE(shell_input::scrollTop(14, kRows, kVisible, /*wheel=*/-1, kNone) == 14);
    }

    SECTION("a followed cursor is dragged into sight from either side")
    {
        REQUIRE(shell_input::scrollTop(10, kRows, kVisible, 0, /*follow=*/2) == 2);
        REQUIRE(shell_input::scrollTop(0, kRows, kVisible, 0, /*follow=*/8) == 3);
    }

    SECTION("a cursor already in sight leaves the view where it is")
    {
        REQUIRE(shell_input::scrollTop(4, kRows, kVisible, 0, /*follow=*/6) == 4);
    }

    SECTION("without a follow the view stays where the hand left it")
    {
        REQUIRE(shell_input::scrollTop(9, kRows, kVisible, 0, kNone) == 9);
    }
}

TEST_CASE("a split page keeps both its sides inside the column")
{
    const screen_style::Rect panel = screen_style::pagePanelRect(kW, kH);
    const screen_style::Split s =
        screen_style::pageSplit(kW, 200.0f, screen_style::pageBottom(kH), 0.62f);

    SECTION("neither side reaches past the page")
    {
        REQUIRE(spansWithin(s.left.x, s.left.w, panel));
        REQUIRE(spansWithin(s.right.x, s.right.w, panel));
    }

    SECTION("the two sides never overlap, and together they are the column")
    {
        REQUIRE(s.left.x + s.left.w < s.right.x);
        REQUIRE(s.left.x == screen_style::pageColumnLeft(kCx));
        REQUIRE(s.right.x + s.right.w ==
                screen_style::pageColumnLeft(kCx) + screen_style::pageColumnW());
    }

    SECTION("both sides get the full height they were given")
    {
        REQUIRE(s.left.y == s.right.y);
        REQUIRE(s.left.h == s.right.h);
    }

    SECTION("a row in a band is that band's width, not the column's")
    {
        const screen_style::Rect row = screen_style::bandRow(s.right, 300.0f);
        REQUIRE(row.x == s.right.x);
        REQUIRE(row.w == s.right.w);
        REQUIRE(row.w < screen_style::pageColumnW());
    }
}

TEST_CASE("a cursor steps over what cannot be taken")
{
    constexpr int kNone = shell_input::kNoChoice;
    const auto evens = [](int i) { return i % 2 == 0; };

    SECTION("it lands on the first takeable row from nothing")
    {
        REQUIRE(shell_input::stepOver(kNone, 6, false, true, evens) == 0);
        REQUIRE(shell_input::stepOver(kNone, 6, true, false, evens) == 4);
    }

    SECTION("it skips the dead rows between")
    {
        REQUIRE(shell_input::stepOver(0, 6, false, true, evens) == 2);
        REQUIRE(shell_input::stepOver(4, 6, false, true, evens) == 0);
    }

    SECTION("a list with nothing takeable leaves the cursor on nothing")
    {
        REQUIRE(shell_input::stepOver(kNone, 6, false, true, [](int) { return false; }) == kNone);
    }

    SECTION("no keys, no movement")
    {
        REQUIRE(shell_input::stepOver(3, 6, false, false, evens) == 3);
    }
}
