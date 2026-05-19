#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include <glm/mat4x4.hpp>
#include <glm/vec3.hpp>

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
    std::uint32_t base_color_tex = 0;
    float alpha_cutoff = 0.0f;
    float height = 1.0f;
    float trunk_radius = 1.0f;
};

// A "variant": one trunk + one branches submesh, paired. Drawing one
// variant draws both submeshes at the same model matrix.
struct TreeVariant
{
    TreeMesh trunk;
    TreeMesh branches;
};

bool initTreeAssets();
void shutdownTreeAssets();

// How many tree variants are loaded. Currently 4 (3x Trunk_01 +
// Branches_01 variants and 1x Trunk_02 + Branches_02).
int treeVariantCount();

// Access a loaded variant by index (0..treeVariantCount()-1).
const TreeVariant& treeVariant(int idx);

} // namespace selva::world
