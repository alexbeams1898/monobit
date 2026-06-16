#include "AppState.h"
#include "SaveManager.h"

#include <cstdio>
#include <filesystem>
#include <string>

#include <catch2/catch_test_macros.hpp>

// Tests use a per-test temp path so concurrent runs and CI re-runs don't
// share state. Working directory is build/bin/ (set by CMake on the test
// target), so relative "tmp/..." paths resolve to a writable location
// that's gitignored.

namespace
{
std::string testSavePath(const std::string& name)
{
    return "tmp/selva-save-tests/" + name + ".json";
}

void cleanupTestFile(const std::string& path)
{
    std::error_code ec;
    std::filesystem::remove(path, ec);
}
} // namespace

TEST_CASE("SaveManager round-trip preserves character list", "[save][round-trip]")
{
    const std::string path = testSavePath("round-trip");
    cleanupTestFile(path);

    selva::SaveData data;
    selva::SaveManager::addCharacter(data, "ELENA");
    selva::SaveManager::addCharacter(data, "DANTE");

    REQUIRE(selva::SaveManager::save(data, path));

    const selva::SaveData loaded = selva::SaveManager::load(path);
    REQUIRE(loaded.schema_version == selva::SaveData::CURRENT_VERSION);
    REQUIRE(loaded.characters.size() == 2);
    REQUIRE(loaded.characters[0].name == "ELENA");
    REQUIRE(loaded.characters[1].name == "DANTE");

    cleanupTestFile(path);
}

TEST_CASE("SaveManager load returns defaults when file missing", "[save][defaults]")
{
    const selva::SaveData data =
        selva::SaveManager::load("tmp/selva-save-tests/does-not-exist.json");
    REQUIRE(data.schema_version == selva::SaveData::CURRENT_VERSION);
    REQUIRE(data.characters.empty());
}

TEST_CASE("SaveManager deleteCharacter removes by name", "[save][delete]")
{
    const std::string path = testSavePath("delete");
    cleanupTestFile(path);

    selva::SaveData data;
    selva::SaveManager::addCharacter(data, "A");
    selva::SaveManager::addCharacter(data, "B");
    selva::SaveManager::addCharacter(data, "C");
    REQUIRE(data.characters.size() == 3);

    selva::SaveManager::deleteCharacter(data, "B");
    REQUIRE(data.characters.size() == 2);
    REQUIRE(data.characters[0].name == "A");
    REQUIRE(data.characters[1].name == "C");

    // Delete missing name: no-op.
    selva::SaveManager::deleteCharacter(data, "ZZZ");
    REQUIRE(data.characters.size() == 2);

    REQUIRE(selva::SaveManager::save(data, path));
    const selva::SaveData loaded = selva::SaveManager::load(path);
    REQUIRE(loaded.characters.size() == 2);

    cleanupTestFile(path);
}

TEST_CASE("SaveManager keeps unnamed-but-real characters on load",
          "[save][robustness][unnamed-character]")
{
    const std::string path = testSavePath("empty-names");
    cleanupTestFile(path);

    // Write a save file manually with one empty-name character and one
    // valid character. Per the unnamed-but-real-character doctrine
    // (see ClassPickerScreen.cpp and the atomic Signing chain locked
    // 2026-06-13): pre-Signing the active profile has an empty name
    // and IS a real character that must round-trip through
    // save/load. The loader keeps it. This test used to assert the
    // empty-name entry was dropped; that was a pre-doctrine
    // assumption.
    std::filesystem::create_directories(std::filesystem::path(path).parent_path());
    {
        FILE* f = std::fopen(path.c_str(), "w");
        REQUIRE(f != nullptr);
        std::fprintf(f, "{\n  \"schema_version\": 1,\n"
                        "  \"characters\": [\n"
                        "    {\"name\": \"\"},\n"
                        "    {\"name\": \"VALID\"}\n"
                        "  ]\n}\n");
        std::fclose(f);
    }

    const selva::SaveData loaded = selva::SaveManager::load(path);
    REQUIRE(loaded.characters.size() == 2);
    REQUIRE(loaded.characters[0].name.empty());
    REQUIRE(loaded.characters[1].name == "VALID");

    cleanupTestFile(path);
}

TEST_CASE("SaveManager round-trip preserves sangue fields", "[save][sangue]")
{
    const std::string path = testSavePath("sangue-roundtrip");
    cleanupTestFile(path);

    selva::SaveData data;
    selva::SaveManager::addCharacter(data, "PILGRIM");
    data.characters[0].sangue_lifetime = 1234u;
    data.characters[0].sangue_vessel = 56u;

    REQUIRE(selva::SaveManager::save(data, path));

    const selva::SaveData loaded = selva::SaveManager::load(path);
    REQUIRE(loaded.characters.size() == 1);
    REQUIRE(loaded.characters[0].sangue_lifetime == 1234u);
    REQUIRE(loaded.characters[0].sangue_vessel == 56u);

    cleanupTestFile(path);
}

