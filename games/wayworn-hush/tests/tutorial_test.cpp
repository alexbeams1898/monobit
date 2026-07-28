#include "Tutorial.h"

#include <filesystem>
#include <fstream>

#include <catch2/catch_test_macros.hpp>

// Card RENDERING needs GL (integration-tested by running the game); these cover
// the load and fire-once logic, which is pure state.

namespace
{
tutorial::Config twoOnOneEvent()
{
    tutorial::Config cfg;
    cfg.cards.push_back(tutorial::Card{"first", "thought", "A", "body", "thought"});
    cfg.cards.push_back(tutorial::Card{"second", "thought", "B", "body", ""});
    cfg.cards.push_back(tutorial::Card{"other", "reading", "C", "body", ""});
    return cfg;
}
} // namespace

TEST_CASE("load keeps well-formed cards and drops one without id or event", "[tutorial]")
{
    const auto dir = std::filesystem::temp_directory_path() / "wayworn_tutorial_fixture";
    std::filesystem::create_directories(dir);
    const auto path = (dir / "tutorial.json").string();
    std::ofstream(path) << R"({ "cards": [
        { "id": "ok", "on": "reading", "title": "T", "body": "B", "focus": "observation" },
        { "id": "no_event", "title": "T", "body": "B" },
        { "on": "reading", "title": "no id", "body": "B" }
    ]})";

    tutorial::Config cfg;
    tutorial::load(cfg, path);
    REQUIRE(cfg.cards.size() == 1);
    REQUIRE(cfg.cards[0].id == "ok");
    REQUIRE(cfg.cards[0].focus == "observation");
}

TEST_CASE("fire queues listeners in authored order, once per pilgrim", "[tutorial]")
{
    const tutorial::Config cfg = twoOnOneEvent();
    tutorial::State st;

    REQUIRE(tutorial::current(st) == nullptr);
    tutorial::fire(st, cfg, "thought");
    REQUIRE(tutorial::current(st)->id == "first");

    // The same event again can never double-queue -- seen is marked at queue time.
    tutorial::fire(st, cfg, "thought");
    tutorial::dismiss(st);
    REQUIRE(tutorial::current(st)->id == "second");
    tutorial::dismiss(st);
    REQUIRE(tutorial::current(st) == nullptr);

    tutorial::fire(st, cfg, "unheard_of"); // an event with no listeners is a no-op
    REQUIRE(tutorial::current(st) == nullptr);
}

TEST_CASE("a resumed walk's seen set suppresses its cards", "[tutorial]")
{
    const tutorial::Config cfg = twoOnOneEvent();
    tutorial::State st;
    st.seen = {"first", "second"}; // what the save restored

    tutorial::fire(st, cfg, "thought");
    REQUIRE(tutorial::current(st) == nullptr); // both already seen -- nothing replays
    tutorial::fire(st, cfg, "reading");
    REQUIRE(tutorial::current(st)->id == "other"); // an unseen card still teaches
}
