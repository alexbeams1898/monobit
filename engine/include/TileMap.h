#pragma once

#include <cmath>
#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

// ---------------------------------------------------------------------------
// TileConfig -- loaded once from config/tilemap.json.
// Maps tile_id -> sprite path + walkable flag + visual info.
// Engine falls back to a colored rectangle if no tileset is configured.
// ---------------------------------------------------------------------------
struct TileConfig
{
    struct Entry
    {
        std::string sprite;
        bool walkable = true;
    };
    std::unordered_map<int, Entry> tiles; // tile_id -> sprite + nav info

    // Tileset atlas -- a single PNG containing all tile sprites.
    // When non-empty, TileMapRenderer uses textured quads instead of flat colors.
    std::string tileset_path;

    // Per-tile-id visual: atlas position + fallback RGB color.
    struct TileVisual
    {
        int uv_col = 0;
        int uv_row = 0;
        float r = 0.2f, g = 0.2f, b = 0.2f; // flat color when no tileset
    };
    std::unordered_map<int, TileVisual> tile_visuals;
};

// ---------------------------------------------------------------------------
// Room -- a parsed ASCII room template from config/rooms/*.room.
// Used by TileMapLoader during procedural generation; not stored at runtime.
// ---------------------------------------------------------------------------
struct Room
{
    int width = 0;
    int height = 0;
    std::vector<int> tiles; // row-major tile_ids: tiles[y * width + x]

    struct SpawnPoint
    {
        int col;
        int row;
        char type; // single-char marker from .room template
    };
    std::vector<SpawnPoint> spawn_points;

    std::string name; // filename, for debug logging
};

// ---------------------------------------------------------------------------
// TileMap -- the live 2-D grid owned by EntityManager.
// Stored as a flat vector (row-major) for cache-friendly traversal.
//
// Coordinate convention:
//   World position (wx, wy) -> tile cell (wx / TILE_SIZE, wy / TILE_SIZE).
//   Tile top-left corner in world space = (col * TILE_SIZE, row * TILE_SIZE).
//   TileMapRenderer uses top-left; all ECS systems use CENTER (col*32+16, row*32+16).
// ---------------------------------------------------------------------------
struct TileMap
{
    static constexpr int TILE_SIZE = 32;

    // Tile IDs are game-defined integers; engine only uses SOLID_ID/WALKABLE_ID
    // for procedural generation (corridor carving, initial fill).
    static constexpr int SOLID_ID = 1;
    static constexpr int WALKABLE_ID = 0;

    int width = 0;  // columns
    int height = 0; // rows

    struct Tile
    {
        int tile_id = SOLID_ID;
        bool walkable = false; // pre-computed at generation time
    };
    std::vector<Tile> tiles; // row-major: tiles[row * width + col]

    // Spawn points collected from room template markers.
    struct SpawnPoint
    {
        float x, y; // world center of the tile
        char type;  // marker character from .room template
    };
    std::vector<SpawnPoint> spawn_points;

    // Placed rooms -- stored during generation for runtime queries
    // (e.g. "which room is the player in?"). Tile coordinates.
    struct PlacedRoom
    {
        int col, row;      // top-left tile coordinate
        int width, height; // size in tiles
    };
    std::vector<PlacedRoom> placed_rooms;

    // Returns index into placed_rooms for the room containing world position
    // (wx, wy), or -1 if the position is in a corridor or outside any room.
    int findRoomAt(float wx, float wy) const
    {
        const int tc = static_cast<int>(wx) / TILE_SIZE;
        const int tr = static_cast<int>(wy) / TILE_SIZE;
        for (int i = 0; i < static_cast<int>(placed_rooms.size()); ++i)
        {
            const auto& rm = placed_rooms[static_cast<std::size_t>(i)];
            if (tc >= rm.col && tc < rm.col + rm.width && tr >= rm.row && tr < rm.row + rm.height)
                return i;
        }
        return -1;
    }

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
    // only walkable tiles. Uses DDA grid traversal -- visits every cell the segment
    // enters, never misses a cell, never revisits one.
    // Use this for LOS checks (aggro, hitbox validity) -- O(tiles crossed).
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

    // Like hasLineOfSight but returns the parameter t in [0, 1] where the
    // segment first enters a non-walkable tile, or 1.0 if it never does.
    // Used by swept projectile collision to order enemy hits vs wall hits along
    // the same segment.
    float firstWallHitT(float x1, float y1, float x2, float y2) const
    {
        const float dx = x2 - x1;
        const float dy = y2 - y1;
        if (dx == 0.0f && dy == 0.0f)
            return 1.0f;

        int col = static_cast<int>(std::floor(x1 / TILE_SIZE));
        int row = static_cast<int>(std::floor(y1 / TILE_SIZE));
        const int endCol = static_cast<int>(std::floor(x2 / TILE_SIZE));
        const int endRow = static_cast<int>(std::floor(y2 / TILE_SIZE));

        const int stepCol = (dx >= 0.0f) ? 1 : -1;
        const int stepRow = (dy >= 0.0f) ? 1 : -1;

        const float tDeltaCol = (dx != 0.0f) ? std::abs(static_cast<float>(TILE_SIZE) / dx) : 1e30f;
        const float tDeltaRow = (dy != 0.0f) ? std::abs(static_cast<float>(TILE_SIZE) / dy) : 1e30f;

        const float colBoundary = static_cast<float>((dx >= 0.0f ? col + 1 : col) * TILE_SIZE);
        const float rowBoundary = static_cast<float>((dy >= 0.0f ? row + 1 : row) * TILE_SIZE);

        float tMaxCol = (dx != 0.0f) ? std::abs((colBoundary - x1) / dx) : 1e30f;
        float tMaxRow = (dy != 0.0f) ? std::abs((rowBoundary - y1) / dy) : 1e30f;

        float lastT = 0.0f;
        while (true)
        {
            if (!in_bounds(col, row) || !at(col, row).walkable)
                return lastT;
            if (col == endCol && row == endRow)
                return 1.0f;

            if (tMaxCol < tMaxRow)
            {
                lastT = tMaxCol;
                col += stepCol;
                tMaxCol += tDeltaCol;
            }
            else
            {
                lastT = tMaxRow;
                row += stepRow;
                tMaxRow += tDeltaRow;
            }
        }
    }
};
