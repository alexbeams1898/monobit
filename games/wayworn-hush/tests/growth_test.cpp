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

TEST_CASE("recordUse accumulates exp; statLevel derives a diminishing level", "[growth][use]")
{
    // A stat rises by being EXERCISED. The level is derived from accumulated use via a log
    // curve: exp_per_level buys level 1, each further level costs progressively more.
    GrowthState s = makeState();
    s.exp_per_level = 20.0f;

    REQUIRE(growth::statLevel(s, "perception") == 0); // no use yet

    growth::recordUse(s, "perception", 20);           // exactly one "level" of exp
    REQUIRE(growth::statLevel(s, "perception") == 1); // floor(log2(20/20 + 1)) = floor(log2 2) = 1

    growth::recordUse(s, "perception", 40); // total 60 -> log2(60/20+1)=log2(4)=2
    REQUIRE(growth::statLevel(s, "perception") == 2);

    // Diminishing: it took 20 for level 1, but 60 total for level 2, and 140 total for level 3.
    growth::recordUse(s, "perception", 80); // total 140 -> log2(140/20+1)=log2(8)=3
    REQUIRE(growth::statLevel(s, "perception") == 3);
}

TEST_CASE("use-derived level stacks on top of the base", "[growth][use]")
{
    GrowthState s = makeState();
    s.exp_per_level = 20.0f;
    s.stat_levels["reason"] = 5; // authored base
    growth::recordUse(s, "reason", 20);
    REQUIRE(growth::statLevel(s, "reason") == 6); // 5 base + 1 from use
}

TEST_CASE("recordUse ignores empty name and non-positive exp", "[growth][use]")
{
    GrowthState s = makeState();
    growth::recordUse(s, "", 50);
    growth::recordUse(s, "wonder", 0);
    growth::recordUse(s, "wonder", -10);
    REQUIRE(s.stat_use.empty());
    REQUIRE(growth::statLevel(s, "wonder") == 0);
}

TEST_CASE("any stat grows by use -- mental or physical, no distinction", "[growth][use]")
{
    // The principle: the mechanism doesn't care which family. A physical stat grows exactly
    // like a mental one; content decides what gets exercised.
    GrowthState s = makeState();
    s.exp_per_level = 20.0f;
    growth::recordUse(s, "survival", 20); // physical
    growth::recordUse(s, "wonder", 20);   // mental
    REQUIRE(growth::statLevel(s, "survival") == 1);
    REQUIRE(growth::statLevel(s, "wonder") == 1);
}

TEST_CASE("statProgress: a fresh stat is level 0, empty bar", "[growth][use]")
{
    GrowthState s = makeState();
    s.exp_per_level = 20.0f;
    const auto p = growth::statProgress(s, "wonder");
    REQUIRE(p.level == 0);
    REQUIRE(p.fill == 0.0f);
}

TEST_CASE("statProgress: the bar renormalizes each level (fill resets after a level-up)",
          "[growth][use]")
{
    // The Skyrim-style bar: fill is 0..1 through THIS level's span, so it resets to ~empty
    // just after crossing into a new level and approaches full just before the next.
    GrowthState s = makeState();
    s.exp_per_level = 20.0f;

    // Level 0 spans use 0..20. Halfway (use 10) -> ~half full.
    growth::recordUse(s, "reason", 10);
    auto p = growth::statProgress(s, "reason");
    REQUIRE(p.level == 0);
    REQUIRE(p.fill > 0.4f);
    REQUIRE(p.fill < 0.6f);

    // Cross into level 1 (use 20): bar RESETS toward empty against level 1's larger span (20..60).
    growth::recordUse(s, "reason", 10); // total 20
    p = growth::statProgress(s, "reason");
    REQUIRE(p.level == 1);
    REQUIRE(p.fill < 0.1f); // just crossed -> near empty

    // Fill level 1 toward its end (use ~55 of the 20..60 span) -> near full.
    growth::recordUse(s, "reason", 35); // total 55
    p = growth::statProgress(s, "reason");
    REQUIRE(p.level == 1);
    REQUIRE(p.fill > 0.8f);
}

TEST_CASE("statProgress into/span describe the current level's raw exp window", "[growth][use]")
{
    GrowthState s = makeState();
    s.exp_per_level = 20.0f;
    growth::recordUse(s, "wonder", 30); // level 1 (20..60), 10 into a 40-wide span
    const auto p = growth::statProgress(s, "wonder");
    REQUIRE(p.level == 1);
    REQUIRE(p.into == 10);
    REQUIRE(p.span == 40);
}