TEST_CASE("SaveManager defaults sangue to zero on legacy saves", "[save][sangue]")
{
    // Pre-currency saves had no sangue fields. Load must default both
    // to 0 without crashing or rejecting the file.
    const std::string path = testSavePath("sangue-legacy");
    cleanupTestFile(path);
    std::filesystem::create_directories(std::filesystem::path(path).parent_path());

    if (std::FILE* f = std::fopen(path.c_str(), "w"))
    {
        std::fprintf(f, "{\n  \"schema_version\": 1,\n"
                        "  \"characters\": [ { \"name\": \"PILGRIM\" } ]\n}\n");
        std::fclose(f);
    }
    const selva::SaveData loaded = selva::SaveManager::load(path);
    REQUIRE(loaded.characters.size() == 1);
    REQUIRE(loaded.characters[0].sangue_lifetime == 0u);
    REQUIRE(loaded.characters[0].sangue_vessel == 0u);

    cleanupTestFile(path);
}

TEST_CASE("SaveManager round-trip preserves player_class + sangue_riversato",
          "[save][player-class]")
{
    const std::string path = testSavePath("player-class-roundtrip");
    cleanupTestFile(path);

    selva::SaveData data;
    selva::SaveManager::addCharacter(data, "PILGRIM");
    data.characters[0].player_class = selva::PlayerClass::Heretic;
    data.characters[0].sangue_riversato = 789u;

    REQUIRE(selva::SaveManager::save(data, path));

    const selva::SaveData loaded = selva::SaveManager::load(path);
    REQUIRE(loaded.characters.size() == 1);
    REQUIRE(loaded.characters[0].player_class == selva::PlayerClass::Heretic);
    REQUIRE(loaded.characters[0].sangue_riversato == 789u);

    cleanupTestFile(path);
}

TEST_CASE("SaveManager defaults player_class to None on legacy saves", "[save][player-class]")
{
    const std::string path = testSavePath("player-class-legacy");
    cleanupTestFile(path);
    std::filesystem::create_directories(std::filesystem::path(path).parent_path());

    if (std::FILE* f = std::fopen(path.c_str(), "w"))
    {
        std::fprintf(f, "{\n  \"schema_version\": 1,\n"
                        "  \"characters\": [ { \"name\": \"PILGRIM\" } ]\n}\n");
        std::fclose(f);
    }
    const selva::SaveData loaded = selva::SaveManager::load(path);
    REQUIRE(loaded.characters.size() == 1);
    REQUIRE(loaded.characters[0].player_class == selva::PlayerClass::None);
    REQUIRE(loaded.characters[0].sangue_riversato == 0u);

    cleanupTestFile(path);
}

TEST_CASE("SaveManager round-trip preserves felled_bosses", "[save][boss-backend]")
{
    const std::string path = testSavePath("felled-bosses");
    cleanupTestFile(path);

    selva::SaveData data;
    selva::SaveManager::addCharacter(data, "PILGRIM");
    data.characters[0].felled_bosses = {"lupa", "cerberus"};

    REQUIRE(selva::SaveManager::save(data, path));

    const selva::SaveData loaded = selva::SaveManager::load(path);
    REQUIRE(loaded.characters.size() == 1);
    REQUIRE(loaded.characters[0].felled_bosses.size() == 2);
    REQUIRE(loaded.characters[0].felled_bosses[0] == "lupa");
    REQUIRE(loaded.characters[0].felled_bosses[1] == "cerberus");

    cleanupTestFile(path);
}

TEST_CASE("SaveManager omits felled_bosses for fresh characters", "[save][boss-backend]")
{
    // A new character has no felled bosses; verify the field is not
    // emitted to keep save files compact, but loading either
    // representation (missing field OR empty array) is valid.
    const std::string path = testSavePath("fresh-felled");
    cleanupTestFile(path);

    selva::SaveData data;
    selva::SaveManager::addCharacter(data, "FRESH");
    REQUIRE(data.characters[0].felled_bosses.empty());

    REQUIRE(selva::SaveManager::save(data, path));

    const selva::SaveData loaded = selva::SaveManager::load(path);
    REQUIRE(loaded.characters.size() == 1);
    REQUIRE(loaded.characters[0].felled_bosses.empty());

    cleanupTestFile(path);
}

