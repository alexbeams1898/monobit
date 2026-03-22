#include "SpawnUtils.h"

#include <cmath>
#include <ctime>
#include <limits>
#include <vector>

namespace SpawnUtils
{

std::mt19937& getRng()
{
    static std::mt19937 rng(static_cast<std::mt19937::result_type>(std::time(nullptr)));
    return rng;
}

// Pick a random candidate from the vector. Returns false if empty.
static bool pickRandom(const std::vector<std::pair<int, int>>& candidates, float ts, float& out_x,
                       float& out_y)
{
    if (candidates.empty())
        return false;

    auto& rng = getRng();
    std::uniform_int_distribution<std::size_t> dist(0, candidates.size() - 1);
    const auto& chosen = candidates[dist(rng)];
    out_x = static_cast<float>(chosen.first) * ts + ts * 0.5f;
    out_y = static_cast<float>(chosen.second) * ts + ts * 0.5f;
    return true;
}

// Collect walkable tiles in a rectangular region that satisfy distance constraints.
static void collectCandidates(const TileMap& tm, int startCol, int startRow, int cols, int rows,
                              float px, float py, float minDistSq,
                              std::vector<std::pair<int, int>>& candidates,
                              float maxDistSq = std::numeric_limits<float>::max())
{
    const float ts = static_cast<float>(TileMap::TILE_SIZE);
    for (int r = startRow; r < startRow + rows; ++r)
    {
        for (int c = startCol; c < startCol + cols; ++c)
        {
            if (!tm.in_bounds(c, r) || !tm.at(c, r).walkable)
                continue;
            const float cx = static_cast<float>(c) * ts + ts * 0.5f;
            const float cy = static_cast<float>(r) * ts + ts * 0.5f;
            const float dx = cx - px;
            const float dy = cy - py;
            const float dSq = dx * dx + dy * dy;
            if (dSq >= minDistSq && dSq <= maxDistSq)
                candidates.emplace_back(c, r);
        }
    }
}

// Find nearest room to a point by squared center distance.
static int findNearestRoom(const TileMap& tm, float px, float py)
{
    const float ts = static_cast<float>(TileMap::TILE_SIZE);
    float bestDist = std::numeric_limits<float>::max();
    int roomIdx = -1;
    for (int i = 0; i < static_cast<int>(tm.placed_rooms.size()); ++i)
    {
        const auto& rm = tm.placed_rooms[static_cast<std::size_t>(i)];
        const float cx = (static_cast<float>(rm.col) + static_cast<float>(rm.width) * 0.5f) * ts;
        const float cy = (static_cast<float>(rm.row) + static_cast<float>(rm.height) * 0.5f) * ts;
        const float dx = cx - px;
        const float dy = cy - py;
        const float d = dx * dx + dy * dy;
        if (d < bestDist)
        {
            bestDist = d;
            roomIdx = i;
        }
    }
    return roomIdx;
}

bool findSpawnPosition(const TileMap& tm, float px, float py, float nearDist, float farDist,
                       float& out_x, float& out_y)
{
    if (!tm.valid())
        return false;

    const float ts = static_cast<float>(TileMap::TILE_SIZE);

    // Try room-scoped spawning first.
    int roomIdx = tm.findRoomAt(px, py);

    // If player is in a corridor, pick the nearest room by center distance.
    if (roomIdx < 0 && !tm.placed_rooms.empty())
        roomIdx = findNearestRoom(tm, px, py);

    // Room-scoped candidate scan.
    if (roomIdx >= 0)
    {
        const auto& rm = tm.placed_rooms[static_cast<std::size_t>(roomIdx)];

        std::vector<std::pair<int, int>> candidates;
        collectCandidates(tm, rm.col, rm.row, rm.width, rm.height, px, py, nearDist * nearDist,
                          candidates);

        if (pickRandom(candidates, ts, out_x, out_y))
            return true;

        // Room too small for nearDist -- retry with a smaller exclusion zone.
        constexpr float kFallbackMin = 96.0f; // 3 tiles
        candidates.clear();
        collectCandidates(tm, rm.col, rm.row, rm.width, rm.height, px, py,
                          kFallbackMin * kFallbackMin, candidates);

        if (pickRandom(candidates, ts, out_x, out_y))
            return true;
    }

    // Fallback: full-map distance-ring scan (original behavior).
    std::vector<std::pair<int, int>> candidates;
    collectCandidates(tm, 0, 0, tm.width, tm.height, px, py, nearDist * nearDist, candidates,
                      farDist * farDist);

    return pickRandom(candidates, ts, out_x, out_y);
}

bool findSpawnInRoom(const TileMap& tm, int roomIdx, float px, float py, float nearDist,
                     float& out_x, float& out_y)
{
    if (!tm.valid() || roomIdx < 0 || roomIdx >= static_cast<int>(tm.placed_rooms.size()))
        return false;

    const float ts = static_cast<float>(TileMap::TILE_SIZE);
    const auto& rm = tm.placed_rooms[static_cast<std::size_t>(roomIdx)];

    // Check if the player is in this room -- if so, enforce nearDist exclusion.
    const int playerRoom = tm.findRoomAt(px, py);
    const float minDist = (playerRoom == roomIdx) ? nearDist : 0.0f;

    std::vector<std::pair<int, int>> candidates;
    collectCandidates(tm, rm.col, rm.row, rm.width, rm.height, px, py, minDist * minDist,
                      candidates);

    if (pickRandom(candidates, ts, out_x, out_y))
        return true;

    // If nearDist was too large for this room, retry with fallback minimum.
    if (minDist > 96.0f)
    {
        constexpr float kFallbackMin = 96.0f;
        candidates.clear();
        collectCandidates(tm, rm.col, rm.row, rm.width, rm.height, px, py,
                          kFallbackMin * kFallbackMin, candidates);
        if (pickRandom(candidates, ts, out_x, out_y))
            return true;
    }

    return false;
}

static int sSpawnRoomCounter = 0;

int nextSpawnRoom(int room_count, int skip_room)
{
    if (room_count <= 0)
        return 0;
    int idx = sSpawnRoomCounter % room_count;
    sSpawnRoomCounter++;
    if (idx == skip_room)
    {
        idx = sSpawnRoomCounter % room_count;
        sSpawnRoomCounter++;
    }
    return idx;
}

void resetSpawnRoomCounter()
{
    sSpawnRoomCounter = 0;
}

} // namespace SpawnUtils