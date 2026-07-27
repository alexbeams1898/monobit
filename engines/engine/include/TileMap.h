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

    // Pixel size of one tile in the atlas. Independent of TileMap::tile_size
    // (world-space tile size); the atlas can use any sub-tile resolution.
    // Default matches a 32x32 atlas.
    int atlas_tile_size = 32;

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
// TileMap -- the live 2-D grid owned by EntityManager.
// Stored as a flat vector (row-major) for cache-friendly traversal.
//
// Coordinate convention:
//   World position (wx, wy) -> tile cell (wx / tile_size, wy / tile_size).
//   Tile top-left corner in world space = (col * tile_size, row * tile_size).
//   TileMapRenderer uses top-left; all ECS systems use CENTER
//   (col*tile_size + tile_size/2, ...).
// ---------------------------------------------------------------------------
struct TileMap
{
    // World-space pixel size of one tile. Per-game: an action game wants a
    // coarse grid (32), an overworld exploration game wants the 8-16-bit
    // register (16). The game sets this before building the map; every system
    // that maps world<->cell reads it from the live TileMap instance.
    int tile_size = 32;

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
    std::vector<Tile> tiles; // row-major: tiles[row * width + col] -- the GROUND layer

    // Optional DECORATION stamps: sparse visuals drawn ABOVE the ground but BELOW
    // characters -- flowers, a rug, a pot on the rug. An ORDERED LIST, not a grid:
    // one cell may carry several stacked stamps, drawn in list (painter's) order,
    // exactly as the authoring tool composites them. Purely visual (collision lives
    // on `tiles`), and no "none" sentinel -- every entry is real, so any tile id
    // (including 0) can decorate.
    struct Decor
    {
        std::size_t cell = 0; // cellIndex(col, row)
        Tile tile;
    };
    std::vector<Decor> decoration;

    // Optional OVERHANG layer: sparse props drawn ABOVE characters so a character
    // can walk BEHIND them (tree canopies, tall tops). Same layout/size as `tiles`;
    // EMPTY by default (the renderer's overhang pass is then a no-op). Collision
    // lives on `tiles`; decoration/overhang are purely visual. tile_id 0 = "none".
    std::vector<Tile> overhang;

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
        const int tc = static_cast<int>(wx) / tile_size;
        const int tr = static_cast<int>(wy) / tile_size;
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

    // Flat row-major index of (col,row) -- the single source for the tiles/decoration/
    // overhang layout. Use this instead of open-coding `row * width + col` so producers
    // and consumers share one width and can't drift.
    std::size_t cellIndex(int col, int row) const
    {
        return static_cast<std::size_t>(row) * static_cast<std::size_t>(width) +
               static_cast<std::size_t>(col);
    }

    // DDA traversal state along one axis: which cell we're in, the
    // direction step (+1/-1), the t-parameter advance per cell-cross,
    // and the t-parameter at the next boundary crossing. Initialized
    // by ddaInit and advanced by ddaStep.
    struct DdaAxis
    {
        int cell = 0;
        int step = 1;
        float t_delta = 1e30f;
        float t_max = 1e30f;
    };

    static DdaAxis ddaInit(float origin, float delta, int tile_size)
    {
        DdaAxis a;
        a.cell = static_cast<int>(std::floor(origin / static_cast<float>(tile_size)));
        a.step = (delta >= 0.0f) ? 1 : -1;
        if (delta != 0.0f)
        {
            a.t_delta = std::abs(static_cast<float>(tile_size) / delta);
            const float boundary =
                static_cast<float>((delta >= 0.0f ? a.cell + 1 : a.cell) * tile_size);
            a.t_max = std::abs((boundary - origin) / delta);
        }
        return a;
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

        DdaAxis colAxis = ddaInit(x1, dx, tile_size);
        DdaAxis rowAxis = ddaInit(y1, dy, tile_size);
        const int endCol = static_cast<int>(std::floor(x2 / static_cast<float>(tile_size)));
        const int endRow = static_cast<int>(std::floor(y2 / static_cast<float>(tile_size)));

        while (true)
        {
            if (!in_bounds(colAxis.cell, rowAxis.cell) || !at(colAxis.cell, rowAxis.cell).walkable)
                return false;
            if (colAxis.cell == endCol && rowAxis.cell == endRow)
                return true;
            DdaAxis& nextAxis = (colAxis.t_max < rowAxis.t_max) ? colAxis : rowAxis;
            nextAxis.cell += nextAxis.step;
            nextAxis.t_max += nextAxis.t_delta;
        }
    }
};