TEST_CASE("SaveManager felled_bosses load tolerates missing field",
          "[save][boss-backend][back-compat]")
{
    // Pre-boss-backend save files have no felled_bosses field at all.
    // Loader must default to empty list (not crash, not throw).
    const std::string path = testSavePath("pre-boss-backend");
    cleanupTestFile(path);

    std::filesystem::create_directories(std::filesystem::path(path).parent_path());
    {
        FILE* f = std::fopen(path.c_str(), "w");
        REQUIRE(f != nullptr);
        std::fprintf(f, "{\n  \"schema_version\": 1,\n"
                        "  \"characters\": [\n"
                        "    {\"name\": \"LEGACY\", \"pos_x\": 1.0}\n"
                        "  ]\n}\n");
        std::fclose(f);
    }

    const selva::SaveData loaded = selva::SaveManager::load(path);
    REQUIRE(loaded.characters.size() == 1);
    REQUIRE(loaded.characters[0].name == "LEGACY");
    REQUIRE(loaded.characters[0].felled_bosses.empty());

    cleanupTestFile(path);
}

TEST_CASE("SaveManager migrate sets schema_version to current", "[save][migrate]")
{
    selva::SaveData data;
    data.schema_version = 0; // Simulate an older save.
    selva::SaveManager::migrate(data);
    REQUIRE(data.schema_version == selva::SaveData::CURRENT_VERSION);
}

TEST_CASE("SaveManager round-trips active_gather_nodes preserving all per-node fields",
          "[save][gather][round-trip]")
{
    const std::string path = testSavePath("gather-nodes-roundtrip");
    cleanupTestFile(path);

    selva::SaveData data;
    selva::SaveManager::addCharacter(data, "PILGRIM");
    auto& profile = data.characters[0];
    profile.next_gather_node_id = 7;

    selva::gather::NodeState a;
    a.id = 4;
    a.node_config_path = "config/gather_nodes/wood_forage.json";
    a.material_config_path = "config/items/materials/bark_scrap.json";
    a.pos_x = 12.5f;
    a.pos_y = -0.25f;
    a.pos_z = -180.0f;
    a.quality = engine::ecs::QualityTier::Fine;
    a.yaw = 1.75f;
    profile.active_gather_nodes.push_back(a);

    selva::gather::NodeState b;
    b.id = 6;
    b.node_config_path = "config/gather_nodes/wood_forage.json";
    b.material_config_path = "config/items/materials/pale_lichen.json";
    b.pos_x = -34.0f;
    b.pos_y = 0.12f;
    b.pos_z = -220.5f;
    b.quality = engine::ecs::QualityTier::Masterwork;
    b.yaw = 4.20f;
    profile.active_gather_nodes.push_back(b);

    REQUIRE(selva::SaveManager::save(data, path));

    const selva::SaveData loaded = selva::SaveManager::load(path);
    REQUIRE(loaded.characters.size() == 1);
    const auto& lp = loaded.characters[0];
    REQUIRE(lp.next_gather_node_id == 7u);
    REQUIRE(lp.active_gather_nodes.size() == 2);

    const auto& la = lp.active_gather_nodes[0];
    REQUIRE(la.id == 4u);
    REQUIRE(la.node_config_path == "config/gather_nodes/wood_forage.json");
    REQUIRE(la.material_config_path == "config/items/materials/bark_scrap.json");
    REQUIRE(la.pos_x == 12.5f);
    REQUIRE(la.pos_y == -0.25f);
    REQUIRE(la.pos_z == -180.0f);
    REQUIRE(la.quality == engine::ecs::QualityTier::Fine);
    REQUIRE(la.yaw == 1.75f);

    const auto& lb = lp.active_gather_nodes[1];
    REQUIRE(lb.material_config_path == "config/items/materials/pale_lichen.json");
    REQUIRE(lb.quality == engine::ecs::QualityTier::Masterwork);
    REQUIRE(lb.yaw == 4.20f);

    cleanupTestFile(path);
}

TEST_CASE("SaveManager round-trips gather_flows preserving initial_fill_done + timer",
          "[save][gather][flows]")
{
    const std::string path = testSavePath("gather-flows-roundtrip");
    cleanupTestFile(path);

    selva::SaveData data;
    selva::SaveManager::addCharacter(data, "PILGRIM");
    auto& profile = data.characters[0];

    selva::gather::FlowState f;
    f.node_config_path = "config/gather_nodes/wood_forage.json";
    f.last_spawn_wallclock = 123.456f;
    f.initial_fill_done = true;
    profile.gather_flows.push_back(f);

    REQUIRE(selva::SaveManager::save(data, path));

    const selva::SaveData loaded = selva::SaveManager::load(path);
    REQUIRE(loaded.characters[0].gather_flows.size() == 1);
    const auto& lf = loaded.characters[0].gather_flows[0];
    REQUIRE(lf.node_config_path == "config/gather_nodes/wood_forage.json");
    REQUIRE(lf.last_spawn_wallclock == 123.456f);
    REQUIRE(lf.initial_fill_done == true);

    cleanupTestFile(path);
}

