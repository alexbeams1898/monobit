#include "ecs/Components.h"
#include "ecs/EntityManager.h"
#include "ecs/GameComponents.h"
#include "ops/RecordOps.h"
#include "systems/DamageSystem.h"
#include "systems/PlayerSystem.h"

#include <catch2/catch_test_macros.hpp>
#include <entt/entt.hpp>

// The record's promises: a death lands on it exactly once, at the moment the body is really
// gone; a new job wipes it; a species never killed reads zero.

namespace
{
constexpr const char* kAnt = "config/pests/ant.json";

// The player must exist for the reap's payout hooks; a bare body with no tank or sheet is
// enough for them to no-op safely.
void bindBarePlayer(EntityManager& em)
{
    const entt::entity p = em.registry().create();
    em.registry().emplace<Transform>(p, Transform{});
    player::bind(p);
}

// A body already past the fatal blow, waiting only for its death flash to run out.
entt::entity makeDyingPest(EntityManager& em, const char* species)
{
    auto& reg = em.registry();
    const entt::entity e = reg.create();
    reg.emplace<Transform>(e, Transform{});
    reg.emplace<Health>(e, Health{0, 5});
    reg.emplace<Pest>(e, Pest{});
    reg.emplace<Species>(e, Species{species});
    reg.emplace<Dying>(e, Dying{0.01f});
    return e;
}
} // namespace

TEST_CASE("a reaped body lands on the record exactly once", "[record]")
{
    record::reset();
    EntityManager em;
    bindBarePlayer(em);
    const entt::entity e = makeDyingPest(em, kAnt);

    hit_area::update(em, 0.1f); // outlives the death flash: the body is reaped
    REQUIRE_FALSE(em.registry().valid(e));
    CHECK(record::kills(kAnt) == 1);

    // Nothing left to reap: further updates cannot count the same death again.
    hit_area::update(em, 0.1f);
    CHECK(record::kills(kAnt) == 1);
}

TEST_CASE("deaths accumulate per species", "[record]")
{
    record::reset();
    EntityManager em;
    bindBarePlayer(em);
    makeDyingPest(em, kAnt);
    makeDyingPest(em, kAnt);

    hit_area::update(em, 0.1f);
    CHECK(record::kills(kAnt) == 2);
    CHECK(record::all().size() == 1);
}

TEST_CASE("reset clears the record", "[record]")
{
    record::reset();
    record::countKill(kAnt);
    REQUIRE(record::kills(kAnt) == 1);

    record::reset();
    CHECK(record::kills(kAnt) == 0);
    CHECK(record::all().empty());
}

TEST_CASE("an unknown species reads zero", "[record]")
{
    record::reset();
    CHECK(record::kills("config/pests/never-met.json") == 0);
    // Asking must not invent an entry -- the listing shows only what has actually died.
    CHECK(record::all().empty());
}
