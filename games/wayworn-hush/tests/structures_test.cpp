#include "Structures.h"

#include <catch2/catch_test_macros.hpp>

// Walk-on structures: the 9-slice tile picker + config load. Pure data; no GL. Test
// working dir is the game source root (CMake WORKING_DIRECTORY), so the real
// config/structures.json loads.

namespace
{
// A 3x3 layout whose cells encode their position as {col-index, row-index} (0..2), so a
// test can read back which slice was chosen for any (lc,lr,w,h).
structures::Layout gridLayout()
{
    structures::Layout lay;
    lay.surface = "Bridge";
    for (int row = 0; row < 3; ++row)
        for (int col = 0; col < 3; ++col)
            lay.cell[static_cast<std::size_t>(row)][static_cast<std::size_t>(col)] = {col, row};
    return lay;
}
} // namespace

TEST_CASE("The 9-slice picker maps corners, edges, and interior to the right cell", "[structures]")
{
    const structures::Layout lay = gridLayout();
    const int w = 4, h = 3; // a 4x3 deck
    // Corners.
    REQUIRE(lay.at(0, 0, w, h) == std::array<int, 2>{0, 0});         // top-left
    REQUIRE(lay.at(w - 1, 0, w, h) == std::array<int, 2>{2, 0});     // top-right
    REQUIRE(lay.at(0, h - 1, w, h) == std::array<int, 2>{0, 2});     // bottom-left
    REQUIRE(lay.at(w - 1, h - 1, w, h) == std::array<int, 2>{2, 2}); // bottom-right
    // Edges.
    REQUIRE(lay.at(2, 0, w, h) == std::array<int, 2>{1, 0});     // top edge
    REQUIRE(lay.at(0, 1, w, h) == std::array<int, 2>{0, 1});     // left edge
    REQUIRE(lay.at(w - 1, 1, w, h) == std::array<int, 2>{2, 1}); // right edge
    // Interior -> center.
    REQUIRE(lay.at(2, 1, w, h) == std::array<int, 2>{1, 1});
}

TEST_CASE("A 1-wide vertical structure uses the full-deck CENTER column (not a rail)",
          "[structures]")
{
    const structures::Layout lay = gridLayout();
    const int w = 1, h = 5; // a 1x5 linear vertical bridge
    // w==1 -> no left/right edge; the single column is the full deck -> center (col 1).
    // Rows still cap: top / repeating middle / bottom. This is why a thin vertical bridge
    // renders as n/c/s (full deck with both rails), not the left rail alone.
    REQUIRE(lay.at(0, 0, w, h) == std::array<int, 2>{1, 0});     // top-center
    REQUIRE(lay.at(0, 2, w, h) == std::array<int, 2>{1, 1});     // mid-center
    REQUIRE(lay.at(0, h - 1, w, h) == std::array<int, 2>{1, 2}); // bottom-center
}

TEST_CASE("A 1-tall horizontal structure uses the full-deck CENTER row (not a cap edge)",
          "[structures]")
{
    const structures::Layout lay = gridLayout();
    const int w = 5, h = 1;
    // h==1 -> no top/bottom edge; the single row is the full deck -> center (row 1).
    REQUIRE(lay.at(0, 0, w, h) == std::array<int, 2>{0, 1});     // left-center
    REQUIRE(lay.at(2, 0, w, h) == std::array<int, 2>{1, 1});     // mid-center
    REQUIRE(lay.at(w - 1, 0, w, h) == std::array<int, 2>{2, 1}); // right-center
}

TEST_CASE("A 1x1 structure uses the single center cell", "[structures]")
{
    const structures::Layout lay = gridLayout();
    // Both dims 1 -> pure center (c). A one-tile plank / platform.
    REQUIRE(lay.at(0, 0, 1, 1) == std::array<int, 2>{1, 1});
}

TEST_CASE("The authored structures config loads the Bridge layout + surface", "[structures]")
{
    structures::Config cfg;
    structures::load(cfg, "config/structures.json");
    const auto it = cfg.layouts.find("Bridge");
    REQUIRE(it != cfg.layouts.end());
    REQUIRE(it->second.surface == "Bridge");
    // Its 9 cells are all in the bridge atlas region (cols 5-7, rows 6-8).
    for (int row = 0; row < 3; ++row)
        for (int col = 0; col < 3; ++col)
        {
            const auto& c =
                it->second.cell[static_cast<std::size_t>(row)][static_cast<std::size_t>(col)];
            REQUIRE(c[0] >= 5);
            REQUIRE(c[0] <= 7);
            REQUIRE(c[1] >= 6);
            REQUIRE(c[1] <= 8);
        }
}

TEST_CASE("The Bridge's center column is walkable footing; side rails are overhang", "[structures]")
{
    // The load-bearing rule: a bridge's rails hang over water (rendered, not walked on).
    // Only the center column (n/c/s) is footing; the six edge/corner slices are overhang.
    structures::Config cfg;
    structures::load(cfg, "config/structures.json");
    const auto it = cfg.layouts.find("Bridge");
    REQUIRE(it != cfg.layouts.end());
    const structures::Layout& lay = it->second;
    const int w = 3, h = 3; // a 3-wide bridge: side columns are rails
    // Center column walkable at top/mid/bottom.
    REQUIRE(lay.walkableAt(1, 0, w, h));
    REQUIRE(lay.walkableAt(1, 1, w, h));
    REQUIRE(lay.walkableAt(1, 2, w, h));
    // Side columns (rails) are NOT walkable.
    REQUIRE_FALSE(lay.walkableAt(0, 1, w, h)); // left rail
    REQUIRE_FALSE(lay.walkableAt(2, 1, w, h)); // right rail
    REQUIRE_FALSE(lay.walkableAt(0, 0, w, h)); // corner
    // A 1-wide bridge is all-center -> all footing (no rails to hang over water).
    REQUIRE(lay.walkableAt(0, 0, 1, 3));
    REQUIRE(lay.walkableAt(0, 1, 1, 3));
}

TEST_CASE("Walkability defaults to all-true when a structure omits the walkable block",
          "[structures]")
{
    structures::Layout const lay; // default-constructed: every slice walkable
    REQUIRE(lay.walkableAt(0, 0, 3, 3));
    REQUIRE(lay.walkableAt(2, 2, 3, 3));
}

TEST_CASE("A missing structures config leaves an empty (permissive) table", "[structures]")
{
    structures::Config cfg;
    structures::load(cfg, "config/does_not_exist.json"); // silent no-op
    REQUIRE(cfg.layouts.empty());                        // no structures -> importer skips them
}
