#pragma once

#include <cmath>
#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

// ---------------------------------------------------------------------------
// TileType — the four tile categories the engine understands.
// walkable is derived from TileType by TileMapLoader and stored on each Tile.
// ---------------------------------------------------------------------------
enum class TileType : uint8_t
{
    Floor = 0,     // open ground — walkable
    Wall = 1,      // solid block — blocks movement and pathfinding
    DoorFrame = 2, // reserved for future use (corridor/transition tile)
    Obstacle = 3,  // interior obstruction (pillar, crate) — blocks movement
};

// ---------------------------------------------------------------------------
// TileConfig — loaded once from config/tilemap.json.
// Maps tile_id → sprite path + walkable flag.
// Engine falls back to a colored rectangle if the sprite file is missing.
// ---------------------------------------------------------------------------
struct TileConfig
{
    struct Entry
    {
        std::string sprite;
        bool walkable = true;
    };
    std::unordered_map<int, Entry> tiles; // tile_id → visual + nav info
};

// ---------------------------------------------------------------------------
// Room — a parsed ASCII room template from config/rooms/*.room.
// Used by TileMapLoader during procedural generation; not stored at runtime.
// ---------------------------------------------------------------------------
struct Room
{
    int width = 0;
    int height = 0;
    std::vector<TileType> tiles; // row-major: tiles[y * width + x]

    struct SpawnPoint
    {
        int col;
        int row;
        char type; // 'E' = enemy, 'C' = chest
    };
    std::vector<SpawnPoint> spawn_points;

    std::string name; // filename, for debug logging
};

// ---------------------------------------------------------------------------
// TileMap — the live 2-D grid owned by EntityManager.
// Stored as a flat vector (row-major) for cache-friendly traversal.
//
// Coordinate convention:
//   World position (wx, wy) → tile cell (wx / TILE_SIZE, wy / TILE_SIZE).
//   Tile top-left corner in world space = (col * TILE_SIZE, row * TILE_SIZE).
//   TileMapRenderer uses top-left; all ECS systems use CENTER (col*32+16, row*32+16).
// ---------------------------------------------------------------------------
struct TileMap
{
    static constexpr int TILE_SIZE = 32;

    int width = 0;  // columns
    int height = 0; // rows

    struct Tile
    {
        TileType type = TileType::Wall;
        int tile_id = 1;       // index into TileConfig::tiles
        bool walkable = false; // pre-computed from type at generation time
    };
    std::vector<Tile> tiles; // row-major: tiles[row * width + col]

    // Spawn points collected from room template E/C markers.
    // Stored here for SpawnerSystem (#11) to consume; not acted on in this PR.
    struct SpawnPoint
    {
        float x, y; // world center of the tile
        char type;  // 'E' or 'C'
    };
    std::vector<SpawnPoint> spawn_points;

    uint32_t seed = 0; // generation seed - logged at startup for reproducibility

    bool valid() const
    {
        return width > 0 && height > 0;
    }

    Tile& at(int col, int row)
    {
        return tiles[row * width + col];
    }
    const Tile& at(int col, int row) const
    {
        return tiles[row * width + col];
    }

    bool in_bounds(int col, int row) const
    {
        return col >= 0 && col < width && row >= 0 && row < height;
    }

    // Returns true if the straight line from (x1,y1) to (x2,y2) passes through
    // only walkable tiles. Uses DDA grid traversal — visits every cell the segment
    // enters, never misses a cell, never revisits one.
    // Use this for LOS checks (aggro, hitbox validity) — O(tiles crossed).
    bool hasLineOfSight(float x1, float y1, float x2, float y2) const
    {
        const float dx = x2 - x1;
        const float dy = y2 - y1;
        if (dx == 0.0f && dy == 0.0f)
            return true;

        int col = static_cast<int>(std::floor(x1 / TILE_SIZE));
        int row = static_cast<int>(std::floor(y1 / TILE_SIZE));
        const int endCol = static_cast<int>(std::floor(x2 / TILE_SIZE));
        const int endRow = static_cast<int>(std::floor(y2 / TILE_SIZE));

        const int stepCol = (dx >= 0.0f) ? 1 : -1;
        const int stepRow = (dy >= 0.0f) ? 1 : -1;

        const float tDeltaCol = (dx != 0.0f) ? std::abs(static_cast<float>(TILE_SIZE) / dx) : 1e30f;
        const float tDeltaRow = (dy != 0.0f) ? std::abs(static_cast<float>(TILE_SIZE) / dy) : 1e30f;

        // t at the first vertical and horizontal boundary crossing.
        const float colBoundary = static_cast<float>((dx >= 0.0f ? col + 1 : col) * TILE_SIZE);
        const float rowBoundary = static_cast<float>((dy >= 0.0f ? row + 1 : row) * TILE_SIZE);

        float tMaxCol = (dx != 0.0f) ? std::abs((colBoundary - x1) / dx) : 1e30f;
        float tMaxRow = (dy != 0.0f) ? std::abs((rowBoundary - y1) / dy) : 1e30f;

        while (true)
        {
            if (!in_bounds(col, row) || !at(col, row).walkable)
                return false;
            if (col == endCol && row == endRow)
                return true;

            if (tMaxCol < tMaxRow)
            {
                col += stepCol;
                tMaxCol += tDeltaCol;
            }
            else
            {
                row += stepRow;
                tMaxRow += tDeltaRow;
            }
        }
    }
};
