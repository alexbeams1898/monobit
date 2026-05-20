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

TEST_CASE("SaveManager migrate sets schema_version to current", "[save][migrate]")
{
    selva::SaveData data;
    data.schema_version = 0; // Simulate an older save.
    selva::SaveManager::migrate(data);
    REQUIRE(data.schema_version == selva::SaveData::CURRENT_VERSION);
}
