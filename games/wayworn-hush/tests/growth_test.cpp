#include "Growth.h"

#include <catch2/catch_test_macros.hpp>

using growth::BuffDef;
using growth::GrowthState;

namespace
{
// A self with two wonder buffs and one reason buff, so per-faculty vs overall
// sums are distinguishable.
GrowthState makeState()
{
    GrowthState s;
    s.faculties = {"wonder", "reason", "perception"};
    s.secondary = {"survival", "craftsmanship"};
    s.buff_defs = {
        BuffDef{"wonder_a", "wonder", 3},
        BuffDef{"wonder_b", "wonder", 3},
        BuffDef{"reason_a", "reason", 3},
    };
    return s;
}
} // namespace

TEST_CASE("A fresh self has zero Spirit and zero in every faculty", "[growth]")
{
    const GrowthState s = makeState();
    REQUIRE(growth::spirit(s) == 0);
    REQUIRE(growth::facultyLevel(s, "wonder") == 0);
    REQUIRE(growth::facultyLevel(s, "reason") == 0);
    REQUIRE(growth::facultyLevel(s, "perception") == 0);
}

TEST_CASE("Spirit is the sum of all buff levels owned", "[growth]")
{
    GrowthState s = makeState();
    s.buff_levels["wonder_a"] = 2;
    s.buff_levels["wonder_b"] = 1;
    s.buff_levels["reason_a"] = 3;
    REQUIRE(growth::spirit(s) == 6);
}

TEST_CASE("facultyLevel sums only that faculty's buffs", "[growth]")
{
    GrowthState s = makeState();
    s.buff_levels["wonder_a"] = 2;
    s.buff_levels["wonder_b"] = 1;
    s.buff_levels["reason_a"] = 3;
    REQUIRE(growth::facultyLevel(s, "wonder") == 3); // 2 + 1
    REQUIRE(growth::facultyLevel(s, "reason") == 3);
    REQUIRE(growth::facultyLevel(s, "perception") == 0);
}

TEST_CASE("An unknown faculty has level zero", "[growth]")
{
    GrowthState s = makeState();
    s.buff_levels["wonder_a"] = 2;
    REQUIRE(growth::facultyLevel(s, "not_a_faculty") == 0);
}

TEST_CASE("statLevel returns a stat's base value, 0 for absent/unknown", "[growth]")
{
    GrowthState s = makeState();
    REQUIRE(growth::statLevel(s, "wonder") == 0);   // absent
    REQUIRE(growth::statLevel(s, "survival") == 0); // absent
    s.stat_levels["survival"] = 4;
    s.stat_levels["wonder"] = 2;
    REQUIRE(growth::statLevel(s, "survival") == 4);
    REQUIRE(growth::statLevel(s, "wonder") == 2);
    REQUIRE(growth::statLevel(s, "not_a_stat") == 0); // unknown
}

TEST_CASE("facultyLevel is the base value plus that faculty's buffs", "[growth]")
{
    GrowthState s = makeState();
    s.stat_levels["wonder"] = 3; // base
    s.buff_levels["wonder_a"] = 2;
    s.buff_levels["wonder_b"] = 1;
    REQUIRE(growth::facultyLevel(s, "wonder") == 6); // 3 base + 2 + 1 buffs
    // A faculty with a base but no buffs is just its base.
    s.stat_levels["perception"] = 5;
    REQUIRE(growth::facultyLevel(s, "perception") == 5);
}

TEST_CASE("Secondary stats live in the same map, read via statLevel", "[growth]")
{
    GrowthState s = makeState();
    s.stat_levels["craftsmanship"] = 7;
    REQUIRE(growth::statLevel(s, "craftsmanship") == 7);
    // Secondary stats are not faculties, so facultyLevel doesn't apply buffs to
    // them -- but statLevel treats every name uniformly.
    REQUIRE(growth::facultyLevel(s, "craftsmanship") == 7); // base only (no buffs defined)
}

TEST_CASE("A buff level with no matching def does not count toward a faculty", "[growth]")
{
    GrowthState s = makeState();
    // Orphan level (no BuffDef for this id): counts toward Spirit (raw sum of
    // levels) but not toward any faculty (facultyLevel walks the defs).
    s.buff_levels["ghost"] = 5;
    REQUIRE(growth::spirit(s) == 5);
    REQUIRE(growth::facultyLevel(s, "wonder") == 0);
}

TEST_CASE("facultyColor returns the authored hue, a neutral default when absent", "[growth]")
{
    GrowthState s = makeState();
    // No colors authored -> neutral off-white default.
    const growth::Rgb def = growth::facultyColor(s, "wonder");
    REQUIRE(def.r > 0.9f);
    REQUIRE(def.g > 0.9f);
    // Authored hue is returned exactly.
    s.faculty_colors["wonder"] = growth::Rgb{0.9f, 0.7f, 0.4f};
    const growth::Rgb hue = growth::facultyColor(s, "wonder");
    REQUIRE(hue.r == 0.9f);
    REQUIRE(hue.g == 0.7f);
    REQUIRE(hue.b == 0.4f);
}

TEST_CASE("Loading a missing config leaves an empty, valid state", "[growth]")
{
    GrowthState s;
    growth::load(s, "config/does_not_exist.json");
    REQUIRE(s.faculties.empty());
    REQUIRE(s.buff_defs.empty());
    REQUIRE(growth::spirit(s) == 0);
}

TEST_CASE("Loading the authored config reads faculties + secondary stats, no buffs yet", "[growth]")
{
    GrowthState s;
    // Test working dir is the game source root (see CMake WORKING_DIRECTORY).
    growth::load(s, "config/faculties.json");
    REQUIRE(s.faculties.size() == 3);
    REQUIRE(s.faculties[0] == "wonder");
    REQUIRE(s.faculties[1] == "reason");
    REQUIRE(s.faculties[2] == "perception");
    REQUIRE(s.secondary.size() == 2);
    REQUIRE(s.secondary[0] == "survival");
    REQUIRE(s.secondary[1] == "craftsmanship");
    REQUIRE(s.buff_defs.empty()); // effects are content-driven; none authored yet
}
