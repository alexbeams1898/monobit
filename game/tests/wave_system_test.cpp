#include "ecs/Components.h"
#include "ecs/EntityManager.h"
#include "systems/WaveSystem.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

// Helper: set up a minimal auto-gen config.
static void setupAutoGen(EntityManager& em, int max_waves = 0, int start_count = 3,
                          int safe_room_every = 0)
{
    auto& gen = em.wave_config.gen;
    gen.enemies.push_back({"config/entities/skeleton.json", 1, 1});
    gen.start_count = start_count;
    gen.count_growth = 1.0f; // flat count for predictable tests
    gen.max_count = 200;
    gen.start_interval = 0.5f;
    gen.interval_decay = 1.0f; // no decay for predictable tests
    gen.min_interval = 0.1f;
    gen.start_burst = 1;
    gen.burst_growth_every = 0; // no burst growth
    gen.max_burst = 1;
    gen.safe_room_every = safe_room_every;
    gen.max_waves = max_waves;
    em.wave_config.loaded = true;
}

// ---------------------------------------------------------------------------
// Default state
// ---------------------------------------------------------------------------

TEST_CASE("WaveState starts in Idle phase", "[wave]")
{
    EntityManager em;
    REQUIRE(em.wave_state.phase == WaveState::Phase::Idle);
    REQUIRE(em.wave_state.current_wave == 0);
}

// ---------------------------------------------------------------------------
// startNextWave transitions
// ---------------------------------------------------------------------------

TEST_CASE("startNextWave: Idle to Spawning", "[wave]")
{
    EntityManager em;
    setupAutoGen(em, 1, 3);

    bool started = WaveSystem::startNextWave(em);
    REQUIRE(started);
    REQUIRE(em.wave_state.phase == WaveState::Phase::Spawning);
    REQUIRE(em.wave_state.current_wave == 1);
    REQUIRE(em.wave_state.enemies_total == 3);
    REQUIRE(em.wave_state.enemies_spawned == 0);
}

TEST_CASE("startNextWave: past max_waves returns false", "[wave]")
{
    EntityManager em;
    setupAutoGen(em, 1);
    em.wave_state.current_wave = 1;
    em.wave_state.phase = WaveState::Phase::Idle;

    REQUIRE_FALSE(WaveSystem::startNextWave(em));
}

TEST_CASE("startNextWave: from Active returns false", "[wave]")
{
    EntityManager em;
    setupAutoGen(em, 1);
    em.wave_state.phase = WaveState::Phase::Active;

    REQUIRE_FALSE(WaveSystem::startNextWave(em));
}

TEST_CASE("startNextWave: from Spawning returns false", "[wave]")
{
    EntityManager em;
    setupAutoGen(em, 1);
    em.wave_state.phase = WaveState::Phase::Spawning;

    REQUIRE_FALSE(WaveSystem::startNextWave(em));
}

TEST_CASE("startNextWave: from SafeRoom transitions to Spawning", "[wave]")
{
    EntityManager em;
    setupAutoGen(em, 2, 3);

    // Simulate having completed wave 1 and being in SafeRoom.
    em.wave_state.phase = WaveState::Phase::SafeRoom;
    em.wave_state.current_wave = 1;

    bool started = WaveSystem::startNextWave(em);
    REQUIRE(started);
    REQUIRE(em.wave_state.phase == WaveState::Phase::Spawning);
    REQUIRE(em.wave_state.current_wave == 2);
    REQUIRE(em.wave_state.enemies_total == 3);
}

TEST_CASE("startNextWave: infinite waves (max_waves=0) never blocks", "[wave]")
{
    EntityManager em;
    setupAutoGen(em, 0, 1); // infinite

    for (int i = 0; i < 100; ++i)
    {
        em.wave_state.phase = WaveState::Phase::Idle;
        REQUIRE(WaveSystem::startNextWave(em));
    }
    REQUIRE(em.wave_state.current_wave == 100);
}

// ---------------------------------------------------------------------------
// Wave clear detection
// ---------------------------------------------------------------------------

TEST_CASE("Wave clears when all WaveEnemy entities are Dead", "[wave]")
{
    EntityManager em;
    setupAutoGen(em, 1);
    em.wave_state.phase = WaveState::Phase::Active;
    em.wave_state.current_wave = 1;

    auto e1 = em.create();
    em.registry().emplace<WaveEnemy>(e1);
    auto e2 = em.create();
    em.registry().emplace<WaveEnemy>(e2);

    // Both alive -- wave stays Active.
    WaveSystem::update(em, 0.016);
    REQUIRE(em.wave_state.phase == WaveState::Phase::Active);

    // Mark both Dead.
    em.registry().emplace<Dead>(e1);
    em.registry().emplace<Dead>(e2);

    WaveSystem::update(em, 0.016);
    REQUIRE(em.wave_state.phase == WaveState::Phase::Cleared);
}

