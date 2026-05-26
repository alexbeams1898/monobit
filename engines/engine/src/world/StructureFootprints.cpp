#include "world/StructureFootprints.h"

#include "world/TerrainModifiers.h"

#include <vector>

namespace engine::world
{

namespace
{
std::vector<StructureFootprint> sFootprints;
}

void registerStructureFootprint(const StructureFootprint& f)
{
    sFootprints.push_back(f);

    // Wire physics-side: a Hole modifier at the same rect drops
    // terrain trimesh quads inside.
    TerrainModifier hole;
    hole.center_xz = f.center_xz;
    hole.half_extents_xz = f.half_extents_xz;
    hole.mode = TerrainModifier::Mode::Hole;
    hole.debug_name = f.debug_name;
    registerTerrainModifier(hole);
}

void clearStructureFootprints()
{
    sFootprints.clear();
}

int structureFootprintCount()
{
    return static_cast<int>(sFootprints.size());
}

const StructureFootprint& structureFootprintAt(int idx)
{
    return sFootprints[idx];
}

} // namespace engine::world
