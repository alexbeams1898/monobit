#include "Surfaces.h"

#include <catch2/catch_test_macros.hpp>

// Surface table: terrain walkability keyed by surface name. Pure JSON -> struct + a
// lookup with defined fallbacks. Test working dir is the game source root (CMake
// WORKING_DIRECTORY), so the real config/surfaces.json loads.

TEST_CASE("The authored surfaces config loads and grass walks, water blocks", "[surfaces]")
{
    surfaces::Config cfg;
    surfaces::load(cfg, "config/surfaces.json");
    REQUIRE_FALSE(cfg.surfaces.empty());
    // Surface names match the LDtk enum values exactly (capitalized -- LDtk enforces).
    REQUIRE(cfg.walkable("Grass"));
    REQUIRE_FALSE(cfg.walkable("Water"));
}

TEST_CASE("Walkability has safe fallbacks (empty name -> default, unknown -> passable)",
          "[surfaces]")
{
    surfaces::Config cfg;
    cfg.default_surface = "grass";
    cfg.surfaces["grass"] = {true};
    cfg.surfaces["water"] = {false};

    // Empty name resolves to the default surface (untagged tiles = bare grass fill).
    REQUIRE(cfg.walkable("") == cfg.walkable("grass"));
    // An unknown surface never traps the player: passable rather than a silent wall.
    REQUIRE(cfg.walkable("lava_not_defined"));
}

TEST_CASE("A missing config leaves defaults and never traps the player", "[surfaces]")
{
    surfaces::Config cfg;
    surfaces::load(cfg, "config/does_not_exist.json"); // silent no-op
    REQUIRE(cfg.walkable("anything"));                 // no table -> passable, not a wall
}
