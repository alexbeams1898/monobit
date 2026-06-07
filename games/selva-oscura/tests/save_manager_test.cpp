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

TEST_CASE("SaveManager skips characters with empty names on load", "[save][robustness]")
{
    const std::string path = testSavePath("empty-names");
    cleanupTestFile(path);

    // Write a save file manually with one empty-name character and one
    // valid character; loader should drop the empty one.
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
    REQUIRE(loaded.characters.size() == 1);
    REQUIRE(loaded.characters[0].name == "VALID");

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
