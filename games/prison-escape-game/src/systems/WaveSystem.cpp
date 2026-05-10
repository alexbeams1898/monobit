#include "systems/WaveSystem.h"

#include "ConfigLoader.h"
#include "TileMap.h"
#include "ecs/AppState.h"
#include "ecs/Components.h"
#include "ecs/GameComponents.h"
#include "ecs/GameConfig.h"
#include "ops/AppearanceOps.h"
#include "ops/SpawnUtils.h"
#include "systems/AudioSystem.h"
#include "systems/LevelingSystem.h"

#include <tracy/Tracy.hpp>

#include <cmath>
#include <random>
#include <vector>

// Find player position. Returns false if no player exists.
static bool findPlayer(EntityManager& em, float& px, float& py)
{
    for (auto e : em.registry().view<PlayerActions>())
    {
        if (em.registry().all_of<Transform>(e))
        {
            const auto& t = em.registry().get<Transform>(e);
            px = t.x;
            py = t.y;
            return true;
        }
        break;
    }
    return false;
}

// Spawn one enemy from the current group, apply level scaling + essence, advance group tracking.
static bool spawnOneEnemy(EntityManager& em, const ActiveWave& wave, WaveState& ws, float px,
                          float py)
{
    // Find which group to spawn from.
    while (ws.spawn_group_index < static_cast<int>(wave.enemies.size()))
    {
        if (ws.spawn_group_progress < wave.enemies[ws.spawn_group_index].count)
            break;
        ws.spawn_group_index++;
        ws.spawn_group_progress = 0;
    }

    if (ws.spawn_group_index >= static_cast<int>(wave.enemies.size()))
        return false;

    const auto& group = wave.enemies[ws.spawn_group_index];
    auto& wc = em.registry().ctx().get<WaveConfig>();

    float sx = 0.0f;
    float sy = 0.0f;
    const int roomCount = static_cast<int>(em.tile_map.placed_rooms.size());

    // Find the rest room (contains the 'R' spawn point) so we skip it.
    int restRoom = -1;
    for (const auto& sp : em.tile_map.spawn_points)
    {
        if (sp.type == 'R')
        {
            restRoom = em.tile_map.findRoomAt(sp.x, sp.y);
            break;
        }
    }

    bool spawned = false;
    if (roomCount > 0)
    {
        const int targetRoom = SpawnUtils::nextSpawnRoom(roomCount, restRoom);
        spawned =
            SpawnUtils::findSpawnInRoom(em.tile_map, targetRoom, px, py, wc.spawn_near, sx, sy);
    }
    if (!spawned &&
        !SpawnUtils::findSpawnPosition(em.tile_map, px, py, wc.spawn_near, wc.spawn_far, sx, sy))
        return false;

    auto entity = ConfigLoader::loadEntity(em, group.config_path);
    if (!em.registry().valid(entity))
        return false;

    // Resolve layered appearance if this enemy has an AppearanceDef.
    if (em.registry().all_of<AppearanceDef>(entity))
        AppearanceOps::resolveAppearance(em, entity);

    auto& t = em.registry().get<Transform>(entity);
    t.x = sx;
    t.y = sy;

    // Wave level scaling: higher waves produce stronger enemies.
    const auto& gen = wc.gen;
    const int enemy_level =
        1 +
        static_cast<int>(std::floor(static_cast<float>(ws.current_wave - 1) * gen.level_growth));

    // Store wave level on Loot for XP calculation at death.
    if (em.registry().all_of<Loot>(entity))
        em.registry().get<Loot>(entity).level = enemy_level;

    if (em.registry().all_of<Stats>(entity))
    {
        auto& stats = em.registry().get<Stats>(entity);

        const int level_bonus = (enemy_level - 1) * gen.stat_per_level;
        stats.str += level_bonus;
        stats.dex += level_bonus;
        stats.end += level_bonus;
        stats.lck += level_bonus;

        // Roll essence: random 0-100 per stat, scaled proportionally to base stat.
        // bonus = floor(essence_value * base_stat / 100)
        auto& f = em.registry().ctx().get<FormulaConfig>();
        const auto& ess = f.essence;
        auto& rng = SpawnUtils::getRng();
        std::uniform_int_distribution<int> dist(ess.min, ess.max);

        const int es = dist(rng);
        const int ed = dist(rng);
        const int ee = dist(rng);
        const int el = dist(rng);

        const int bs = es * stats.str / 100;
        const int bd = ed * stats.dex / 100;
        const int be = ee * stats.end / 100;
        const int bl = el * stats.lck / 100;

        stats.str += bs;
        stats.dex += bd;
        stats.end += be;
        stats.lck += bl;

        em.registry().emplace<Essence>(entity, Essence{es, ed, ee, el});
    }

    LevelingSystem::deriveInitialStats(em, entity);
    em.registry().emplace<WaveEnemy>(entity);

    ws.spawn_group_progress++;
    ws.enemies_spawned++;

    TracyMessageL("EnemySpawned");
    return true;
}

