#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace selva::world
{

// One drawable static-mesh primitive: VAO + VBO + EBO + a base color
// (read from the source glTF material's baseColorFactor — no texture).
// Static meshes are unanimated, untextured-for-now props: chapels,
// gates, walls, statues, etc. Distinct from TreeAssets which carries
// alpha-cutout foliage + wind metadata.
struct StaticMeshPrimitive
{
    std::uint32_t vao = 0;
    std::uint32_t vbo = 0;
    std::uint32_t ebo = 0;
    int index_count = 0;
    int vertex_count = 0;
    float base_color[3] = {0.8f, 0.8f, 0.8f};
    std::string source_node_name;
};

// A static-mesh asset: one or more primitives drawn at the same model
// matrix. The crypt is the first asset; future props use the same
// loader.
struct StaticMesh
{
    std::vector<StaticMeshPrimitive> primitives;
    std::string asset_name;
};

bool initStaticMeshAssets();
void shutdownStaticMeshAssets();

// The crypt mesh (entrance-to-Hell on the colle plateau). Loaded from
// assets/world/static_meshes/crypt.glb. Returns nullptr if init failed.
const StaticMesh* cryptMesh();

} // namespace selva::world