TEST_CASE("Alive count excludes Dead entities", "[wave]")
{
    EntityManager em;
    setupAutoGen(em, 1);
    em.wave_state.phase = WaveState::Phase::Active;
    em.wave_state.current_wave = 1;

    auto e1 = em.create();
    em.registry().emplace<WaveEnemy>(e1);
    auto e2 = em.create();
    em.registry().emplace<WaveEnemy>(e2);
    em.registry().emplace<Dead>(e2); // dead
    auto e3 = em.create();
    em.registry().emplace<WaveEnemy>(e3);

    // 2 alive (e1, e3) -- wave stays Active.
    WaveSystem::update(em, 0.016);
    REQUIRE(em.wave_state.phase == WaveState::Phase::Active);
}

// ---------------------------------------------------------------------------
// Cleared transitions
// ---------------------------------------------------------------------------

TEST_CASE("Cleared transitions to SafeRoom when safe_room_every triggers", "[wave]")
{
    EntityManager em;
    setupAutoGen(em, 5, 1, 1); // safe_room_every=1 so every wave triggers

    // Start and populate active_def for wave 1.
    WaveSystem::startNextWave(em);
    em.wave_state.phase = WaveState::Phase::Cleared;

    WaveSystem::update(em, 0.016);
    REQUIRE(em.wave_state.phase == WaveState::Phase::SafeRoom);
}

TEST_CASE("Cleared transitions to Idle when safe_room_every is 0", "[wave]")
{
    EntityManager em;
    setupAutoGen(em, 5, 1, 0); // safe_room_every=0

    WaveSystem::startNextWave(em);
    em.wave_state.phase = WaveState::Phase::Cleared;

    WaveSystem::update(em, 0.016);
    REQUIRE(em.wave_state.phase == WaveState::Phase::Idle);
}

TEST_CASE("Cleared transitions to Complete on last wave", "[wave]")
{
    EntityManager em;
    setupAutoGen(em, 1, 1); // max_waves=1

    WaveSystem::startNextWave(em);
    em.wave_state.phase = WaveState::Phase::Cleared;

    WaveSystem::update(em, 0.016);
    REQUIRE(em.wave_state.phase == WaveState::Phase::Complete);
}

// ---------------------------------------------------------------------------
// No-op when config not loaded
// ---------------------------------------------------------------------------

TEST_CASE("WaveSystem update is no-op without loaded config", "[wave]")
{
    EntityManager em;
    // wave_config.loaded is false by default.
    WaveSystem::update(em, 0.016);
    REQUIRE(em.wave_state.phase == WaveState::Phase::Idle);
}

// ---------------------------------------------------------------------------
// Input-triggered wave start
// ---------------------------------------------------------------------------

TEST_CASE("start_wave input triggers startNextWave", "[wave]")
{
    EntityManager em;
    setupAutoGen(em, 1, 2);

    // Create a player entity with Input component.
    auto player = em.create();
    auto& inp = em.registry().emplace<Input>(player);
    inp.start_wave = true;

    WaveSystem::update(em, 0.016);

    // start_wave consumed, wave started.
    REQUIRE(em.registry().get<Input>(player).start_wave == false);
    REQUIRE(em.wave_state.phase == WaveState::Phase::Spawning);
    REQUIRE(em.wave_state.current_wave == 1);
}

// ---------------------------------------------------------------------------
// Auto-wave generation formulas
// ---------------------------------------------------------------------------

TEST_CASE("generateWave: enemy count grows each wave", "[wave]")
{
    WaveGenRules gen;
    gen.enemies.push_back({"skeleton.json", 1, 1});
    gen.start_count = 3;
    gen.count_growth = 2.0f;
    gen.max_count = 100;

    auto w1 = WaveSystem::generateWave(gen, 1);
    auto w2 = WaveSystem::generateWave(gen, 2);
    auto w3 = WaveSystem::generateWave(gen, 3);

    REQUIRE(w1.enemies[0].count == 3);
    REQUIRE(w2.enemies[0].count == 6);
    REQUIRE(w3.enemies[0].count == 12);
}

TEST_CASE("generateWave: count capped at max_count", "[wave]")
{
    WaveGenRules gen;
    gen.enemies.push_back({"skeleton.json", 1, 1});
    gen.start_count = 100;
    gen.count_growth = 2.0f;
    gen.max_count = 50;

    auto w = WaveSystem::generateWave(gen, 1);
    REQUIRE(w.enemies[0].count == 50);
}

