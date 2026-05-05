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
// SteeringConfig -- tuning parameters for SteeringSystem wall/crowd forces.
// Populated by game-side config loading; engine reads these at runtime.
// ---------------------------------------------------------------------------
struct SteeringConfig
{
    float repulsion_radius = 20.0f;
    float repulsion_strength = 0.5f;
    float blend_rate = 4.0f;
    float skip_dot_threshold = -0.5f;
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

    // Buffered input events -- captured per-frame by Engine::processEvents(),
    // consumed per-tick by InputMappingSystem. Prevents brief key/mouse taps
    // from being lost when they happen between fixed-step ticks.
    std::vector<int> key_down_events;
    std::vector<uint8_t> mouse_down_events;

    // Number of fixed-step ticks that have run during the current frame.
    // Set by Engine: zeroed at the start of each frame, incremented per tick.
    // Consumed by render UI to decide whether one-shot input buffers can be
    // cleared this frame: if 0 ticks ran, the events must persist into the next
    // frame so a tick consumer eventually sees them. Without this, brief key
    // taps that arrive on a 0-tick frame are silently lost.
    int ticks_this_frame = 0;

    // Mouse wheel delta -- accumulated per-frame, positive = scroll up.
    int mouse_wheel_y = 0;

    // Text input buffer -- captured from SDL_TEXTINPUT events for name entry.
    std::string text_input_buffer;

    // Input consumption flags -- set by high-priority systems (UI, pickups) to
    // prevent lower-priority systems (combat) from acting on the same input.
    // Reset at the start of each game tick.
    bool lmb_consumed = false;
    bool rmb_consumed = false;

    // Tile map -- populated by game-side world generation if the game uses a
    // 2D tile world; left empty otherwise (3D games leave both fields default).
    TileMap tile_map;
    TileConfig tile_config;

    // Steering parameters -- set by game-side config loading.
    SteeringConfig steering_config;

  private:
    entt::registry reg;
};