// ---------------------------------------------------------------------------
// Auto-wave generation
// ---------------------------------------------------------------------------

ActiveWave WaveSystem::generateWave(const WaveGenRules& gen, int wave_number)
{
    // Check for manual override.
    auto it = gen.overrides.find(wave_number);
    if (it != gen.overrides.end())
    {
        const auto& ov = it->second;
        ActiveWave aw;
        for (const auto& g : ov.enemies)
            aw.enemies.push_back({g.config_path, g.count});
        aw.spawn_interval = ov.spawn_interval;
        aw.burst_size = ov.burst_size;
        aw.safe_room_after = ov.safe_room_after;
        return aw;
    }

    ActiveWave aw;

    // Enemy count: floor(start * growth^(N-1)), clamped.
    int count = static_cast<int>(
        std::floor(static_cast<float>(gen.start_count) *
                   std::pow(gen.count_growth, static_cast<float>(wave_number - 1))));
    count = std::min(count, gen.max_count);
    count = std::max(count, 1);

    // Build eligible enemy pool for this wave.
    struct Eligible
    {
        const WaveEnemyEntry* entry;
        int weight;
    };
    std::vector<Eligible> eligible;
    int total_weight = 0;
    for (const auto& e : gen.enemies)
    {
        if (e.from_wave <= wave_number)
        {
            eligible.push_back({&e, e.weight});
            total_weight += e.weight;
        }
    }

    if (eligible.empty())
    {
        // Fallback: first entry in pool or hardcoded default.
        if (!gen.enemies.empty())
            aw.enemies.push_back({gen.enemies[0].config_path, count});
        else
            aw.enemies.push_back({"config/entities/skeleton.json", count});
    }
    else
    {
        // Distribute count by weight.
        int assigned = 0;
        for (const auto& eg : eligible)
        {
            const int n = (count * eg.weight) / total_weight;
            if (n > 0)
            {
                aw.enemies.push_back({eg.entry->config_path, n});
                assigned += n;
            }
        }
        // Remainder to first eligible type.
        const int remainder = count - assigned;
        if (remainder > 0)
        {
            if (aw.enemies.empty())
                aw.enemies.push_back({eligible[0].entry->config_path, remainder});
            else
                aw.enemies[0].count += remainder;
        }
    }

    // Spawn interval: max(start * decay^(N-1), min).
    aw.spawn_interval = std::max(
        gen.start_interval *
            static_cast<float>(std::pow(gen.interval_decay, static_cast<float>(wave_number - 1))),
        gen.min_interval);

    // Burst size: start + (N-1)/every, clamped.
    aw.burst_size = gen.start_burst;
    if (gen.burst_growth_every > 0)
        aw.burst_size += (wave_number - 1) / gen.burst_growth_every;
    aw.burst_size = std::min(aw.burst_size, gen.max_burst);

    // Safe room periodicity.
    aw.safe_room_after = (gen.safe_room_every > 0) && (wave_number % gen.safe_room_every == 0);

    return aw;
}

// ---------------------------------------------------------------------------
// State machine
// ---------------------------------------------------------------------------

// Spawning phase: drip-feed enemies in bursts at the configured interval.
static void tickSpawning(EntityManager& em, WaveState& ws, double dt)
{
    if (ws.needs_map_regen)
        return;

    ws.spawn_timer -= static_cast<float>(dt);
    if (ws.spawn_timer > 0.0f)
        return;

    const auto& wave = ws.active_def;
    ws.spawn_timer = wave.spawn_interval;

    float px = 0.0f;
    float py = 0.0f;
    if (!findPlayer(em, px, py))
        return;

    for (int i = 0; i < wave.burst_size && ws.enemies_spawned < ws.enemies_total; ++i)
        spawnOneEnemy(em, wave, ws, px, py);

    if (ws.enemies_spawned >= ws.enemies_total)
        ws.phase = WaveState::Phase::Active;
}