TEST_CASE("generateWave: spawn interval decays each wave", "[wave]")
{
    WaveGenRules gen;
    gen.enemies.push_back({"skeleton.json", 1, 1});
    gen.start_count = 1;
    gen.count_growth = 1.0f;
    gen.start_interval = 1.0f;
    gen.interval_decay = 0.5f;
    gen.min_interval = 0.1f;

    auto w1 = WaveSystem::generateWave(gen, 1);
    auto w2 = WaveSystem::generateWave(gen, 2);
    auto w3 = WaveSystem::generateWave(gen, 3);

    REQUIRE(w1.spawn_interval == Catch::Approx(1.0f));
    REQUIRE(w2.spawn_interval == Catch::Approx(0.5f));
    REQUIRE(w3.spawn_interval == Catch::Approx(0.25f));
}

TEST_CASE("generateWave: interval floored at min_interval", "[wave]")
{
    WaveGenRules gen;
    gen.enemies.push_back({"skeleton.json", 1, 1});
    gen.start_count = 1;
    gen.count_growth = 1.0f;
    gen.start_interval = 0.2f;
    gen.interval_decay = 0.01f; // extreme decay
    gen.min_interval = 0.15f;

    auto w = WaveSystem::generateWave(gen, 5);
    REQUIRE(w.spawn_interval >= 0.15f);
}

TEST_CASE("generateWave: burst size grows every N waves", "[wave]")
{
    WaveGenRules gen;
    gen.enemies.push_back({"skeleton.json", 1, 1});
    gen.start_count = 10;
    gen.count_growth = 1.0f;
    gen.start_burst = 1;
    gen.burst_growth_every = 3;
    gen.max_burst = 10;

    auto w1 = WaveSystem::generateWave(gen, 1);
    auto w3 = WaveSystem::generateWave(gen, 3);
    auto w4 = WaveSystem::generateWave(gen, 4);
    auto w7 = WaveSystem::generateWave(gen, 7);

    REQUIRE(w1.burst_size == 1);
    REQUIRE(w3.burst_size == 1); // (3-1)/3 = 0
    REQUIRE(w4.burst_size == 2); // (4-1)/3 = 1
    REQUIRE(w7.burst_size == 3); // (7-1)/3 = 2
}

TEST_CASE("generateWave: safe_room_every triggers correctly", "[wave]")
{
    WaveGenRules gen;
    gen.enemies.push_back({"skeleton.json", 1, 1});
    gen.start_count = 1;
    gen.count_growth = 1.0f;
    gen.safe_room_every = 3;

    REQUIRE_FALSE(WaveSystem::generateWave(gen, 1).safe_room_after);
    REQUIRE_FALSE(WaveSystem::generateWave(gen, 2).safe_room_after);
    REQUIRE(WaveSystem::generateWave(gen, 3).safe_room_after);
    REQUIRE_FALSE(WaveSystem::generateWave(gen, 4).safe_room_after);
    REQUIRE(WaveSystem::generateWave(gen, 6).safe_room_after);
}

TEST_CASE("generateWave: manual override replaces auto-gen", "[wave]")
{
    WaveGenRules gen;
    gen.enemies.push_back({"skeleton.json", 1, 1});
    gen.start_count = 10;
    gen.count_growth = 1.0f;

    WaveOverride ov;
    ov.enemies.push_back({"boss.json", 1});
    ov.spawn_interval = 0.0f;
    ov.burst_size = 1;
    ov.safe_room_after = true;
    gen.overrides[2] = ov;

    // Wave 1: auto-gen (10 enemies).
    auto w1 = WaveSystem::generateWave(gen, 1);
    REQUIRE(w1.enemies[0].count == 10);
    REQUIRE(w1.enemies[0].config_path == "skeleton.json");

    // Wave 2: override (1 boss).
    auto w2 = WaveSystem::generateWave(gen, 2);
    REQUIRE(w2.enemies.size() == 1);
    REQUIRE(w2.enemies[0].count == 1);
    REQUIRE(w2.enemies[0].config_path == "boss.json");
    REQUIRE(w2.safe_room_after);
}

