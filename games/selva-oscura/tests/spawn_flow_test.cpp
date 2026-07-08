#include <nlohmann/json.hpp>

#include <fstream>

#include <catch2/catch_test_macros.hpp>

// Smoke tests for the spawn-flow JSON schema. Validates that the
// authored config/spawn_flows/*.json files parse with the fields the
// FlowSpawner code expects. Catches schema-drift bugs at CI time
// instead of in-game.

TEST_CASE("acheron_foundlings.json parses with expected fields", "[spawn][flow]")
{
    std::ifstream in("config/spawn_flows/acheron_foundlings.json");
    REQUIRE(in.is_open());
    nlohmann::json j;
    in >> j;

    REQUIRE(j.contains("id"));
    REQUIRE(j["id"].get<std::string>() == "acheron_foundlings");

    REQUIRE(j.contains("archetype"));
    REQUIRE(j["archetype"].get<std::string>() == "foundling");

    REQUIRE(j.contains("spawn_region_id"));
    REQUIRE(j["spawn_region_id"].get<std::string>() == "limbo");

    REQUIRE(j.contains("spawn_position"));
    REQUIRE(j["spawn_position"].is_array());
    REQUIRE(j["spawn_position"].size() == 3);

    REQUIRE(j.contains("scripted_target_pos"));
    REQUIRE(j["scripted_target_pos"].is_array());
    REQUIRE(j["scripted_target_pos"].size() == 3);

    REQUIRE(j.contains("active_count_includes"));
    REQUIRE(j["active_count_includes"].is_array());
    bool has_fresh = false;
    bool has_aged = false;
    for (const auto& s : j["active_count_includes"])
    {
        if (s.get<std::string>() == "foundling")
            has_fresh = true;
        if (s.get<std::string>() == "gorged_foundling")
            has_aged = true;
    }
    REQUIRE(has_fresh);
    REQUIRE(has_aged);

    REQUIRE(j.contains("active_cap"));
    REQUIRE(j["active_cap"].get<int>() > 0);

    REQUIRE(j.contains("spawn_interval_seconds"));
    REQUIRE(j["spawn_interval_seconds"].get<float>() > 0.0f);

    REQUIRE(j.contains("on_arrival_action"));
    const std::string action = j["on_arrival_action"].get<std::string>();
    REQUIRE((action.empty() || action == "halt" || action == "despawn" ||
             action.rfind("convert_to:", 0) == 0));

    REQUIRE(j.contains("on_arrival_delay_seconds"));
    REQUIRE(j["on_arrival_delay_seconds"].get<float>() >= 0.0f);

    // Initial population block. Authored "positions" list (each entry
    // [x, y, z, yaw]); the spawner builds one initial actor per entry
    // so the count is determined by the list length.
    REQUIRE(j.contains("initial_population"));
    const auto& ip = j["initial_population"];
    REQUIRE(ip.is_object());
    REQUIRE(ip.contains("archetype"));
    REQUIRE(ip["archetype"].get<std::string>() == "gorged_foundling");
    REQUIRE(ip.contains("positions"));
    REQUIRE(ip["positions"].is_array());
    REQUIRE(!ip["positions"].empty());
    for (const auto& p : ip["positions"])
    {
        REQUIRE(p.is_array());
        REQUIRE(p.size() >= 3); // [x, y, z] minimum; optional 4th = yaw
    }

    // Optional per-slot archetype overrides (parallel to positions).
    // Length must match positions; entries are strings.
    if (ip.contains("archetypes"))
    {
        REQUIRE(ip["archetypes"].is_array());
        REQUIRE(ip["archetypes"].size() == ip["positions"].size());
        for (const auto& a : ip["archetypes"])
            REQUIRE(a.is_string());
    }

    // Slot-mode targeting. With target_mode=first_vacant_slot, each
    // trickle picks the first vacant slot from initial_population.positions
    // and uses THAT as its scripted target -- so killed-slot positions
    // get repopulated by the next fresh.
    if (j.contains("target_mode"))
    {
        const std::string tm = j["target_mode"].get<std::string>();
        REQUIRE((tm == "fixed_target" || tm == "first_vacant_slot"));
    }

    // On-arrival clip: optional one-shot held on the actor from the
    // moment it reaches its scripted target until the arrival action
    // fires (paired conceptually with on_arrival_action). Foundlings
    // use it to crawl-bite a corpse while the conversion timer runs.
    if (j.contains("on_arrival_clip"))
    {
        REQUIRE(j["on_arrival_clip"].is_string());
        REQUIRE(j.contains("on_arrival_clip_freeze_at_seconds"));
        REQUIRE(j["on_arrival_clip_freeze_at_seconds"].get<float>() >= 0.0f);
    }
}
