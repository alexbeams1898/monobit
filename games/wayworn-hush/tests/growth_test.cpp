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

TEST_CASE("A buff level with no matching def does not count toward a faculty", "[growth]")
{
    GrowthState s = makeState();
    // Orphan level (no BuffDef for this id): counts toward Spirit (raw sum of
    // levels) but not toward any faculty (facultyLevel walks the defs).
    s.buff_levels["ghost"] = 5;
    REQUIRE(growth::spirit(s) == 5);
    REQUIRE(growth::facultyLevel(s, "wonder") == 0);
}

TEST_CASE("Loading a missing config leaves an empty, valid state", "[growth]")
{
    GrowthState s;
    growth::load(s, "config/does_not_exist.json");
    REQUIRE(s.faculties.empty());
    REQUIRE(s.buff_defs.empty());
    REQUIRE(growth::spirit(s) == 0);
}

TEST_CASE("Loading the authored faculties config reads the three faculties, no buffs yet",
          "[growth]")
{
    GrowthState s;
    // Test working dir is the game source root (see CMake WORKING_DIRECTORY).
    growth::load(s, "config/faculties.json");
    REQUIRE(s.faculties.size() == 3);
    REQUIRE(s.faculties[0] == "wonder");
    REQUIRE(s.faculties[1] == "reason");
    REQUIRE(s.faculties[2] == "perception");
    REQUIRE(s.buff_defs.empty()); // effects are content-driven; none authored yet
}