TEST_CASE("generateWave: from_wave filters enemy pool", "[wave]")
{
    WaveGenRules gen;
    gen.enemies.push_back({"fodder.json", 1, 1});
    gen.enemies.push_back({"elite.json", 5, 1}); // not available until wave 5
    gen.start_count = 10;
    gen.count_growth = 1.0f;

    // Wave 1: only fodder eligible.
    auto w1 = WaveSystem::generateWave(gen, 1);
    REQUIRE(w1.enemies.size() == 1);
    REQUIRE(w1.enemies[0].config_path == "fodder.json");
    REQUIRE(w1.enemies[0].count == 10);

    // Wave 5: both eligible, equal weight -> 5 each.
    auto w5 = WaveSystem::generateWave(gen, 5);
    int total = 0;
    for (const auto& g : w5.enemies)
        total += g.count;
    REQUIRE(total == 10);
    REQUIRE(w5.enemies.size() == 2);
}

TEST_CASE("generateWave: weighted distribution", "[wave]")
{
    WaveGenRules gen;
    gen.enemies.push_back({"common.json", 1, 3});
    gen.enemies.push_back({"rare.json", 1, 1});
    gen.start_count = 12;
    gen.count_growth = 1.0f;

    auto w = WaveSystem::generateWave(gen, 1);
    // Weight 3:1 of 12 = 9 common, 3 rare.
    int total = 0;
    for (const auto& g : w.enemies)
        total += g.count;
    REQUIRE(total == 12);
    REQUIRE(w.enemies[0].config_path == "common.json");
    REQUIRE(w.enemies[0].count == 9);
    REQUIRE(w.enemies[1].config_path == "rare.json");
    REQUIRE(w.enemies[1].count == 3);
}

// ---------------------------------------------------------------------------
// Essence system
// ---------------------------------------------------------------------------

TEST_CASE("Essence defaults in FormulaConfig", "[wave][essence]")
{
    FormulaConfig f;
    REQUIRE(f.essence.min == 0);
    REQUIRE(f.essence.max == 100);
}

TEST_CASE("WaveGenRules level scaling defaults", "[wave]")
{
    WaveGenRules gen;
    REQUIRE(gen.level_growth == Catch::Approx(0.5f));
    REQUIRE(gen.stat_per_level == 1);
}

// ---------------------------------------------------------------------------
// enemies_total from generated wave
// ---------------------------------------------------------------------------

// ---------------------------------------------------------------------------
// GameOver restart
// ---------------------------------------------------------------------------

TEST_CASE("startNextWave: from GameOver resets to wave 1", "[wave]")
{
    EntityManager em;
    setupAutoGen(em, 0, 3);
    em.wave_state.phase = WaveState::Phase::GameOver;
    em.wave_state.current_wave = 5;

    auto player = em.create();
    em.registry().emplace<Input>(player);
    em.registry().emplace<Health>(player, Health{0, 100});

    bool started = WaveSystem::startNextWave(em);
    REQUIRE(started);
    REQUIRE(em.wave_state.current_wave == 1);
    REQUIRE(em.wave_state.phase == WaveState::Phase::Spawning);
    REQUIRE(em.registry().get<Health>(player).current == 100);
}

TEST_CASE("startNextWave: GameOver destroys wave enemies", "[wave]")
{
    EntityManager em;
    setupAutoGen(em, 0, 1);
    em.wave_state.phase = WaveState::Phase::GameOver;
    em.wave_state.current_wave = 3;

    auto player = em.create();
    em.registry().emplace<Input>(player);
    em.registry().emplace<Health>(player, Health{0, 50});

    auto enemy = em.create();
    em.registry().emplace<WaveEnemy>(enemy);

    WaveSystem::startNextWave(em);

    REQUIRE_FALSE(em.registry().valid(enemy));
    REQUIRE(em.wave_state.current_wave == 1);
}

TEST_CASE("startNextWave: GameOver no-op state waits for input", "[wave]")
{
    EntityManager em;
    setupAutoGen(em, 0, 1);
    em.wave_state.phase = WaveState::Phase::GameOver;
    em.wave_state.current_wave = 2;

    // Update without input -- stays in GameOver.
    WaveSystem::update(em, 0.016);
    REQUIRE(em.wave_state.phase == WaveState::Phase::GameOver);
}

// ---------------------------------------------------------------------------

TEST_CASE("enemies_total sums all groups from generated wave", "[wave]")
{
    EntityManager em;
    auto& gen = em.wave_config.gen;
    gen.enemies.push_back({"enemy_a.json", 1, 3});
    gen.enemies.push_back({"enemy_b.json", 1, 2});
    gen.start_count = 10;
    gen.count_growth = 1.0f;
    gen.max_waves = 1;
    em.wave_config.loaded = true;

    WaveSystem::startNextWave(em);
    REQUIRE(em.wave_state.enemies_total == 10);
}
