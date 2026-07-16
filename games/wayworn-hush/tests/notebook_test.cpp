#include "Notebook.h"
#include "WorldClock.h"

#include <catch2/catch_test_macros.hpp>

namespace
{
// A thought table standing in for authored config: ids paired with a rarity band.
observations::State withThoughts(const std::vector<std::pair<std::string, int>>& ids)
{
    observations::State s;
    for (const auto& [id, band] : ids)
    {
        observations::Thought t;
        t.id = id;
        t.text = id + " text";
        t.difficulty = band;
        s.thoughts.push_back(t);
    }
    return s;
}

// Mark a thought as landed, the way the observation engine does.
void fire(observations::State& s, const std::string& id)
{
    s.fired.insert(id);
}

// A clock whose day is a round 100 seconds, so a test can say "day 2" by writing 150.
worldclock::WorldClock clock100()
{
    return worldclock::WorldClock{0.0, 100.0};
}
} // namespace

TEST_CASE("note keeps the FIRST moment a thought landed", "[notebook]")
{
    notebook::Record r;
    notebook::note(r, "a", 20.0);
    notebook::note(r, "a", 70.0); // a second note must not re-date the memory
    REQUIRE(r.at.at("a") == 20.0);
}

TEST_CASE("note ignores an empty id", "[notebook]")
{
    notebook::Record r;
    notebook::note(r, "", 1.0);
    REQUIRE(r.at.empty());
}

TEST_CASE("found() returns ONLY what he has reached", "[notebook]")
{
    // What he hasn't thought is not his and has no line in his notebook.
    observations::State s = withThoughts({{"a", 1}, {"b", 1}, {"c", 1}});
    fire(s, "b");
    notebook::Record r;
    notebook::note(r, "b", 30.0);

    const auto rows = notebook::found(r, s);
    REQUIRE(rows.size() == 1);
    REQUIRE(rows[0].thought->id == "b");
    REQUIRE(rows[0].at == 30.0);
}

TEST_CASE("found() is chronological, untimed last", "[notebook]")
{
    observations::State s = withThoughts({{"a", 2}, {"b", 2}, {"c", 2}});
    for (const auto& id : {"a", "b", "c"})
        fire(s, id);
    notebook::Record r;
    notebook::note(r, "a", 500.0);
    notebook::note(r, "b", 100.0);
    notebook::note(r, "c", notebook::kUntimed); // no moment on record

    const auto rows = notebook::found(r, s);
    REQUIRE(rows[0].thought->id == "b");
    REQUIRE(rows[1].thought->id == "a");
    REQUIRE(rows[2].thought->id == "c"); // untimed sorts last, not first
}

TEST_CASE("found() orders WITHIN a day by the moment, not authored order", "[notebook]")
{
    // Storing the moment (not a day) is what buys this: "second" is authored first but was
    // thought later, and the page reads in the order he actually wrote it.
    observations::State s = withThoughts({{"second", 1}, {"first", 1}});
    fire(s, "first");
    fire(s, "second");
    notebook::Record r;
    notebook::note(r, "first", 10.0);
    notebook::note(r, "second", 40.0); // same day, later

    const auto rows = notebook::found(r, s);
    REQUIRE(rows[0].thought->id == "first");
    REQUIRE(rows[1].thought->id == "second");
}

TEST_CASE("rarity does NOT reorder the pages", "[notebook]")
{
    // It's a notebook: the page is when he was there. A legendary thought reached on day 5
    // still comes after a common one from day 1.
    observations::State s = withThoughts({{"early_common", 1}, {"late_legendary", 5}});
    fire(s, "early_common");
    fire(s, "late_legendary");
    notebook::Record r;
    notebook::note(r, "early_common", 50.0);
    notebook::note(r, "late_legendary", 450.0);

    const auto rows = notebook::found(r, s);
    REQUIRE(rows[0].thought->id == "early_common");
    REQUIRE(rows[1].thought->id == "late_legendary");
}

TEST_CASE("untimed notes hold authored order and don't shuffle", "[notebook]")
{
    // Untimed notes are all equal to each other, so the sort must leave them where the
    // config put them rather than reordering between frames (the cursor would jump).
    observations::State s = withThoughts({{"first", 2}, {"second", 2}});
    fire(s, "first");
    fire(s, "second");

    const auto rows = notebook::found(notebook::Record{}, s);
    REQUIRE(rows[0].thought->id == "first");
    REQUIRE(rows[1].thought->id == "second");
    REQUIRE(notebook::found(notebook::Record{}, s)[0].thought->id == "first"); // stable
}

