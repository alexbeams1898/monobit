// Territory clamp form gate: only DamnedSoul-form actors are subject
// to the ring-bound clamp; all other forms move freely across region
// boundaries. Regression for the Guide-disappears-walking-outside bug
// (2026-06-06): the Guide (UnjudgedSoul) was getting clamped on his
// first step out of the chapel and teleported ~192m down to the
// surface territory's bottom Y face.
//
// Doctrine: territory clamp is Hell's measurement machinery. Hell's
// measurement only grips imprinted damned souls. UnjudgedSoul, Animal,
// HellMachinery, Divine all carry their own movement authority. Per
// [[project_territory_system_doctrine]] + the five-form taxonomy +
// [[project_imprint_handle_required_for_sangue]].
//
// Pure-compute test: register two synthetic territories, place actors
// at positions that classify as foreign for their spawn_region, run
// the clamp, assert the form-gate selects who gets pushed.

#include "gameplay/Actor.h"
#include "gameplay/Faction.h"
#include "gameplay/TerritoryClamp.h"
#include "world/Territory.h"

#include <catch2/catch_test_macros.hpp>

namespace
{

// Two synthetic territories at the test scale. Same shape as the
// Selva chapel-vs-surface setup that surfaced the bug, but tiny
// numbers so the test reads in one screen. The "limbo_test" volume
// nests inside the "surface_test" volume so a point at (0,0,0)
// resolves to limbo_test by smallest-volume tie-break.
void registerTestTerritories()
{
    // Outer surface volume.
    engine::world::Territory surface;
    surface.center = glm::vec3(0.0f, 0.0f, 0.0f);
    surface.half_extents = glm::vec3(100.0f, 100.0f, 100.0f);
    surface.owner_region_id = "surface_test";
    surface.debug_name = "surface_test_main";
    engine::world::registerTerritory(surface);
    // Inner chapel volume nested inside surface. Smaller-volume wins.
    engine::world::Territory chapel;
    chapel.center = glm::vec3(0.0f, 0.0f, 0.0f);
    chapel.half_extents = glm::vec3(10.0f, 10.0f, 10.0f);
    chapel.owner_region_id = "chapel_test";
    chapel.debug_name = "chapel_test_main";
    engine::world::registerTerritory(chapel);
}

// Append an actor at a position with the given spawn_region + form.
// is_boss flag has no effect on the clamp (it's purely form-gated).
std::size_t pushActor(const glm::vec3& pos, const std::string& spawn_region,
                      selva::gameplay::Form form)
{
    selva::gameplay::Actor a;
    a.pos = pos;
    a.spawn_region_id = spawn_region;
    a.form = form;
    selva::gameplay::actors().push_back(std::move(a));
    return selva::gameplay::actors().size() - 1;
}

void clearPool()
{
    selva::gameplay::actors().clear();
}

} // namespace

TEST_CASE("DamnedSoul in foreign territory gets pushed out", "[territory-clamp][form-gate]")
{
    registerTestTerritories();
    clearPool();
    // A DamnedSoul authored to chapel_test, currently at (50, 0, 0) --
    // that's inside surface_test (foreign). The clamp must push it.
    const auto idx =
        pushActor(glm::vec3(50.0f, 0.0f, 0.0f), "chapel_test", selva::gameplay::Form::DamnedSoul);
    selva::gameplay::clampActorsToOwnTerritory();
    REQUIRE(selva::gameplay::actors()[idx].pos.x != 50.0f);
    clearPool();
}

TEST_CASE("UnjudgedSoul in foreign territory is NOT clamped (Guide regression)",
          "[territory-clamp][form-gate]")
{
    registerTestTerritories();
    clearPool();
    // The Guide case: UnjudgedSoul authored to chapel_test, walks out
    // into surface_test territory. Clamp must NOT touch him.
    const glm::vec3 start_pos(50.0f, 0.0f, 0.0f);
    const auto idx = pushActor(start_pos, "chapel_test", selva::gameplay::Form::UnjudgedSoul);
    selva::gameplay::clampActorsToOwnTerritory();
    REQUIRE(selva::gameplay::actors()[idx].pos == start_pos);
    clearPool();
}

TEST_CASE("Animal in foreign territory is NOT clamped (Lupa free movement)",
          "[territory-clamp][form-gate]")
{
    registerTestTerritories();
    clearPool();
    const glm::vec3 start_pos(50.0f, 0.0f, 0.0f);
    const auto idx = pushActor(start_pos, "chapel_test", selva::gameplay::Form::Animal);
    selva::gameplay::clampActorsToOwnTerritory();
    REQUIRE(selva::gameplay::actors()[idx].pos == start_pos);
    clearPool();
}

TEST_CASE("HellMachinery in foreign territory is NOT clamped (keeper free movement)",
          "[territory-clamp][form-gate]")
{
    registerTestTerritories();
    clearPool();
    const glm::vec3 start_pos(50.0f, 0.0f, 0.0f);
    const auto idx = pushActor(start_pos, "chapel_test", selva::gameplay::Form::HellMachinery);
    selva::gameplay::clampActorsToOwnTerritory();
    REQUIRE(selva::gameplay::actors()[idx].pos == start_pos);
    clearPool();
}

TEST_CASE("DamnedSoul in own territory is not touched", "[territory-clamp][form-gate]")
{
    registerTestTerritories();
    clearPool();
    // Inside chapel_test (the nested volume wins by smallest-volume),
    // authored to chapel_test. No-op.
    const glm::vec3 start_pos(5.0f, 0.0f, 0.0f);
    const auto idx = pushActor(start_pos, "chapel_test", selva::gameplay::Form::DamnedSoul);
    selva::gameplay::clampActorsToOwnTerritory();
    REQUIRE(selva::gameplay::actors()[idx].pos == start_pos);
    clearPool();
}

TEST_CASE("Dead DamnedSoul is not clamped (corpses stay where they fell)",
          "[territory-clamp][form-gate]")
{
    registerTestTerritories();
    clearPool();
    const glm::vec3 start_pos(50.0f, 0.0f, 0.0f);
    const auto idx = pushActor(start_pos, "chapel_test", selva::gameplay::Form::DamnedSoul);
    selva::gameplay::actors()[idx].is_dead = true;
    selva::gameplay::clampActorsToOwnTerritory();
    REQUIRE(selva::gameplay::actors()[idx].pos == start_pos);
    clearPool();
}

TEST_CASE("Actor without spawn_region_id is not clamped", "[territory-clamp][form-gate]")
{
    registerTestTerritories();
    clearPool();
    // Flow-spawned or test-spawned actors with no region binding are
    // intentionally region-free; the clamp must skip them.
    const glm::vec3 start_pos(50.0f, 0.0f, 0.0f);
    const auto idx = pushActor(start_pos, "", selva::gameplay::Form::DamnedSoul);
    selva::gameplay::clampActorsToOwnTerritory();
    REQUIRE(selva::gameplay::actors()[idx].pos == start_pos);
    clearPool();
}

TEST_CASE("Player (Controller::Input) is not clamped", "[territory-clamp][form-gate]")
{
    registerTestTerritories();
    clearPool();
    const glm::vec3 start_pos(50.0f, 0.0f, 0.0f);
    const auto idx = pushActor(start_pos, "chapel_test", selva::gameplay::Form::DamnedSoul);
    selva::gameplay::actors()[idx].controller = selva::gameplay::Controller::Input;
    selva::gameplay::clampActorsToOwnTerritory();
    REQUIRE(selva::gameplay::actors()[idx].pos == start_pos);
    clearPool();
}
