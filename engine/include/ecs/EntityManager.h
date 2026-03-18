#pragma once

#include "TileMap.h"

#include <entt/entt.hpp>
#include <string>
#include <vector>

// ---------------------------------------------------------------------------
// FormulaConfig — all balance constants loaded once from config/balance/formulas.json.
// Stored here so every system can read from em.formulas without coupling to
// the JSON parser or re-loading the file each frame.
// All fields carry sensible defaults so tests don't need a real JSON file.
// ---------------------------------------------------------------------------
struct FormulaConfig
{
    struct
    {
        float base = 5.f;
        float scale = 100.f;
    } hp;

    struct
    {
        float base = 150.f;
        float dex_scale = 30.f;
        float sprint_multiplier = 1.6f;
        float sprint_blend = 8.0f; // ramp-up rate when transitioning to sprint
        float walk_blend = 20.0f;  // ramp-down rate when returning to walk
    } movement;

    struct
    {
        float str_scale = 20.f;
        float end_scale = 10.f;
    } carry_weight;

    struct
    {
        float str_scale = 0.3f;
        float end_scale = 0.5f;
        float level_scale = 0.2f;
        float cap = 75.f;
    } defense;

    struct
    {
        float drop_scale = 15.f;
    } luck;

    // Display thresholds only — not used in damage calculation.
    // Given a weapon's raw scaling float, the UI shows the letter whose
    // threshold it meets or exceeds (checked S → E in order).
    struct
    {
        float s = 1.5f;
        float a = 1.25f;
        float b = 1.0f;
        float c = 0.75f;
        float d = 0.5f;
        float e = 0.25f;
    } grade_thresholds;

    struct
    {
        float weight_scale = 100.f;
        float stat_scale = 40.f;
        float two_handed_str_bonus = 0.3f;
    } swing;

    struct
    {
        float penalty_rate = 0.15f;
    } stat_requirement;

    struct
    {
        // poise_max = floor(END * end_scale + STR * str_scale) at spawn.
        // Armor and shields add flat bonuses on top (future).
        float end_scale = 2.0f;
        float str_scale = 1.0f;
        // poise_damage per hit = attacker weapon weight * weight_scale
        float weight_scale = 20.0f;
        // how long the hit-stagger lasts (brief flinch — not guard-break length)
        float stagger_duration = 0.15f;
        // seconds of no hits before accumulated poise damage resets
        float decay_window = 5.0f;
    } poise;

    struct
    {
        float xp_base = 100.f;
        float xp_exponent = 1.5f;
        float points_per_level = 1.f;
    } leveling;

    bool loaded = false;
};

// ---------------------------------------------------------------------------
// SoundConfig — event-to-file mappings loaded from config/audio/sounds.json.
// Same pattern as FormulaConfig: singleton on EntityManager, loaded once at
// startup, read-only during gameplay. Defaults match the placeholder sounds
// so the game runs identically even without the JSON file.
// ---------------------------------------------------------------------------
struct SoundEntry
{
    std::string path;
    float volume = 0.5f;
};

struct SoundConfig
{
    SoundEntry player_attack{"assets/sfx/attack.wav", 0.5f};
    SoundEntry player_skill{"assets/sfx/skill.wav", 0.6f};
    SoundEntry player_dodge{"assets/sfx/dodge.wav", 0.5f};
    SoundEntry hit{"assets/sfx/hit.wav", 0.4f};
    SoundEntry parry{"assets/sfx/parry.wav", 0.6f};
    SoundEntry death{"assets/sfx/death.wav", 0.5f};
    SoundEntry pickup{"assets/sfx/pickup.wav", 0.4f};
    SoundEntry level_up{"assets/sfx/levelup.wav", 0.6f};
    SoundEntry wall_bump{"assets/sfx/wall_bump.wav", 0.3f};
    SoundEntry footstep_walk{"assets/sfx/footstep_walk.wav", 0.15f};
    SoundEntry footstep_run{"assets/sfx/footstep_run.wav", 0.25f};
    bool loaded = false;
};

// ---------------------------------------------------------------------------
// CollisionEvent — emitted by CollisionSystem each frame for every overlapping
// pair of entities that both carry a Collider.
// Stored in EntityManager so any system can read this frame's collisions without
// being directly coupled to CollisionSystem.
// ---------------------------------------------------------------------------
struct CollisionEvent
{
    entt::entity a; // first entity in the overlapping pair
    entt::entity b; // second entity
};

