#pragma once

#include "TileMap.h"

#include <entt/entt.hpp>
#include <string>
#include <vector>

// ---------------------------------------------------------------------------
// ENGINE DATA -- generic infrastructure used by the engine's own systems.
// Game-specific config (FormulaConfig, SoundConfig, WaveConfig, WaveState)
// is stored in entt::registry::ctx() and defined in game/include/ecs/GameConfig.h.
// ---------------------------------------------------------------------------

// ---------------------------------------------------------------------------
// CollisionEvent -- emitted by CollisionSystem each frame for every overlapping
// pair of entities that both carry a Collider.
// ---------------------------------------------------------------------------
struct CollisionEvent
{
    entt::entity a;
    entt::entity b;
};

// ---------------------------------------------------------------------------
// FlowField -- a spatial direction map that tells AI entities which way to move
// to reach the player along the shortest open path around walls.
// ---------------------------------------------------------------------------
struct FlowField
{
    static constexpr int COLS = 320;
    static constexpr int ROWS = 240;
    static constexpr float CELL_SIZE = 16.0f;

    struct Cell
    {
        float dx = 0.0f;
        float dy = 0.0f;
    };

    Cell cells[ROWS][COLS]{};

    uint8_t density[ROWS][COLS]{};

    static constexpr int STABILITY_FRAMES = 3;

    int last_player_col = -1;
    int last_player_row = -1;

    int pending_col = -1;
    int pending_row = -1;
    int stable_count = 0;
};

// ---------------------------------------------------------------------------
// EntityManager -- thin owner of the entt::registry.
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
    std::vector<CollisionEvent> collision_events;

    void clearCollisionEvents()
    {
        collision_events.clear();
    }

    // Flow field -- rebuilt by FlowFieldSystem via BFS whenever the player
    // enters a new grid cell. Read by ChaseSystem every frame.
    FlowField flow_field;

    // Render interpolation factor -- set by Engine each frame to
    // accumulator / FIXED_TIMESTEP.
    float render_alpha = 1.0f;

    // Tile map -- generated at startup by TileMapLoader::generate().
    TileMap tile_map;
    TileConfig tile_config;

  private:
    entt::registry reg;
};
