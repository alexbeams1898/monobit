#pragma once

#include "Room.h"
#include "TileMap.h"
#include "ecs/EntityManager.h"

#include <cstdint>
#include <random>
#include <string>
#include <utility>
#include <vector>

// ---------------------------------------------------------------------------
// TileMapLoader — parses ASCII room templates and generates the world map.
//
// Usage (game startup):
//   auto [px, py] = TileMapLoader::generate(em, "config/tilemap.json",
//                                            "config/rooms");
//   // px, py = world-space center of the first placed room (player spawn)
//
// The generator writes em.tile_map and em.tile_config.
// MovementSystem, CollisionSystem, and FlowFieldSystem query em.tile_map directly —
// no ECS wall entities are spawned, keeping the sparse set small.
//
// A seed of 0 (default) picks a timestamp-based seed at runtime.
// Pass an explicit seed for deterministic output (e.g. in tests).
// ---------------------------------------------------------------------------
class TileMapLoader
{
  public:
    // Parse a single ASCII room template. Public so tests can call it directly.
    // 'text' is the raw file content (newline-separated rows).
    // 'name' is used for debug logging only.
    static Room parseRoom(const std::string& text, const std::string& name = "");

    // Full generation pipeline:
    //   1. Load config/tilemap.json  → TileConfig + map dimensions
    //   2. Load all *.room files     → Room pool
    //   3. Place rooms randomly      → stamp tiles onto TileMap
    //   4. Connect rooms             → L-shaped floor corridors
    //   5. Write em.tile_map + em.tile_config
    //   6. Log seed to stdout
    // Returns (playerX, playerY) — world-space center of the first placed room.
    static std::pair<float, float> generate(EntityManager& em, const std::string& tilemapConfigPath,
                                            const std::string& roomsDir, uint32_t seed = 0,
                                            int level = 0);

  private:
    static TileConfig loadConfig(const std::string& path);
    static std::vector<Room> loadRooms(const std::string& dir);

    // Place rooms randomly onto the map. Centers are appended to 'centers'.
    // TODO: upgrade to BSP partitioning (issue #XX)
    static void placeRooms(TileMap& map, const std::vector<Room>& rooms, std::mt19937& rng,
                           int count, std::vector<std::pair<int, int>>& centers);

    // Connect adjacent room centers with L-shaped floor corridors.
    // corridor_half = tiles from center to edge (e.g. 2 = 5 tiles wide).
    static void connectRooms(TileMap& map, const std::vector<std::pair<int, int>>& centers,
                             int corridor_half);

    // Stamp a room's tiles into the TileMap at (origin_col, origin_row).
    static void stampRoom(TileMap& map, const Room& room, int origin_col, int origin_row);

    // Check whether placing 'room' at (col, row) would overlap existing non-wall
    // tiles (with a 2-tile border margin).
    static bool canPlace(const TileMap& map, const Room& room, int col, int row);
};
