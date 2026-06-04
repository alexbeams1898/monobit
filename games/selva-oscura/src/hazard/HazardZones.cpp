#include "hazard/HazardZones.h"

#include <cmath>

namespace selva::hazard
{

namespace
{

std::vector<HazardZone>& zones()
{
    static std::vector<HazardZone> v;
    return v;
}

} // namespace

void registerZone(const HazardZone& zone)
{
    zones().push_back(zone);
}

void clearAllZones()
{
    zones().clear();
}

const std::vector<HazardZone>& allZones()
{
    return zones();
}

bool positionIsInAvoidedZone(const glm::vec3& pos,
                             const std::unordered_set<std::string>& avoided_kinds)
{
    if (avoided_kinds.empty())
        return false;
    for (const auto& z : zones())
    {
        if (avoided_kinds.find(z.kind) == avoided_kinds.end())
            continue;
        if (std::abs(pos.x - z.center.x) > z.half_extents.x)
            continue;
        if (std::abs(pos.y - z.center.y) > z.half_extents.y)
            continue;
        if (std::abs(pos.z - z.center.z) > z.half_extents.z)
            continue;
        return true;
    }
    return false;
}

bool positionIsInAvoidedZone(const glm::vec3& pos, const std::vector<std::string>& avoided_kinds)
{
    if (avoided_kinds.empty())
        return false;
    std::unordered_set<std::string> as_set(avoided_kinds.begin(), avoided_kinds.end());
    return positionIsInAvoidedZone(pos, as_set);
}

} // namespace selva::hazard
