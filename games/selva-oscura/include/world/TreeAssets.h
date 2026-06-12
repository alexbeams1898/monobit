#pragma once

#include <glm/mat4x4.hpp>
#include <glm/vec3.hpp>

#include <cstdint>
#include <string>
#include <vector>

namespace selva::world
{

// One drawable tree submesh: a primitive (trunk OR branches), its
// texture, its alpha-cutoff. A "tree" is typically two of these
// drawn together (a trunk + a branches submesh on top).
struct TreeMesh
{
    std::uint32_t vao = 0;
    std::uint32_t vbo = 0;
    std::uint32_t ebo = 0;
    int index_count = 0;
    int vertex_count = 0;
    std::uint32_t base_color_tex = 0;
    float alpha_cutoff = 0.0f;
    float height = 1.0f;
    float trunk_radius = 1.0f;
    // Post-shift model-space AABB (in the variant's own model frame,
    // after re-centering by the trunk's source centroid + base_y).
    // Surfaced by the F2 tree-preview panel.
    float aabb_min[3] = {0.0f, 0.0f, 0.0f};
    float aabb_max[3] = {0.0f, 0.0f, 0.0f};
    // Source GLTF node name this submesh was loaded from. Surfaced
    // by the F2 tree-preview panel.
    std::string source_node_name;
};

// A "variant": one trunk + one branches submesh, paired. Drawing one
// variant draws both submeshes at the same model matrix.
struct TreeVariant
{
    TreeMesh trunk;
    TreeMesh branches;
    // Canonical variant identifier (e.g. "pine_a", "pine_short",
    // "rock_00"). Surfaced by the F2 tree-preview panel.
    std::string variant_name;
};

bool initTreeAssets();
void shutdownTreeAssets();

// How many tree variants are loaded. Currently 4 (3x Trunk_01 +
// Branches_01 variants and 1x Trunk_02 + Branches_02).
int treeVariantCount();

// Access a loaded variant by index (0..treeVariantCount()-1).
const TreeVariant& treeVariant(int idx);

// Reverse lookup: variant name -> index. Returns -1 if no loaded
// variant matches `name`. Used by the prop spawn funnel to resolve
// a PropArchetype id ("pine_a") to a forced_variant_idx on the
// generated CylinderCollider.
int treeVariantIndexByName(const std::string& name);

} // namespace selva::world