TEST_CASE("byDay splits the notes into the days he wrote them", "[notebook]")
{
    observations::State s = withThoughts({{"a", 1}, {"b", 1}, {"c", 1}, {"d", 1}});
    for (const auto& id : {"a", "b", "c", "d"})
        fire(s, id);
    notebook::Record r;
    notebook::note(r, "a", 10.0);  // day 1
    notebook::note(r, "b", 150.0); // day 2
    notebook::note(r, "c", 80.0);  // day 1
    notebook::note(r, "d", notebook::kUntimed);

    const auto days = notebook::byDay(r, s, clock100());
    REQUIRE(days.size() == 3);
    REQUIRE(days[0].day == 1);
    REQUIRE(days[0].entries.size() == 2); // a + c share day 1
    REQUIRE(days[1].day == 2);
    REQUIRE(days[2].day == 0); // the untimed page last
    REQUIRE(days[2].entries.size() == 1);
}

TEST_CASE("byDay holds every note found(), and no more", "[notebook]")
{
    // The tab walks byDay but indexes the cursor flat: a note in one and not the other
    // would put the detail panel on the wrong entry.
    observations::State s = withThoughts({{"a", 1}, {"b", 1}, {"c", 1}});
    fire(s, "a");
    fire(s, "c");
    notebook::Record r;
    notebook::note(r, "a", 10.0);
    notebook::note(r, "c", 150.0);

    std::size_t n = 0;
    for (const auto& d : notebook::byDay(r, s, clock100()))
        n += d.entries.size();
    REQUIRE(n == notebook::found(r, s).size());
}

TEST_CASE("byDay is empty when nothing has been reached", "[notebook]")
{
    const observations::State s = withThoughts({{"a", 1}});
    REQUIRE(notebook::byDay(notebook::Record{}, s, clock100()).empty());
}

TEST_CASE("a found thought with no note reads as untimed", "[notebook]")
{
    // The record only knows moments. A thought that fired while he carried no notebook
    // is still HIS -- `fired` says so -- it just has no time on it.
    observations::State s = withThoughts({{"a", 1}});
    fire(s, "a");
    const auto rows = notebook::found(notebook::Record{}, s);
    REQUIRE(rows.size() == 1);
    REQUIRE_FALSE(notebook::timed(rows[0]));
}

TEST_CASE("a note taken at the very start of the walk is timed, not untimed", "[notebook]")
{
    // The boundary the kUntimed sentinel has to survive: second 0 is a real moment (the
    // first instant of day 1), and must not read as "no moment on record". A sentinel of
    // 0 rather than -1 would collapse these two.
    observations::State s = withThoughts({{"a", 1}});
    fire(s, "a");
    notebook::Record r;
    notebook::note(r, "a", 0.0);

    const auto rows = notebook::found(r, s);
    REQUIRE(notebook::timed(rows[0]));
    REQUIRE(notebook::byDay(r, s, clock100())[0].day == 1);
}

TEST_CASE("found() carries the thought's authored stats, not a copy", "[notebook]")
{
    // An entry points into the thought table, so re-tuning a thought's rarity is
    // reflected in the notebook rather than frozen at the moment it was written.
    observations::State s = withThoughts({{"a", 1}});
    fire(s, "a");
    notebook::Record r;
    notebook::note(r, "a", 10.0);

    s.thoughts[0].difficulty = 5;
    REQUIRE(notebook::found(r, s)[0].thought->difficulty == 5);
}

TEST_CASE("a noted id with no thought behind it is dropped", "[notebook]")
{
    // Content removed since the walk: the note survives in the save, but the
    // collection is the thought table's shape, so it simply doesn't appear.
    observations::State s = withThoughts({{"a", 1}});
    fire(s, "gone");
    notebook::Record r;
    notebook::note(r, "gone", 10.0);

    REQUIRE(notebook::found(r, s).empty()); // 'a' was never reached; 'gone' no longer exists
}

TEST_CASE("nothing reached means an empty notebook", "[notebook]")
{
    const observations::State s = withThoughts({{"a", 1}});
    REQUIRE(notebook::found(notebook::Record{}, s).empty());
}
