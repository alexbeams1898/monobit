#pragma once

#include <glm/vec3.hpp>

#include <cstdint>
#include <string>
#include <vector>

namespace selva::world
{

// Per-primitive role in the render+physics pipelines. Authored as a
// node-extras key in the .glb (`extras: { "usage": "visual" }`); the
// loader reads it via cgltf_node::extras. Default = Both, so any mesh
// authored without the extras key behaves the way it always has: both
// drawn and collidable. Use Visual / Collision when authoring a pair
// of meshes that share geometric intent but have different roles
// (e.g. stair-silhouette visual + smooth-ramp collision).
enum class StaticMeshUsage
{
    Both = 0,  // default: rendered AND used for physics
    Visual,    // rendered only — physics layer skips
    Collision, // physics only — renderer skips
};

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
    // Floor-mask flag: primitives whose XZ footprint should mask out
    // terrain (so terrain doesn't render under them). Set by the
    // loader based on naming convention (crypt_plinth_* nodes). When
    // true, the stencil pre-pass before terrain draws this primitive
    // into the stencil buffer; the terrain shader then rejects
    // fragments where stencil != 0. Pixel-perfect carve-out without
    // any duplicated polygon data.
    bool floor_mask = false;
    // Role in the render+physics pipelines (see StaticMeshUsage above).
    // Defaults to Both so authored meshes without the `usage` extras
    // key behave the way they always have.
    StaticMeshUsage usage = StaticMeshUsage::Both;

    // World-space CPU copies of the geometry (positions + indices)
    // kept around so the physics layer can register this primitive
    // as a static trimesh body. Without this, after upload to GL the
    // mesh data is gone and physics would need to read it back from
    // the .glb a second time.
    std::vector<glm::vec3> cpu_positions;
    std::vector<std::uint32_t> cpu_indices;
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

// Generic loader used by JsonScene: load any .glb into a fresh
// StaticMesh. Caller owns the returned StaticMesh; calls
// freeStaticMeshGLResources(mesh) at scene-deactivate time to release
// VAOs/VBOs. CPU data (cpu_positions/cpu_indices) is consumed by the
// physics layer at scene activation and can be freed thereafter, or
// kept around for re-registration if the scene loads/unloads
// repeatedly.
//
// `world_origin` is added to every vertex position during load so the
// CPU positions are world-space (matches the convention chapel uses).
// GL vertex buffer is also uploaded with world-space positions, so the
// renderer can draw at identity model matrix.
bool loadStaticMesh(const char* glb_path, const glm::vec3& world_origin, StaticMesh& out);

// Free GPU resources (VAO/VBO/EBO) for a mesh loaded via loadStaticMesh.
// Idempotent.
void freeStaticMeshGLResources(StaticMesh& mesh);

} // namespace selva::world
