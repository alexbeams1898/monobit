#include "utils/SaveFile.h"

#include <nlohmann/json.hpp>

#include <filesystem>
#include <fstream>

#include <catch2/catch_test_macros.hpp>

namespace
{
namespace fs = std::filesystem;

// A scratch directory, removed on scope exit so tests don't leak files or see
// each other's writes.
struct TempDir
{
    fs::path root;

    explicit TempDir(const char* name) : root(fs::temp_directory_path() / "engine_save_test" / name)
    {
        fs::remove_all(root);
    }
    ~TempDir()
    {
        std::error_code ec;
        fs::remove_all(root, ec);
    }
    std::string file(const char* name) const
    {
        return (root / name).string();
    }
};
} // namespace

TEST_CASE("dir nests org/app and ends with a separator", "[save]")
{
    const std::string d = engine::save::dir("MonobitTestOrg", "MonobitTestApp");
    REQUIRE_FALSE(d.empty());
    // Trailing separator is part of the contract -- path() concatenates onto it.
    const char back = d.back();
    REQUIRE((back == '/' || back == '\\'));
    // The platform nests them: both parts appear, org before app.
    const auto orgAt = d.find("MonobitTestOrg");
    const auto appAt = d.find("MonobitTestApp");
    REQUIRE(orgAt != std::string::npos);
    REQUIRE(appAt != std::string::npos);
    REQUIRE(orgAt < appAt);
}

TEST_CASE("dir is stable across calls (cached)", "[save]")
{
    REQUIRE(engine::save::dir("MonobitTestOrg", "MonobitTestApp") ==
            engine::save::dir("MonobitTestOrg", "MonobitTestApp"));
}

TEST_CASE("dir keys on the pair, not just the app", "[save]")
{
    // Two orgs sharing an app name must not collide in the cache.
    REQUIRE(engine::save::dir("OrgOne", "SharedApp") != engine::save::dir("OrgTwo", "SharedApp"));
}

TEST_CASE("path appends the file name to dir", "[save]")
{
    const std::string d = engine::save::dir("MonobitTestOrg", "MonobitTestApp");
    REQUIRE(engine::save::path("MonobitTestOrg", "MonobitTestApp") == d + "save.json");
    REQUIRE(engine::save::path("MonobitTestOrg", "MonobitTestApp", "other.json") ==
            d + "other.json");
}

TEST_CASE("writeJson creates missing parent directories", "[save]")
{
    const TempDir tmp("nested");
    // Two levels that don't exist yet -- writing must make them rather than fail.
    const std::string target = (tmp.root / "a" / "b" / "save.json").string();

    nlohmann::json doc;
    doc["hello"] = "world";
    REQUIRE(engine::save::writeJson(doc, target));
    REQUIRE(fs::exists(target));
}

TEST_CASE("writeJson then readJson round-trips a document", "[save]")
{
    const TempDir tmp("roundtrip");
    nlohmann::json doc;
    doc["n"] = 42;
    doc["s"] = "text";
    doc["nested"] = {{"flag", true}};
    REQUIRE(engine::save::writeJson(doc, tmp.file("save.json")));

    const auto read = engine::save::readJson(tmp.file("save.json"));
    REQUIRE(read.has_value());
    // REQUIRE above aborts on a missing optional, but the analyser cannot see
    // Catch2 do it -- and it rejects value() as readily as operator*. value_or
    // needs no proof: the fallback is unreachable precisely because REQUIRE
    // already passed.
    const nlohmann::json loaded = read.value_or(nlohmann::json::object());
    REQUIRE(loaded["n"] == 42);
    REQUIRE(loaded["s"] == "text");
    REQUIRE(loaded["nested"]["flag"] == true);
}

TEST_CASE("readJson of a missing file is nullopt, not an error", "[save]")
{
    const TempDir tmp("missing");
    REQUIRE_FALSE(engine::save::readJson(tmp.file("nope.json")).has_value());
}

TEST_CASE("readJson of a corrupt file is nullopt, not a throw", "[save]")
{
    // A truncated/garbled save must read as "no save" rather than take down the boot.
    const TempDir tmp("corrupt");
    fs::create_directories(tmp.root);
    std::ofstream(tmp.file("save.json")) << "{ not valid json at all";
    REQUIRE_FALSE(engine::save::readJson(tmp.file("save.json")).has_value());
}
