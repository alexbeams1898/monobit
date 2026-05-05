#include "SaveManager.h"
#include "ops/UpdateChecker.h"

#include <catch2/catch_test_macros.hpp>

TEST_CASE("isNewer compares semver correctly", "[update]")
{
    REQUIRE(UpdateChecker::isNewer("0.1.0", "0.2.0"));
    REQUIRE(UpdateChecker::isNewer("0.1.0", "0.1.1"));
    REQUIRE(UpdateChecker::isNewer("0.1.0", "1.0.0"));
    REQUIRE(UpdateChecker::isNewer("0.9.9", "0.10.0"));
    REQUIRE(UpdateChecker::isNewer("0.1.0", "0.1.0") == false);
    REQUIRE(UpdateChecker::isNewer("0.2.0", "0.1.0") == false);
    REQUIRE(UpdateChecker::isNewer("1.0.0", "0.99.99") == false);
}

TEST_CASE("isNewer handles edge cases", "[update]")
{
    REQUIRE(UpdateChecker::isNewer("0.0.0", "0.0.1"));
    REQUIRE(UpdateChecker::isNewer("", "0.1.0"));
    REQUIRE(UpdateChecker::isNewer("0.1.0", "") == false);
}

// ---------------------------------------------------------------------------
// parseAssetDownloadUrl
// ---------------------------------------------------------------------------

TEST_CASE("parseAssetDownloadUrl finds zip URL", "[update]")
{
    const std::string json = R"({
        "tag_name": "v0.2.0",
        "assets": [
            {
                "name": "prison-escape-game-v0.2.0.zip",
                "browser_download_url": "https://github.com/user/repo/releases/download/v0.2.0/prison-escape-game-v0.2.0.zip"
            }
        ]
    })";
    const auto url = UpdateChecker::parseAssetDownloadUrl(json);
    REQUIRE(url ==
            "https://github.com/user/repo/releases/download/v0.2.0/prison-escape-game-v0.2.0.zip");
}

TEST_CASE("parseAssetDownloadUrl picks first zip among multiple assets", "[update]")
{
    const std::string json = R"({
        "assets": [
            { "name": "checksums.txt", "browser_download_url": "https://example.com/checksums.txt" },
            { "name": "game-v1.0.zip", "browser_download_url": "https://example.com/game-v1.0.zip" },
            { "name": "source.zip",    "browser_download_url": "https://example.com/source.zip" }
        ]
    })";
    REQUIRE(UpdateChecker::parseAssetDownloadUrl(json) == "https://example.com/game-v1.0.zip");
}

TEST_CASE("parseAssetDownloadUrl returns empty for no zip asset", "[update]")
{
    const std::string json = R"({
        "assets": [
            { "name": "readme.txt", "browser_download_url": "https://example.com/readme.txt" }
        ]
    })";
    REQUIRE(UpdateChecker::parseAssetDownloadUrl(json).empty());
}

TEST_CASE("parseAssetDownloadUrl returns empty for empty assets array", "[update]")
{
    REQUIRE(UpdateChecker::parseAssetDownloadUrl(R"({"assets":[]})").empty());
}

TEST_CASE("parseAssetDownloadUrl returns empty for malformed JSON", "[update]")
{
    REQUIRE(UpdateChecker::parseAssetDownloadUrl("not json at all").empty());
    REQUIRE(UpdateChecker::parseAssetDownloadUrl("").empty());
}

// ---------------------------------------------------------------------------
// stripTopLevelDir
// ---------------------------------------------------------------------------

TEST_CASE("stripTopLevelDir removes matching prefix", "[update]")
{
    REQUIRE(UpdateChecker::stripTopLevelDir("game-v1.0/bin/game.exe", "game-v1.0/") ==
            "bin/game.exe");
}

TEST_CASE("stripTopLevelDir returns path unchanged without prefix", "[update]")
{
    REQUIRE(UpdateChecker::stripTopLevelDir("other/file.txt", "game-v1.0/") == "other/file.txt");
}

TEST_CASE("stripTopLevelDir returns path unchanged with empty prefix", "[update]")
{
    REQUIRE(UpdateChecker::stripTopLevelDir("some/path.txt", "") == "some/path.txt");
}

// ---------------------------------------------------------------------------
// formatBytes
// ---------------------------------------------------------------------------

TEST_CASE("formatBytes formats byte ranges", "[update]")
{
    REQUIRE(UpdateChecker::formatBytes(0) == "0 B");
    REQUIRE(UpdateChecker::formatBytes(512) == "512 B");
    REQUIRE(UpdateChecker::formatBytes(1023) == "1023 B");
}

TEST_CASE("formatBytes formats KB range", "[update]")
{
    REQUIRE(UpdateChecker::formatBytes(1024) == "1.0 KB");
    REQUIRE(UpdateChecker::formatBytes(1536) == "1.5 KB");
    REQUIRE(UpdateChecker::formatBytes(10240) == "10.0 KB");
}

TEST_CASE("formatBytes formats MB range", "[update]")
{
    REQUIRE(UpdateChecker::formatBytes(1048576) == "1.0 MB");
    REQUIRE(UpdateChecker::formatBytes(12582912) == "12.0 MB");
    REQUIRE(UpdateChecker::formatBytes(1572864) == "1.5 MB");
}

// ---------------------------------------------------------------------------
// Save path tests
// ---------------------------------------------------------------------------

TEST_CASE("getSaveDir returns non-empty path", "[save]")
{
    const std::string dir = SaveManager::getSaveDir();
    REQUIRE_FALSE(dir.empty());
}

TEST_CASE("defaultSavePath ends with save.json", "[save]")
{
    const std::string path = SaveManager::defaultSavePath();
    REQUIRE(path.size() > 9);
    REQUIRE(path.substr(path.size() - 9) == "save.json");
}
