#include "ops/NavUtils.h"

#include "ecs/Components.h"
#include "ecs/EntityManager.h"

#include <array>
#include <cmath>
#include <set>
#include <utility>
#include <vector>

namespace world
{
namespace
{
// How far out a misplaced thing is searched for standing room, in tiles. Generous: a floor's
// space is one connected piece, so anything this far from open ground is a map that failed to
// build rather than a spot that needs nudging.
constexpr int kSearchRings = 16;
} // namespace

bool walkable(const EntityManager& em, float x, float y)
{
    const TileMap& map = em.tile_map;
    if (map.tile_size <= 0)
        return true;
    const int col = static_cast<int>(x) / map.tile_size;
    const int row = static_cast<int>(y) / map.tile_size;
    if (col < 0 || row < 0 || col >= map.width || row >= map.height)
        return false;
    // Widen BEFORE multiplying, not after: the index is computed in the wider type rather than
    // overflowing as an int and being widened once the damage is done.
    const std::size_t index = static_cast<std::size_t>(row) * static_cast<std::size_t>(map.width) +
                              static_cast<std::size_t>(col);
    return map.tiles[index].walkable;
}

// HOW MUCH OF THE BOX IS IN SOLID -- the count of overlapped tiles that are not walkable, off
// the map included. boxFree is this being zero; the count itself is what tells a thing already
// inside the architecture which way is out.
int blockedTiles(const EntityManager& em, float cx, float cy, float w, float h)
{
    const TileMap& map = em.tile_map;
    if (map.tile_size <= 0)
        return 0;
    const auto ts = static_cast<float>(map.tile_size);
    const float hw = w * 0.5f;
    const float hh = h * 0.5f;
    const int cmin = static_cast<int>(std::floor((cx - hw) / ts));
    const int cmax = static_cast<int>(std::floor((cx + hw) / ts));
    const int rmin = static_cast<int>(std::floor((cy - hh) / ts));
    const int rmax = static_cast<int>(std::floor((cy + hh) / ts));
    int blocked = 0;
    for (int r = rmin; r <= rmax; ++r)
        for (int c = cmin; c <= cmax; ++c)
        {
            if (c < 0 || r < 0 || c >= map.width || r >= map.height ||
                !map.tiles[static_cast<std::size_t>(r) * static_cast<std::size_t>(map.width) +
                           static_cast<std::size_t>(c)]
                     .walkable)
                ++blocked;
        }
    return blocked;
}

bool boxFree(const EntityManager& em, float cx, float cy, float w, float h)
{
    const TileMap& map = em.tile_map;
    if (map.tile_size <= 0)
        return true;
    const auto ts = static_cast<float>(map.tile_size);
    const float hw = w * 0.5f;
    const float hh = h * 0.5f;
    // Every tile the box overlaps, not just the one under the centre -- a box spanning a tile
    // boundary must respect both tiles.
    const int cmin = static_cast<int>(std::floor((cx - hw) / ts));
    const int cmax = static_cast<int>(std::floor((cx + hw) / ts));
    const int rmin = static_cast<int>(std::floor((cy - hh) / ts));
    const int rmax = static_cast<int>(std::floor((cy + hh) / ts));
    for (int r = rmin; r <= rmax; ++r)
        for (int c = cmin; c <= cmax; ++c)
        {
            if (c < 0 || r < 0 || c >= map.width || r >= map.height)
                return false; // off the floor counts as wall
            if (!map.tiles[static_cast<std::size_t>(r) * static_cast<std::size_t>(map.width) +
                           static_cast<std::size_t>(c)]
                     .walkable)
                return false;
        }
    return true;
}

namespace
{
// THE NEAREST TILE CENTRE SATISFYING `ok`, rings outward from (x, y) and the spot itself first.
// A whole ring is measured before it is accepted -- a ring's corner is further away than its
// edge, and taking the first cell in scan order would step past a closer one.
template <typename Fn> bool nearestTile(const EntityManager& em, float& x, float& y, Fn ok)
{
    if (ok(x, y))
        return true;
    const TileMap& map = em.tile_map;
    if (map.tile_size <= 0)
        return true;
    const auto ts = static_cast<float>(map.tile_size);
    const int col = static_cast<int>(std::floor(x / ts));
    const int row = static_cast<int>(std::floor(y / ts));
    for (int ring = 1; ring <= kSearchRings; ++ring)
    {
        float bestX = 0.0f;
        float bestY = 0.0f;
        float best = -1.0f;
        for (int r = row - ring; r <= row + ring; ++r)
            for (int c = col - ring; c <= col + ring; ++c)
            {
                if (std::abs(r - row) != ring && std::abs(c - col) != ring)
                    continue; // interior: already covered by a tighter ring
                const float cx = (static_cast<float>(c) + 0.5f) * ts;
                const float cy = (static_cast<float>(r) + 0.5f) * ts;
                if (!ok(cx, cy))
                    continue;
                const float d = (cx - x) * (cx - x) + (cy - y) * (cy - y);
                if (best < 0.0f || d < best)
                {
                    best = d;
                    bestX = cx;
                    bestY = cy;
                }
            }
        if (best >= 0.0f)
        {
            x = bestX;
            y = bestY;
            return true;
        }
    }
    return false;
}
} // namespace

bool freeSpotNear(const EntityManager& em, float& x, float& y, float w, float h)
{
    return nearestTile(em, x, y, [&](float cx, float cy) { return boxFree(em, cx, cy, w, h); });
}

bool archSpotNear(const EntityManager& em, float& x, float& y, float w, float h, Side side,
                  int reach, int depth)
{
    return nearestTile(
        em, x, y, [&](float cx, float cy)
        { return boxFree(em, cx, cy, w, h) && wallFace(em, cx, cy, side, reach, depth) >= 0.0f; });
}

Step stepOf(Side side)
{
    switch (side)
    {
    case Side::North:
        return {0, -1};
    case Side::South:
        return {0, 1};
    case Side::East:
        return {1, 0};
    case Side::West:
        return {-1, 0};
    }
    return {0, -1};
}

Side opposite(Side side)
{
    switch (side)
    {
    case Side::North:
        return Side::South;
    case Side::South:
        return Side::North;
    case Side::East:
        return Side::West;
    case Side::West:
        return Side::East;
    }
    return Side::South;
}

float wallFace(const EntityManager& em, float x, float y, Side side, int reach, int depth)
{
    const auto ts = static_cast<float>(em.tile_map.tile_size);
    if (ts <= 0.0f)
        return -1.0f;
    const auto [dc, dr] = stepOf(side);
    for (int step = 1; step <= reach; ++step)
    {
        const float wx = x + static_cast<float>(dc * step) * ts;
        const float wy = y + static_cast<float>(dr * step) * ts;
        if (walkable(em, wx, wy))
            continue; // still open floor between the spot and whatever stands beyond it
        for (int back = 1; back < depth; ++back)
            if (walkable(em, wx + static_cast<float>(dc * back) * ts,
                         wy + static_cast<float>(dr * back) * ts))
                return -1.0f; // a partition, not a wall
        // The face's NEAR edge -- the side of it he is standing on.
        const float found = dc != 0 ? wx : wy;
        const float edge = std::floor(found / ts) * ts;
        return (dc + dr) > 0 ? edge : edge + ts;
    }
    return -1.0f;
}

namespace
{

// The cell of the first solid standing `side` of (x, y), or {-1,-1} within `reach`.
std::pair<int, int> faceCell(const EntityManager& em, float x, float y, Side side, int reach)
{
    const TileMap& map = em.tile_map;
    const auto ts = static_cast<float>(map.tile_size);
    const auto [dc, dr] = stepOf(side);
    for (int step = 1; step <= reach; ++step)
    {
        const float wx = x + static_cast<float>(dc * step) * ts;
        const float wy = y + static_cast<float>(dr * step) * ts;
        const int c = static_cast<int>(wx) / map.tile_size;
        const int r = static_cast<int>(wy) / map.tile_size;
        if (wx < 0.0f || wy < 0.0f || c >= map.width || r >= map.height)
            return {-1, -1};
        if (!walkable(em, wx, wy))
            return {c, r};
    }
    return {-1, -1};
}

// The connected solid containing (col, row), as a bounding box -- or nothing where it reaches the
// outside of the map (the room's own edge) or runs past `cap` (architecture, not a slab).
Slab floodSlab(const EntityManager& em, int col, int row, int cap)
{
    const TileMap& map = em.tile_map;
    const auto ts = static_cast<float>(map.tile_size);
    std::vector<std::pair<int, int>> open{{col, row}};
    std::set<std::pair<int, int>> seen{{col, row}};
    int c0 = col, c1 = col, r0 = row, r1 = row;
    while (!open.empty())
    {
        if (static_cast<int>(seen.size()) > cap)
            return {};
        const auto [c, r] = open.back();
        open.pop_back();
        c0 = std::min(c0, c);
        c1 = std::max(c1, c);
        r0 = std::min(r0, r);
        r1 = std::max(r1, r);
        constexpr std::array<std::array<int, 2>, 4> kAround = {
            {{{1, 0}}, {{-1, 0}}, {{0, 1}}, {{0, -1}}}};
        for (const auto& step : kAround)
        {
            const int nc = c + step[0];
            const int nr = r + step[1];
            if (nc < 0 || nr < 0 || nc >= map.width || nr >= map.height)
                return {}; // it reaches the outside: this is the room's edge
            const float wx = (static_cast<float>(nc) + 0.5f) * ts;
            const float wy = (static_cast<float>(nr) + 0.5f) * ts;
            if (walkable(em, wx, wy) || !seen.emplace(nc, nr).second)
                continue;
            open.emplace_back(nc, nr);
        }
    }
    return Slab{c1 - c0 + 1, r1 - r0 + 1};
}

} // namespace

bool rockAllTheWayOut(const EntityManager& em, float x, float y, Side side)
{
    const TileMap& map = em.tile_map;
    if (map.tile_size <= 0)
        return false;
    const auto ts = static_cast<float>(map.tile_size);
    const auto [dc, dr] = stepOf(side);
    bool inRock = false;
    for (int step = 1;; ++step)
    {
        const float wx = x + static_cast<float>(dc * step) * ts;
        const float wy = y + static_cast<float>(dr * step) * ts;
        const int c = static_cast<int>(wx) / map.tile_size;
        const int r = static_cast<int>(wy) / map.tile_size;
        if (wx < 0.0f || wy < 0.0f || c >= map.width || r >= map.height)
            return inRock; // left the map still in rock: there is nothing behind this wall
        if (walkable(em, wx, wy))
        {
            // Floor after rock is the far side of a divider. Floor BEFORE it is just the gap he
            // is standing in, since a hole may stand back from the wall it is cut into.
            if (inRock)
                return false;
            continue;
        }
        inRock = true;
    }
}

Slab slabBeyond(const EntityManager& em, float x, float y, Side side, int reach, int cap)
{
    const TileMap& map = em.tile_map;
    if (map.tile_size <= 0 || map.width <= 0 || map.height <= 0)
        return {};
    const auto [col, row] = faceCell(em, x, y, side, reach);
    return col < 0 ? Slab{} : floodSlab(em, col, row, cap);
}

bool stepBlocked(const EntityManager& em, float& pos, float delta, bool horizontal, float otherAxis,
                 float w, float h)
{
    const float want = pos + delta;
    const bool ok =
        horizontal ? boxFree(em, want, otherAxis, w, h) : boxFree(em, otherAxis, want, w, h);
    if (ok)
    {
        pos = want;
        return true;
    }
    // ALREADY INSIDE THE ARCHITECTURE. The rule above answers "may I enter that space", which
    // assumes the space being left is one you were allowed to be in -- and where that is false
    // it refuses every direction at once, so a thing that got in can never get out. Defined for
    // that case too: a move is allowed while it is climbing OUT, measured by the tiles the box
    // overlaps. Movement then walks anything misplaced back into the world instead of sealing
    // it wherever it landed.
    const bool stuck =
        horizontal ? !boxFree(em, pos, otherAxis, w, h) : !boxFree(em, otherAxis, pos, w, h);
    if (!stuck)
        return false;
    const int was = horizontal ? blockedTiles(em, pos, otherAxis, w, h)
                               : blockedTiles(em, otherAxis, pos, w, h);
    const int now = horizontal ? blockedTiles(em, want, otherAxis, w, h)
                               : blockedTiles(em, otherAxis, want, w, h);
    if (now > was)
        return false; // deeper in; that is not the way out
    pos = want;
    return true;
}

} // namespace world
