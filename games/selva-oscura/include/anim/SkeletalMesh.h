#pragma once

#include <cstdint>
#include <glad/glad.h>
#include <glm/glm.hpp>
#include <string>
#include <vector>

namespace selva::anim
{

struct Skeleton; // forward — full def in anim/Skeleton.h

// A skinned 3D mesh — geometry that deforms when its skeleton's bones move.
// Owns GPU buffers (VAO/VBO/EBO). One SkeletalMesh per character mesh part
// (single-primitive characters like X Bot ship one mesh; multi-mesh rigs
// would need one SkeletalMesh per primitive).
//
// Vertex layout in the VBO is interleaved:
//
//   struct Vertex {
//       vec3   position;    // location 0
//       vec3   normal;      // location 1
//       vec2   uv;          // location 2  (texture coords; unused today)
//       ivec4  bone_indices;// location 3  (which bones influence this vertex)
//       vec4   bone_weights;// location 4  (how much, must sum to ~1.0)
//   };
//
// Index buffer is unsigned int; cgltf gives us the source component type
// and we promote-on-load if it's smaller.
//
// Bones-per-vertex is hardcoded at 4. This is convention — works on every
// GPU since OpenGL 3.3 and is enough for almost all rigs (extra weights
// past the top-4 get re-normalised at load time).
struct SkeletalMesh
{
    GLuint vao = 0;
    GLuint vbo = 0;
    GLuint ebo = 0;
    GLsizei index_count = 0;

    // Cumulative transform of the mesh's ancestor nodes in the glTF scene
    // graph (e.g. Mixamo's "Character" node applies a 0.01 cm→m scale).
    // We bake this into vertex positions and normals at load time, AND
    // the caller must feed the same transform into the bone palette
    // computation (PoseSampler / LocalToModelJob's `root` parameter) so
    // both sides of the skinning math stay in the same space. Identity
    // for assets whose mesh node sits directly under the scene root.
    glm::mat4 asset_root_transform = glm::mat4(1.0f);

    // Inverse bind matrices, one per bone, **in ozz joint order**. Each
    // matrix is the inverse of the bone's bind-pose (rest pose) world
    // transform. The skinning equation requires multiplying current
    // bone-world-transform by inverse_bind to produce a "delta from rest"
    // matrix that vertices can be transformed by:
    //
    //     skin_matrix[i] = current_bone[i] * inverse_bind[i]
    //
    // PoseSampler does this multiplication per-frame to produce the
    // bone palette uploaded to the shader.
    //
    // Matrices live in the same space as our baked vertex positions
    // (post-asset_root_transform). cgltf provides the raw inverse binds
    // in the asset's native space; we apply the asset_root_transform on
    // both sides during load to bring them into the same baked space.
    std::vector<glm::mat4> inverse_bind_matrices;

    // Lowest vertex Y in the baked rest pose. Different rigs author the
    // mesh origin differently — Mixamo puts it near toe-level (a few cm
    // below the feet); other rigs put it at hips, on the floor, or
    // anywhere else. To plant a character's feet at world y=0, draw
    // with y_offset = -foot_offset_y. We measure this once at load
    // time so any character mesh works without per-asset tuning.
    float foot_offset_y = 0.0f;

    SkeletalMesh() = default;
    SkeletalMesh(const SkeletalMesh&) = delete;
    SkeletalMesh& operator=(const SkeletalMesh&) = delete;
    SkeletalMesh(SkeletalMesh&&) noexcept;
    SkeletalMesh& operator=(SkeletalMesh&&) noexcept;
    ~SkeletalMesh();

    // True if GPU buffers were created and we have triangles to draw.
    bool isLoaded() const
    {
        return vao != 0 && index_count > 0;
    }
};

// Load a skinned mesh from a glTF file (.glb or .gltf). Path is relative
// to the working directory. Reads the FIRST mesh's FIRST primitive — for
// X_Bot.glb that's the single skinned body mesh. We can extend to
// multi-primitive / multi-mesh loading when we need armor swaps.
//
// The skeleton must already be loaded — we use its bone names to remap
// glTF's per-vertex joint indices into ozz's internal bone ordering.
// Without this remapping, vertex `bone_indices` in the GPU buffer would
// point at the wrong rows of the bone palette at draw time.
//
// Returns an unloaded mesh on failure and logs to stderr.
SkeletalMesh loadSkeletalMesh(const std::string& path, const Skeleton& skeleton);

} // namespace selva::anim
