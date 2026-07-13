#include "Notebook.h"

#include <catch2/catch_test_macros.hpp>

using observations::LineKind;

namespace
{
void add(notebook::Record& r, const std::string& text, int day,
         LineKind kind = LineKind::Observation)
{
    notebook::record(r, kind, text, /*faculty=*/{}, /*difficulty=*/0, day);
}
} // namespace

TEST_CASE("record appends entries in write order", "[notebook]")
{
    notebook::Record r;
    add(r, "first", 1);
    add(r, "second", 1, LineKind::Thought);
    REQUIRE(r.entries.size() == 2);
    REQUIRE(r.entries[0].text == "first");
    REQUIRE(r.entries[1].kind == LineKind::Thought);
}

TEST_CASE("groupByDay groups by day, ascending, undated last", "[notebook]")
{
    notebook::Record r;
    add(r, "d2-a", 2);
    add(r, "d1-a", 1);
    add(r, "undated", 0);
    add(r, "d2-b", 2);
    add(r, "d1-b", 1);

    const auto groups = notebook::groupByDay(r);
    REQUIRE(groups.size() == 3);

    // Day 1 first, then day 2, then the undated (day 0) group last.
    REQUIRE(groups[0].day == 1);
    REQUIRE(groups[1].day == 2);
    REQUIRE(groups[2].day == 0);

    // Within a day, entries keep write order.
    REQUIRE(groups[0].entries.size() == 2);
    REQUIRE(groups[0].entries[0].text == "d1-a");
    REQUIRE(groups[0].entries[1].text == "d1-b");
    REQUIRE(groups[1].entries[0].text == "d2-a");
    REQUIRE(groups[1].entries[1].text == "d2-b");
    REQUIRE(groups[2].entries[0].text == "undated");
}

TEST_CASE("an empty record groups to nothing", "[notebook]")
{
    notebook::Record r;
    REQUIRE(notebook::groupByDay(r).empty());
}
