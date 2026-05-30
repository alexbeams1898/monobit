#include "gameplay/AiBarriers.h"

#include <cmath>
#include <cstdio>

namespace selva::gameplay
{

namespace
{
// Append-only registry: stable pointers for the program lifetime
// (resident-all-scenes architecture). reserve() generously at first
// register to avoid reallocs invalidating returned pointers.
std::vector<AiBlockVolume> sVolumes;
bool sReserved = false;

bool aabbContainsPoint(const glm::vec3& center, const glm::vec3& half, const glm::vec3& p)
{
    return std::abs(p.x - center.x) <= half.x && std::abs(p.y - center.y) <= half.y &&
           std::abs(p.z - center.z) <= half.z;
}
} // namespace

void registerAiBlockVolume(const AiBlockVolume& v)
{
    if (!sReserved)
    {
        sVolumes.reserve(64); // generous; chapel + descent + future-circles
        sReserved = true;
    }
    sVolumes.push_back(v);
    std::fprintf(stderr,
                 "[ai-barrier] registered '%s' owner='%s' center=(%.1f,%.1f,%.1f) "
                 "half=(%.1f,%.1f,%.1f)\n",
                 v.debug_name.c_str(), v.owner_region_id.c_str(), v.center.x, v.center.y,
                 v.center.z, v.half_extents.x, v.half_extents.y, v.half_extents.z);
}

int aiBlockVolumeCount()
{
    return static_cast<int>(sVolumes.size());
}

const AiBlockVolume* findAiBlockingVolume(const glm::vec3& world_pos,
                                          const std::string& actor_region_id)
{
    for (const auto& v : sVolumes)
    {
        if (v.owner_region_id == actor_region_id)
            continue; // an actor never blocks itself out of its own region
        if (aabbContainsPoint(v.center, v.half_extents, world_pos))
            return &v;
    }
    return nullptr;
}

} // namespace selva::gameplay
