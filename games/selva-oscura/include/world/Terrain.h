#pragma once

#include <glm/vec2.hpp>

#include <cstdint>
#include <string>
#include <vector>

namespace selva::world
{

// One region of terrain. v1 = single chunk per region; the chunking
// architecture will subdivide this when streaming ships.
struct TerrainRegion
{
    std::string name;
    std::uint32_t vao = 0;
    std::uint32_t vbo = 0;
    std::uint32_t ebo = 0;
    int index_count = 0;
    // Heightmap data kept on CPU for sampling queries.
    std::vector<float> heights; // height[y * width + x] in world Y meters
    int hm_width = 0;
    int hm_height = 0;
    glm::vec2 world_origin{0.0f, 0.0f};
    float world_extent = 0.0f;
    float height_min = 0.0f;
    float height_max = 0.0f;
    float base_color[3] = {0.16f, 0.13f, 0.10f};
    // Mesh-vertex Y values, mirroring the GPU mesh. Used by sampleHeight
    // so the gameplay ground always matches the rendered surface.
    std::vector<float> mesh_y; // mesh_y[iz * verts_per_side + ix]
    int subdivide = 0;
};

bool initTerrain();
void shutdownTerrain();

int terrainRegionCount();
const TerrainRegion& terrainRegion(int idx);

// Sample world Y at the given world (x, z). Bilinear interpolation
// across the heightmap. Returns 0 if (x, z) falls outside any
// region. v1 assumes one region; expand to nearest-region or
// region-by-coordinate lookup when more ship.
float sampleHeight(float world_x, float world_z);

// Player spawn position in world coordinates, loaded from the
// terrain config (player_spawn { x, z }). Y is intentionally NOT
// part of the config - callers should query sampleHeight(x, z) to
// place the player on the ground, so the spawn always tracks the
// current heightmap. Returns (0, 0) if the config is missing the
// player_spawn block.
glm::vec2 playerSpawnXZ();

} // namespace selva::world
