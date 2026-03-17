#pragma once

#include <entt/entt.hpp>
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
        float base = 50.f;
        float scale = 100.f;
    } hp;

    struct
    {
        float base = 150.f;
        float dex_scale = 30.f;
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

    struct
    {
        float S = 1.5f;
        float A = 1.25f;
        float B = 1.0f;
        float C = 0.75f;
        float D = 0.5f;
        float E = 0.25f;
    } grade_multipliers;

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
    int lastPlayerCol = -1;
    int lastPlayerRow = -1;

    // Pending cell — the cell the player is currently in but hasn't stayed in
    // long enough to trigger a rebuild yet.
    int pendingCol = -1;
    int pendingRow = -1;
    int stableCount = 0;
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
        return registry_.create();
    }

    void destroy(entt::entity entity)
    {
        registry_.destroy(entity);
    }

    entt::registry& registry()
    {
        return registry_;
    }

    const entt::registry& registry() const
    {
        return registry_;
    }

    // Collision events accumulated by CollisionSystem this frame.
    // Cleared at the start of each CollisionSystem::update() call.
    std::vector<CollisionEvent> collisionEvents;

    void clearCollisionEvents()
    {
        collisionEvents.clear();
    }

    // Flow field — rebuilt by FlowFieldSystem via BFS whenever the player
    // enters a new grid cell. Read by ChaseSystem every frame.
    FlowField flowField;

    // Balance formulas — loaded once from config/balance/formulas.json by
    // ConfigLoader::loadFormulas(). Read by combat, movement, and leveling
    // systems every frame. Never write to this after startup.
    FormulaConfig formulas;

  private:
    entt::registry registry_;
};
