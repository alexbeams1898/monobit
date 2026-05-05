#pragma once

#include <string>
#include <vector>

// ---------------------------------------------------------------------------
// Room -- a parsed ASCII room template from config/rooms/*.room.
// Used by TileMapLoader during procedural generation; not stored at runtime.
// Game-side because the room template format and procgen pipeline are
// prison-escape-game-specific.
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
