// Schema-validation tests for the region-bootstrap data contract.
//
// PURPOSE: this test class catches the bug class where a rename of a JSON
// key in code drifts from the actual data file -- the loader silently
// falls back to a default (empty array, empty string), the world boots
// into a broken state, the player falls through the ground.
//
// This bit us in the engine Scene -> Region rename: code read
// `value("scenes", ...)` after I'd renamed the JSON key to `"regions"`,
// the loader silently iterated over an empty array, no physics bodies
// got registered, the player fell through.
//
// The tests below assert the JSON files on disk contain the exact keys
// the C++ loader reads. They DO NOT exercise GL or Jolt; they just
// parse the JSON and check the schema.
//
// Working directory is build/bin/selva-oscura/ (set by CMake on the test
// target), so relative "assets/regions/..." paths resolve to the synced
// copy of the asset tree.

#include <nlohmann/json.hpp>

#include <fstream>
#include <string>

#include <catch2/catch_test_macros.hpp>

namespace
{
nlohmann::json loadJson(const std::string& path)
{
    std::ifstream f(path);
    REQUIRE(f.is_open());
    nlohmann::json out;
    f >> out;
    return out;
}
} // namespace

TEST_CASE("regions.json has required top-level keys", "[region-schema]")
{
    const auto j = loadJson("assets/regions/regions.json");
    REQUIRE(j.contains("regions"));
    REQUIRE(j.at("regions").is_array());
    REQUIRE_FALSE(j.at("regions").empty());
    REQUIRE(j.contains("default_spawn_region"));
    REQUIRE(j.at("default_spawn_region").is_string());
}

TEST_CASE("regions.json default_spawn_region exists in regions list", "[region-schema]")
{
    const auto j = loadJson("assets/regions/regions.json");
    const auto default_spawn = j.at("default_spawn_region").get<std::string>();
    bool found = false;
    for (const auto& r : j.at("regions"))
    {
        if (r.get<std::string>() == default_spawn)
        {
            found = true;
            break;
        }
    }
    REQUIRE(found);
}

TEST_CASE("every region in regions.json has a loadable region.json with required keys",
          "[region-schema]")
{
    const auto registry = loadJson("assets/regions/regions.json");
    for (const auto& sid_val : registry.at("regions"))
    {
        const std::string sid = sid_val.get<std::string>();
        const std::string path = "assets/regions/" + sid + "/region.json";

        // The region's individual file must be loadable.
        const auto region = loadJson(path);

        // region_id is REQUIRED (used by findRegionId / RegionId resolution).
        // Missing this means the region constructs with an empty ID and
        // can never be looked up; the world boots into nothing.
        REQUIRE(region.contains("region_id"));
        REQUIRE(region.at("region_id").is_string());
        REQUIRE_FALSE(region.at("region_id").get<std::string>().empty());

        // region_id in the file must match the folder name (the registry's
        // contract is that "regions": ["surface"] means assets/regions/
        // surface/region.json declares region_id="surface").
        REQUIRE(region.at("region_id").get<std::string>() == sid);
    }
}

TEST_CASE("surface region has at least one static_mesh entry", "[region-schema]")
{
    // The surface region is the entire playable world in v1; if its
    // static_meshes array is empty, no chapel renders. This test fires
    // on any rename / refactor that breaks the static_meshes key.
    const auto region = loadJson("assets/regions/surface/region.json");
    REQUIRE(region.contains("static_meshes"));
    REQUIRE(region.at("static_meshes").is_array());
    REQUIRE_FALSE(region.at("static_meshes").empty());

    // Each static_mesh must declare a path. Without it, the loader
    // silently skips the mesh.
    for (const auto& m : region.at("static_meshes"))
    {
        REQUIRE(m.contains("path"));
        REQUIRE(m.at("path").is_string());
        REQUIRE_FALSE(m.at("path").get<std::string>().empty());
    }
}

TEST_CASE("tree-asset glTF file exists at the path TreeAssets.cpp reads", "[region-schema]")
{
    // TreeAssets.cpp reads "assets/world/trees/low_poly_forest_tree_pack/
    // scene.gltf". If this filename ever gets renamed (e.g. a future
    // engine concept rename sweeping through), trees silently vanish.
    // This test ensures the filename the code expects is the filename
    // on disk.
    std::ifstream f("assets/world/trees/low_poly_forest_tree_pack/scene.gltf");
    REQUIRE(f.is_open());
}