static constexpr float TRANSITION_DELAY = 1.0f;
static constexpr float WAVE_ADVANCE_DELAY = 2.0f;
static void commitWaveStart(EntityManager& em);

// Count wave enemies that haven't died yet.
static int countAliveWaveEnemies(EntityManager& em)
{
    int alive = 0;
    for (auto e : em.registry().view<WaveEnemy>())
    {
        if (!em.registry().all_of<Dead>(e))
            ++alive;
    }
    return alive;
}

// Victory: all waves cleared. Stop combat music, play victory fanfare.
static void handleVictoryCompletion(EntityManager& em, WaveState& ws)
{
    ws.phase = WaveState::Phase::Complete;
    AudioSystem::stopMusic();

    if (auto* t = em.registry().ctx().get<MusicConfig>().get("victory"))
        AudioSystem::playMusic(t->path, t->volume);

    TracyMessageL("RunComplete");
}

// Spawn a ladder at the player-start marker, pan camera to it, play SFX.
static void transitionToSafeRoom(EntityManager& em, WaveState& ws)
{
    ws.phase = WaveState::Phase::SafeRoom;

    // Find the player-start spawn point ('P') for ladder placement.
    float lx = 0.0f;
    float ly = 0.0f;
    bool found = false;
    for (const auto& sp : em.tile_map.spawn_points)
    {
        if (sp.type == 'P')
        {
            lx = sp.x;
            ly = sp.y;
            found = true;
            break;
        }
    }

    if (!found)
        return;

    auto ladder = ConfigLoader::loadEntity(em, "config/entities/ladder.json");
    if (!em.registry().valid(ladder))
        return;

    auto& lt = em.registry().get<Transform>(ladder);
    lt.x = lx;
    lt.y = ly;
    lt.scale = 0.0f;

    // Camera pan to show the ladder materializing.
    for (auto pe : em.registry().view<PlayerActions>())
    {
        if (em.registry().all_of<Camera>(pe))
        {
            const auto& cam = em.registry().get<Camera>(pe);
            CameraPan pan;
            pan.start_x = cam.x;
            pan.start_y = cam.y;
            pan.target_x = lx;
            pan.target_y = ly;
            em.registry().emplace<CameraPan>(pe, pan);
        }
        break;
    }

    const auto& sc = em.registry().ctx().get<SoundConfig>();
    const auto& ladderSnd = sc.get("ladder_appear");
    if (!ladderSnd.path.empty())
        AudioSystem::playSfx(ladderSnd.path, ladderSnd.volume);
}

void WaveSystem::update(EntityManager& em, double dt)
{
    ZoneScopedN("WaveSystem");
    auto& ws = em.registry().ctx().get<WaveState>();
    const auto& wc = em.registry().ctx().get<WaveConfig>();

    if (!wc.loaded)
        return;

    switch (ws.phase)
    {
    case WaveState::Phase::Idle:
    case WaveState::Phase::SafeRoom:
    case WaveState::Phase::GameOver:
    case WaveState::Phase::Complete:
        break;

    case WaveState::Phase::Transitioning:
        ws.transition_timer += static_cast<float>(dt);
        if (ws.transition_timer >= TRANSITION_DELAY)
            commitWaveStart(em);
        break;

    case WaveState::Phase::Spawning:
        tickSpawning(em, ws, dt);
        break;

    case WaveState::Phase::Active:
        if (countAliveWaveEnemies(em) == 0)
        {
            ws.phase = WaveState::Phase::Cleared;
            ws.cleared_timer = 0.0f;
            TracyMessageL("WaveCleared");
        }
        break;

    case WaveState::Phase::Cleared:
        ws.cleared_timer += static_cast<float>(dt);
        if (ws.cleared_timer >= WAVE_ADVANCE_DELAY)
        {
            if (wc.gen.max_waves > 0 && ws.current_wave >= wc.gen.max_waves)
                handleVictoryCompletion(em, ws);
            else
                transitionToSafeRoom(em, ws);
        }
        break;
    }
}

