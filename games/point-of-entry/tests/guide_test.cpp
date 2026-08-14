#include "ops/GuideOps.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <filesystem>
#include <fstream>

#include <catch2/catch_test_macros.hpp>

// The guide's promises: the listing is the pests directory under the exact ids the record
// counts by; a page carries printed sections only when its file authors them; the kill
// thresholds gate in order.

namespace
{

std::filesystem::path writePestFixture(bool withGuide)
{
    const auto dir = std::filesystem::temp_directory_path() / "poe_guide_fixture";
    std::filesystem::create_directories(dir);
    const auto path = dir / (withGuide ? "printed.json" : "blank.json");
    nlohmann::json j = {
        {"sprite", "assets/sprites/fixture.json"},
        {"sheet", {{"resistance", 3}, {"defensiveness", 2}, {"dispersal", 4}}},
    };
    if (withGuide)
        j["guide"] = {{"description", "d"}, {"signs", "s"}};
    std::ofstream(path) << j.dump(2);
    return path;
}

std::filesystem::path writeGatesFixture(int forEntry, int forStats)
{
    const auto dir = std::filesystem::temp_directory_path() / "poe_guide_fixture";
    std::filesystem::create_directories(dir);
    const auto path = dir / "stats.json";
    const nlohmann::json j = {
        {"guide", {{"kills_for_entry", forEntry}, {"kills_for_stats", forStats}}},
    };
    std::ofstream(path) << j.dump(2);
    return path;
}

} // namespace

TEST_CASE("scan lists the shipped pests under their record ids", "[guide]")
{
    const auto pages = guide::scan("config/pests");
    REQUIRE(pages.size() >= 2);
    CHECK(std::is_sorted(pages.begin(), pages.end(), [](const guide::Page& a, const guide::Page& b)
                         { return a.species < b.species; }));
    // Forward-slashed ids: the same strings the swarm spawns by, so record lookups by a
    // scanned id can never miss on a separator.
    bool foundAnt = false;
    for (const auto& p : pages)
    {
        CHECK(p.species.find('\\') == std::string::npos);
        if (p.species == "config/pests/ant.json")
        {
            foundAnt = true;
            CHECK(p.name == "ant");
            CHECK(p.printed);
            // The page's plate is the species' own art, straight off the pest file.
            CHECK(p.sprite == "assets/sprites/ant.json");
        }
    }
    CHECK(foundAnt);
}

TEST_CASE("a missing pests directory reads as an empty book", "[guide]")
{
    CHECK(guide::scan("no/such/dir").empty());
}

TEST_CASE("a species' label is its file stem, from the id the record counts by", "[guide]")
{
    CHECK(guide::nameOf("config/pests/ant.json") == "ant");
    CHECK(guide::nameOf("mouse.json") == "mouse");
    // The label a scan produces and the label derived from the raw id must be the same
    // string -- the feed and the listing may never disagree on a name.
    for (const auto& p : guide::scan("config/pests"))
        CHECK(p.name == guide::nameOf(p.species));
}

TEST_CASE("a page carries printed sections only when the file authors them", "[guide]")
{
    const guide::Page p = guide::load(writePestFixture(true).generic_string());
    CHECK(p.printed);
    CHECK(p.sprite == "assets/sprites/fixture.json");
    CHECK(p.entry.description == "d");
    CHECK(p.entry.signs == "s");
    CHECK(p.entry.biology.empty()); // an unauthored section is empty, never invented
    CHECK(p.resistance == 3);
    CHECK(p.defensiveness == 2);
    CHECK(p.dispersal == 4);

    const guide::Page b = guide::load(writePestFixture(false).generic_string());
    CHECK_FALSE(b.printed);
}

TEST_CASE("gates come from the stats config and default sanely", "[guide]")
{
    const guide::Gates g = guide::gates(writeGatesFixture(3, 7).generic_string());
    CHECK(g.kills_for_entry == 3);
    CHECK(g.kills_for_stats == 7);

    // The shipped config must gate in a workable order, whatever it is tuned to.
    const guide::Gates shipped = guide::gates("config/stats.json");
    CHECK(shipped.kills_for_entry > 0);
    CHECK(shipped.kills_for_entry <= shipped.kills_for_stats);

    const guide::Gates d = guide::gates("no/such/file.json");
    CHECK(d.kills_for_entry == 1);
    CHECK(d.kills_for_stats == 10);
}

TEST_CASE("the thresholds gate in order", "[guide]")
{
    const guide::Gates g{2, 5};
    CHECK(guide::tier(0, g) == guide::Tier::Undocumented);
    CHECK(guide::tier(1, g) == guide::Tier::Undocumented);
    CHECK(guide::tier(2, g) == guide::Tier::Entry); // the threshold itself counts
    CHECK(guide::tier(4, g) == guide::Tier::Entry);
    CHECK(guide::tier(5, g) == guide::Tier::Stats);
}