TEST_CASE("SaveManager round-trips quick_slot rotation + primed index",
          "[save][quick-slot][round-trip]")
{
    const std::string path = testSavePath("quickslot-roundtrip");
    cleanupTestFile(path);

    selva::SaveData data;
    selva::SaveManager::addCharacter(data, "PILGRIM");
    auto& p = data.characters[0];
    p.quick_slot_assigned.push_back("config/items/consumables/poultice.json");
    p.quick_slot_assigned.push_back("config/items/consumables/salve.json");
    p.quick_slot_primed_index = 1;

    REQUIRE(selva::SaveManager::save(data, path));
    const selva::SaveData loaded = selva::SaveManager::load(path);
    REQUIRE(loaded.characters[0].quick_slot_assigned.size() == 2);
    REQUIRE(loaded.characters[0].quick_slot_assigned[0] ==
            "config/items/consumables/poultice.json");
    REQUIRE(loaded.characters[0].quick_slot_assigned[1] ==
            "config/items/consumables/salve.json");
    REQUIRE(loaded.characters[0].quick_slot_primed_index == 1);

    cleanupTestFile(path);
}

TEST_CASE("SaveManager defaults quick_slot fields on legacy v5 saves",
          "[save][quick-slot][migration]")
{
    const std::string path = testSavePath("quickslot-legacy-v5");
    cleanupTestFile(path);
    std::filesystem::create_directories(std::filesystem::path(path).parent_path());

    if (std::FILE* f = std::fopen(path.c_str(), "w"))
    {
        std::fprintf(f, "{\n  \"schema_version\": 5,\n"
                        "  \"characters\": [ { \"name\": \"PILGRIM\" } ]\n}\n");
        std::fclose(f);
    }
    const selva::SaveData loaded = selva::SaveManager::load(path);
    REQUIRE(loaded.characters[0].quick_slot_assigned.empty());
    REQUIRE(loaded.characters[0].quick_slot_primed_index == -1);

    cleanupTestFile(path);
}

TEST_CASE("SaveManager round-trips auto_assign_consumables setting",
          "[save][settings][quick-slot]")
{
    const std::string path = testSavePath("autoassign-setting");
    cleanupTestFile(path);

    selva::SaveData data;
    data.settings.auto_assign_consumables_to_quick_slot = true;
    REQUIRE(selva::SaveManager::save(data, path));
    const selva::SaveData loaded = selva::SaveManager::load(path);
    REQUIRE(loaded.settings.auto_assign_consumables_to_quick_slot == true);

    cleanupTestFile(path);
}

TEST_CASE("SaveManager round-trips craft_counts and known_recipes",
          "[save][craft][round-trip]")
{
    const std::string path = testSavePath("craft-progression-roundtrip");
    cleanupTestFile(path);

    selva::SaveData data;
    selva::SaveManager::addCharacter(data, "PILGRIM");
    auto& p = data.characters[0];
    p.craft_counts["config/recipes/craft_poultice.json"] = 9;
    p.craft_counts["config/recipes/craft_salve.json"] = 2;
    p.known_recipes.push_back("config/recipes/craft_poultice.json");
    p.known_recipes.push_back("config/recipes/craft_salve.json");

    REQUIRE(selva::SaveManager::save(data, path));

    const selva::SaveData loaded = selva::SaveManager::load(path);
    REQUIRE(loaded.characters.size() == 1);
    const auto& lp = loaded.characters[0];
    REQUIRE(lp.craft_counts.at("config/recipes/craft_poultice.json") == 9u);
    REQUIRE(lp.craft_counts.at("config/recipes/craft_salve.json") == 2u);
    REQUIRE(lp.known_recipes.size() == 2);
    REQUIRE(lp.known_recipes[0] == "config/recipes/craft_poultice.json");
    REQUIRE(lp.known_recipes[1] == "config/recipes/craft_salve.json");

    cleanupTestFile(path);
}

TEST_CASE("SaveManager defaults gather state to empty on legacy v4 saves",
          "[save][gather][migration]")
{
    // v4 saves have no active_gather_nodes / gather_flows fields.
    // Load must default both to empty without crashing or rejecting.
    const std::string path = testSavePath("gather-legacy-v4");
    cleanupTestFile(path);
    std::filesystem::create_directories(std::filesystem::path(path).parent_path());

    if (std::FILE* f = std::fopen(path.c_str(), "w"))
    {
        std::fprintf(f, "{\n  \"schema_version\": 4,\n"
                        "  \"characters\": [ { \"name\": \"PILGRIM\" } ]\n}\n");
        std::fclose(f);
    }
    const selva::SaveData loaded = selva::SaveManager::load(path);
    REQUIRE(loaded.characters.size() == 1);
    REQUIRE(loaded.characters[0].active_gather_nodes.empty());
    REQUIRE(loaded.characters[0].gather_flows.empty());
    REQUIRE(loaded.characters[0].next_gather_node_id == 1u);

    cleanupTestFile(path);
}