// Death restart: destroy all enemies + old player, recreate player fresh.
static void handleDeathRestart(EntityManager& em, WaveState& ws)
{
    auto& reg = em.registry();

    std::vector<entt::entity> toDestroy;
    for (auto e : reg.view<WaveEnemy>())
        toDestroy.push_back(e);
    for (auto e : toDestroy)
        reg.destroy(e);

    entt::entity oldPlayer = entt::null;
    float spawnX = 0.0f;
    float spawnY = 0.0f;
    for (auto pe : reg.view<PlayerActions>())
    {
        oldPlayer = pe;
        if (reg.all_of<Transform>(pe))
        {
            const auto& t = reg.get<Transform>(pe);
            spawnX = t.x;
            spawnY = t.y;
        }
        break;
    }
    if (oldPlayer != entt::null)
        reg.destroy(oldPlayer);

    auto newPlayer = ConfigLoader::loadEntity(em, "config/entities/player.json");
    if (reg.valid(newPlayer))
    {
        // Resolve appearance from saved character profile.
        const auto& saveData = reg.ctx().get<SaveData>();
        const auto& charName = reg.ctx().get<GameState>().active_character;
        std::unordered_map<std::string, std::string> savedAppearance;
        for (const auto& prof : saveData.characters)
        {
            if (prof.name == charName)
            {
                savedAppearance = prof.appearance;
                break;
            }
        }
        AppearanceOps::resolveAppearance(em, newPlayer, savedAppearance);
        AppearanceOps::applyAppearanceScale(em, newPlayer, savedAppearance);

        auto& t = reg.get<Transform>(newPlayer);
        t.x = spawnX;
        t.y = spawnY;
        reg.emplace<PlayerActions>(newPlayer);
        reg.emplace<Camera>(newPlayer,
                            Camera{.x = spawnX, .y = spawnY, .prev_x = spawnX, .prev_y = spawnY});
        LevelingSystem::applyInitialDerivations(em);
    }

    ws.current_wave = 0;
    TracyMessageL("DeathRestart");
}

// Phase 2 of wave start: set up spawning after the transition delay.
static void commitWaveStart(EntityManager& em)
{
    ZoneScopedN("commitWaveStart");
    auto& ws = em.registry().ctx().get<WaveState>();
    const auto& wc = em.registry().ctx().get<WaveConfig>();

    const int next = ws.current_wave;
    ws.active_def = WaveSystem::generateWave(wc.gen, next);

    ws.enemies_total = 0;
    for (const auto& g : ws.active_def.enemies)
        ws.enemies_total += g.count;

    ws.enemies_spawned = 0;
    ws.spawn_timer = 0.0f;
    ws.spawn_group_index = 0;
    ws.spawn_group_progress = 0;
    // Wave 1 map is already built by WorldInit::createWorld -- skip regen.
    ws.needs_map_regen = (ws.current_wave > 1);
    ws.phase = WaveState::Phase::Spawning;
    SpawnUtils::resetSpawnRoomCounter();

    TracyMessageL("WaveStarted");

    // Pick a random music track, avoiding back-to-back repeats.
    auto& mc = em.registry().ctx().get<MusicConfig>();
    if (!mc.tracks.empty())
    {
        auto& rng = SpawnUtils::getRng();
        int idx = 0;
        if (mc.tracks.size() == 1)
        {
            idx = 0;
        }
        else
        {
            std::uniform_int_distribution<int> dist(0, static_cast<int>(mc.tracks.size()) - 1);
            do
            {
                idx = dist(rng);
            } while (idx == mc.last_track_index);
        }
        mc.last_track_index = idx;
        const auto& track = mc.tracks[idx];
        AudioSystem::playMusic(track.path, track.volume);
    }
}

bool WaveSystem::startNextWave(EntityManager& em)
{
    auto& ws = em.registry().ctx().get<WaveState>();
    const auto& wc = em.registry().ctx().get<WaveConfig>();

    if (ws.phase == WaveState::Phase::GameOver)
        handleDeathRestart(em, ws);

    if (ws.phase != WaveState::Phase::Idle && ws.phase != WaveState::Phase::SafeRoom &&
        ws.phase != WaveState::Phase::Cleared && ws.phase != WaveState::Phase::GameOver)
        return false;

    const int next = ws.current_wave + 1;
    if (wc.gen.max_waves > 0 && next > wc.gen.max_waves)
        return false;

    ws.current_wave = next;
    em.registry().ctx().get<RunStats>().wave = next;

    // First wave starts immediately; subsequent waves play teleport SFX
    // and delay 1 second so the sound finishes before the map switches.
    if (next == 1)
    {
        commitWaveStart(em);
    }
    else
    {
        const auto& sc = em.registry().ctx().get<SoundConfig>();
        const auto& wcSnd = sc.get("wave_clear");
        AudioSystem::playSfx(wcSnd.path, wcSnd.volume);
        ws.transition_timer = 0.0f;
        ws.phase = WaveState::Phase::Transitioning;
    }

    return true;
}
