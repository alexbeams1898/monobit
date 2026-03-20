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
    {
        float bestDist = std::numeric_limits<float>::max();
        for (int i = 0; i < static_cast<int>(tm.placed_rooms.size()); ++i)
        {
            const auto& rm = tm.placed_rooms[static_cast<std::size_t>(i)];
            const float cx =
                (static_cast<float>(rm.col) + static_cast<float>(rm.width) * 0.5f) * ts;
            const float cy =
                (static_cast<float>(rm.row) + static_cast<float>(rm.height) * 0.5f) * ts;
            const float dx = cx - px;
            const float dy = cy - py;
            const float d = dx * dx + dy * dy;
            if (d < bestDist)
            {
                bestDist = d;
                roomIdx = i;
            }
        }
    }

    // Room-scoped candidate scan.
    if (roomIdx >= 0)
    {
        const auto& rm = tm.placed_rooms[static_cast<std::size_t>(roomIdx)];
        const float nearSq = nearDist * nearDist;

        std::vector<std::pair<int, int>> candidates;
        for (int r = rm.row; r < rm.row + rm.height; ++r)
        {
            for (int c = rm.col; c < rm.col + rm.width; ++c)
            {
                if (!tm.in_bounds(c, r) || !tm.at(c, r).walkable)
                    continue;
                const float cx = static_cast<float>(c) * ts + ts * 0.5f;
                const float cy = static_cast<float>(r) * ts + ts * 0.5f;
                const float dx = cx - px;
                const float dy = cy - py;
                if (dx * dx + dy * dy >= nearSq)
                    candidates.emplace_back(c, r);
            }
        }

        if (pickRandom(candidates, ts, out_x, out_y))
            return true;

        // Room too small for nearDist — retry with a smaller exclusion zone.
        constexpr float kFallbackMin = 96.0f; // 3 tiles
        const float fallbackSq = kFallbackMin * kFallbackMin;
        candidates.clear();
        for (int r = rm.row; r < rm.row + rm.height; ++r)
        {
            for (int c = rm.col; c < rm.col + rm.width; ++c)
            {
                if (!tm.in_bounds(c, r) || !tm.at(c, r).walkable)
                    continue;
                const float cx = static_cast<float>(c) * ts + ts * 0.5f;
                const float cy = static_cast<float>(r) * ts + ts * 0.5f;
                const float dx = cx - px;
                const float dy = cy - py;
                if (dx * dx + dy * dy >= fallbackSq)
                    candidates.emplace_back(c, r);
            }
        }

        if (pickRandom(candidates, ts, out_x, out_y))
            return true;
    }

    // Fallback: full-map distance-ring scan (original behavior).
    const float nearSq = nearDist * nearDist;
    const float farSq = farDist * farDist;

    std::vector<std::pair<int, int>> candidates;
    for (int r = 0; r < tm.height; ++r)
    {
        for (int c = 0; c < tm.width; ++c)
        {
            if (!tm.at(c, r).walkable)
                continue;
            const float cx = static_cast<float>(c) * ts + ts * 0.5f;
            const float cy = static_cast<float>(r) * ts + ts * 0.5f;
            const float dx = cx - px;
            const float dy = cy - py;
            const float dSq = dx * dx + dy * dy;
            if (dSq >= nearSq && dSq <= farSq)
                candidates.emplace_back(c, r);
        }
    }

    return pickRandom(candidates, ts, out_x, out_y);
}

bool findSpawnInRoom(const TileMap& tm, int roomIdx, float px, float py, float nearDist,
                     float& out_x, float& out_y)
{
    if (!tm.valid() || roomIdx < 0 || roomIdx >= static_cast<int>(tm.placed_rooms.size()))
        return false;

    const float ts = static_cast<float>(TileMap::TILE_SIZE);
    const auto& rm = tm.placed_rooms[static_cast<std::size_t>(roomIdx)];

    // Check if the player is in this room — if so, enforce nearDist exclusion.
    const int playerRoom = tm.findRoomAt(px, py);
    const float minDist = (playerRoom == roomIdx) ? nearDist : 0.0f;
    const float minSq = minDist * minDist;

    std::vector<std::pair<int, int>> candidates;
    for (int r = rm.row; r < rm.row + rm.height; ++r)
    {
        for (int c = rm.col; c < rm.col + rm.width; ++c)
        {
            if (!tm.in_bounds(c, r) || !tm.at(c, r).walkable)
                continue;
            const float cx = static_cast<float>(c) * ts + ts * 0.5f;
            const float cy = static_cast<float>(r) * ts + ts * 0.5f;
            const float dx = cx - px;
            const float dy = cy - py;
            if (dx * dx + dy * dy >= minSq)
                candidates.emplace_back(c, r);
        }
    }

    if (pickRandom(candidates, ts, out_x, out_y))
        return true;

    // If nearDist was too large for this room, retry with fallback minimum.
    if (minDist > 96.0f)
    {
        constexpr float kFallbackMin = 96.0f;
        const float fallbackSq = kFallbackMin * kFallbackMin;
        candidates.clear();
        for (int r = rm.row; r < rm.row + rm.height; ++r)
        {
            for (int c = rm.col; c < rm.col + rm.width; ++c)
            {
                if (!tm.in_bounds(c, r) || !tm.at(c, r).walkable)
                    continue;
                const float cx = static_cast<float>(c) * ts + ts * 0.5f;
                const float cy = static_cast<float>(r) * ts + ts * 0.5f;
                const float dx = cx - px;
                const float dy = cy - py;
                if (dx * dx + dy * dy >= fallbackSq)
                    candidates.emplace_back(c, r);
            }
        }
        if (pickRandom(candidates, ts, out_x, out_y))
            return true;
    }

    return false;
}

static int sSpawnRoomCounter = 0;

int nextSpawnRoom(int room_count)
{
    if (room_count <= 0)
        return 0;
    const int idx = sSpawnRoomCounter % room_count;
    sSpawnRoomCounter++;
    return idx;
}

void resetSpawnRoomCounter()
{
    sSpawnRoomCounter = 0;
}

} // namespace SpawnUtils
