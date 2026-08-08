#include "Walkable.h"

#include "ecs/Components.h"
#include "ecs/EntityManager.h"

#include <cmath>

namespace world
{

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

bool stepBlocked(const EntityManager& em, float& pos, float delta, bool horizontal, float otherAxis,
                 float w, float h)
{
    const float want = pos + delta;
    const bool ok =
        horizontal ? boxFree(em, want, otherAxis, w, h) : boxFree(em, otherAxis, want, w, h);
    if (ok)
        pos = want;
    return ok;
}

} // namespace world
