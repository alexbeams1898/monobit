// Boss-dispatch verb idempotence: trigger re-fires while a boss is
// past Dormant must be no-ops. Regression for the Lupa-decline-too-slow
// bug (2026-06-06): the engage trigger volume re-firing during her
// scripted-death pain stage flipped boss_state Dying -> Engaged,
// cancelling the pain phase and pushing real death out by the
// scripted_death_seconds deadline (30s) instead of pain_duration (4s).
//
// Doctrine: dispatch verbs are state-setters that compose. Second-time
// entry for an id already in the pool is a no-op regardless of current
// state. Per [[feedback_spawn_systems_need_idempotency]] applied at
// the dispatch layer.
//
// Pure-compute test against the actor pool. No archetype, no audio,
// no region -- the idempotence check runs BEFORE any state mutation,
// so the test exercises only that guard.

#include "gameplay/Actor.h"
#include "gameplay/BossDispatcher.h"
#include "gameplay/BossState.h"

#include <catch2/catch_test_macros.hpp>

namespace
{

// Append a fresh boss actor to the pool with the given decl id +
// initial state. Returns its index. Caller is responsible for
// clearing the pool between cases.
std::size_t pushBoss(const std::string& spawn_decl_id, selva::gameplay::BossState state,
                     bool dead = false)
{
    selva::gameplay::Actor a;
    a.spawn_decl_id = spawn_decl_id;
    a.is_boss = true;
    a.is_dead = dead;
    a.boss_state = state;
    selva::gameplay::actors().push_back(std::move(a));
    return selva::gameplay::actors().size() - 1;
}

void clearPool()
{
    selva::gameplay::actors().clear();
}

} // namespace

TEST_CASE("engage verb is a no-op when boss is Dying", "[boss-dispatch][idempotence]")
{
    clearPool();
    const auto idx = pushBoss("lupa", selva::gameplay::BossState::Dying);
    selva::gameplay::handleEngageVerb("lupa");
    REQUIRE(selva::gameplay::actors()[idx].boss_state == selva::gameplay::BossState::Dying);
    clearPool();
}

TEST_CASE("engage verb is a no-op when boss is already Engaged", "[boss-dispatch][idempotence]")
{
    clearPool();
    const auto idx = pushBoss("lupa", selva::gameplay::BossState::Engaged);
    selva::gameplay::handleEngageVerb("lupa");
    REQUIRE(selva::gameplay::actors()[idx].boss_state == selva::gameplay::BossState::Engaged);
    clearPool();
}

TEST_CASE("engage verb is a no-op when boss is Felled", "[boss-dispatch][idempotence]")
{
    clearPool();
    pushBoss("lupa", selva::gameplay::BossState::Felled, /*dead=*/true);
    selva::gameplay::handleEngageVerb("lupa");
    // is_dead=true filters the boss out of findBossInPool(include_dead=false),
    // so the verb logs "no matching alive boss" and returns -- state
    // unchanged.
    REQUIRE(selva::gameplay::actors()[0].boss_state == selva::gameplay::BossState::Felled);
    REQUIRE(selva::gameplay::actors()[0].is_dead);
    clearPool();
}

TEST_CASE("spawn verb is a no-op when boss already in pool", "[boss-dispatch][idempotence]")
{
    clearPool();
    pushBoss("lupa", selva::gameplay::BossState::Engaged);
    const auto pool_size_before = selva::gameplay::actors().size();
    selva::gameplay::handleSpawnVerb("lupa");
    // Idempotence check returns before findCurrentRegionSpawnDecl, so
    // no new actor appears. Existing actor's state is untouched.
    REQUIRE(selva::gameplay::actors().size() == pool_size_before);
    REQUIRE(selva::gameplay::actors()[0].boss_state == selva::gameplay::BossState::Engaged);
    clearPool();
}

TEST_CASE("spawn verb is a no-op when felled boss in pool", "[boss-dispatch][idempotence]")
{
    clearPool();
    pushBoss("lupa", selva::gameplay::BossState::Felled, /*dead=*/true);
    const auto pool_size_before = selva::gameplay::actors().size();
    selva::gameplay::handleSpawnVerb("lupa");
    // include_dead=true on the spawn-side check -- a dead boss in the
    // pool means "this id has been here; don't duplicate." Important
    // for Pattern A bosses (trigger-spawned + killed + player re-enters
    // the trigger volume): without this, the boss respawns mid-cycle.
    REQUIRE(selva::gameplay::actors().size() == pool_size_before);
    clearPool();
}

TEST_CASE("engage verb on missing boss is a logged no-op", "[boss-dispatch][idempotence]")
{
    clearPool();
    // Pool empty -- findBossInPool returns -1, verb logs and returns.
    selva::gameplay::handleEngageVerb("nonexistent_boss");
    REQUIRE(selva::gameplay::actors().empty());
}
