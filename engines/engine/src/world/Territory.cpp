#include "world/Territory.h"

#include <algorithm>
#include <cmath>
#include <cstdio>

namespace engine::world
{

namespace
{
// Append-only registry: stable pointers for the program lifetime
// (resident-all-scenes architecture). reserve() generously at first
// register so subsequent push_backs don't invalidate addresses.
std::vector<Territory> sTerritories;
bool sReserved = false;

// Transform a world point into the OBB's local frame. Identity
// orientation collapses to a translate-only operation, so AABB-mode
// territories pay zero cost beyond a vec3 subtract + conjugate-rotate
// (which is one vector-quat multiply -- cheap).
glm::vec3 worldToLocal(const Territory& t, const glm::vec3& world_p)
{
    const glm::vec3 rel = world_p - t.center;
    return glm::conjugate(t.orientation) * rel;
}

// Min-inclusive, max-exclusive containment in the OBB's local frame.
// A point on a face is in the lower-edge volume only, guaranteeing
// exactly one territory claims any boundary position.
bool containsPoint(const Territory& t, const glm::vec3& world_p)
{
    const glm::vec3 local = worldToLocal(t, world_p);
    return local.x >= -t.half_extents.x && local.x < t.half_extents.x &&
           local.y >= -t.half_extents.y && local.y < t.half_extents.y &&
           local.z >= -t.half_extents.z && local.z < t.half_extents.z;
}

float volumeOf(const Territory& t)
{
    return 8.0f * t.half_extents.x * t.half_extents.y * t.half_extents.z;
}

} // namespace

void registerTerritory(const Territory& t)
{
    if (!sReserved)
    {
        sTerritories.reserve(64);
        sReserved = true;
    }
    sTerritories.push_back(t);
    const glm::quat& q = t.orientation;
    const bool is_identity = std::abs(q.w - 1.0f) < 1e-5f && std::abs(q.x) < 1e-5f &&
                             std::abs(q.y) < 1e-5f && std::abs(q.z) < 1e-5f;
    if (is_identity)
        std::fprintf(stderr,
                     "[territory] registered '%s' owner='%s' AABB center=(%.1f,%.1f,%.1f) "
                     "half=(%.1f,%.1f,%.1f) priority=%d\n",
                     t.debug_name.c_str(), t.owner_region_id.c_str(), t.center.x, t.center.y,
                     t.center.z, t.half_extents.x, t.half_extents.y, t.half_extents.z, t.priority);
    else
        std::fprintf(stderr,
                     "[territory] registered '%s' owner='%s' OBB center=(%.1f,%.1f,%.1f) "
                     "half=(%.1f,%.1f,%.1f) quat=(%.3f,%.3f,%.3f,%.3f) priority=%d\n",
                     t.debug_name.c_str(), t.owner_region_id.c_str(), t.center.x, t.center.y,
                     t.center.z, t.half_extents.x, t.half_extents.y, t.half_extents.z, q.w, q.x,
                     q.y, q.z, t.priority);
}

int territoryCount()
{
    return static_cast<int>(sTerritories.size());
}

const Territory& territoryAt(int idx)
{
    return sTerritories[static_cast<std::size_t>(idx)];
}

const std::string& regionIdAtPosition(const glm::vec3& world_pos)
{
    static const std::string kEmpty;
    const Territory* winner = nullptr;
    float winner_volume = 0.0f;
    for (const auto& t : sTerritories)
    {
        if (!containsPoint(t, world_pos))
            continue;
        if (winner == nullptr)
        {
            winner = &t;
            winner_volume = volumeOf(t);
            continue;
        }
        // Higher priority wins; ties broken by smallest volume.
        if (t.priority > winner->priority)
        {
            winner = &t;
            winner_volume = volumeOf(t);
            continue;
        }
        if (t.priority == winner->priority)
        {
            const float v = volumeOf(t);
            if (v < winner_volume)
            {
                winner = &t;
                winner_volume = v;
            }
        }
    }
    return (winner != nullptr) ? winner->owner_region_id : kEmpty;
}

} // namespace engine::world