// ---------------------------------------------------------------------------
// FlowField — a spatial direction map that tells AI entities which way to move
// to reach the player along the shortest open path around walls.
//
// Stored on EntityManager (singleton — one per game world) so FlowFieldSystem
// can write it and ChaseSystem can read it without coupling them together.
//
// The grid covers COLS * ROWS cells of CELL_SIZE world-units each.
// A world position (wx, wy) maps to cell (wx / CELL_SIZE, wy / CELL_SIZE).
//
// CELL_SIZE=16 (half the tile size) is intentional: a 32px tile spans exactly
// 2×2 cells, keeping tile edges on cell boundaries. At CELL_SIZE=32 a tile and
// the free space immediately adjacent to it could share a cell, causing BFS to
// treat reachable space as blocked or vice versa.
//
// FlowFieldSystem rebuilds this via BFS each time the player enters a new cell.
// At CELL_SIZE=16 and player speed ≈200 px/s, that is at most ~12 rebuilds/sec.
// Each BFS visits at most COLS*ROWS = 16384 cells — still trivially fast.
// ---------------------------------------------------------------------------
struct FlowField
{
    static constexpr int COLS = 128;
    static constexpr int ROWS = 128;
    static constexpr float CELL_SIZE = 16.0f;

    struct Cell
    {
        float dx = 0.0f; // normalized direction toward player (or zero if no path)
        float dy = 0.0f;
    };

    Cell cells[ROWS][COLS]{};

    // Enemy density grid — binned by FlowFieldSystem each frame from current
    // enemy positions.  Each cell holds the count of chasing enemies whose
    // center falls within it, capped at 255.  Read by SteeringSystem to
    // compute crowd-pressure separation vectors.
    uint8_t density[ROWS][COLS]{};

    // How many consecutive frames the player must occupy a new cell before a
    // flow-field rebuild is triggered.  Rapid back-and-forth across a cell
    // boundary keeps resetting this counter, so the field stays at the last
    // stable position instead of flipping directions every frame.
    // Exposed here (not buried in FlowFieldSystem.cpp) so tests can call
    // FlowFieldSystem::update exactly this many times to prime the field.
    static constexpr int STABILITY_FRAMES = 3;

    // Player's last fully-built grid cell.
    int last_player_col = -1;
    int last_player_row = -1;

    // Pending cell — the cell the player is currently in but hasn't stayed in
    // long enough to trigger a rebuild yet.
    int pending_col = -1;
    int pending_row = -1;
    int stable_count = 0;
};

// ---------------------------------------------------------------------------
// EntityManager — thin owner of the entt::registry.
//
// Responsibilities:
//   - Own the registry (one per game world / scene)
//   - Expose create() / destroy() as the canonical way to manage entity lifetime
//   - Expose registry() for all other operations (views, emplace, get, patch…)
//   - Hold this frame's collision events (written by CollisionSystem, read by others)
//
// Intentionally minimal: entt already has a complete, well-documented API.
// Don't wrap what entt does perfectly well on its own.
//
// JS analogy: think of this as the Redux store — it owns the state and hands
// out a reference. Systems are the reducers that read and write through it.
// ---------------------------------------------------------------------------

class EntityManager
{
  public:
    entt::entity create()
    {
        return reg.create();
    }

    void destroy(entt::entity entity)
    {
        reg.destroy(entity);
    }

    entt::registry& registry()
    {
        return reg;
    }

    const entt::registry& registry() const
    {
        return reg;
    }

    // Collision events accumulated by CollisionSystem this frame.
    // Cleared at the start of each CollisionSystem::update() call.
    std::vector<CollisionEvent> collision_events;

    void clearCollisionEvents()
    {
        collision_events.clear();
    }

    // Flow field — rebuilt by FlowFieldSystem via BFS whenever the player
    // enters a new grid cell. Read by ChaseSystem every frame.
    FlowField flow_field;

    // Balance formulas — loaded once from config/balance/formulas.json by
    // ConfigLoader::loadFormulas(). Read by combat, movement, and leveling
    // systems every frame. Never write to this after startup.
    FormulaConfig formulas;

    // Sound mappings — loaded once from config/audio/sounds.json by
    // ConfigLoader::loadSounds(). Read by combat, damage, movement, pickup,
    // and leveling systems. Never write to this after startup.
    SoundConfig sounds;

    // Render interpolation factor — set by Engine each frame to
    // accumulator / FIXED_TIMESTEP. RenderSystem reads this to blend
    // between PreviousTransform and Transform for smooth rendering.
    float render_alpha = 1.0f;

    // Tile map — generated at startup by TileMapLoader::generate().
    // Read by TileMapRenderer every frame for viewport-culled drawing.
    // Future: FlowFieldSystem can read tile_map.at(c,r).walkable directly
    // instead of querying wall entities.
    TileMap tile_map;
    TileConfig tile_config;

  private:
    entt::registry reg;
};
