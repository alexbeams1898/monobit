#pragma once

#include <glm/glm.hpp>

#include <cstdint>
#include <string>
#include <vector>

#include <glad/glad.h>

namespace selva::anim
{

struct Skeleton; // forward — full def in anim/Skeleton.h

// Material describes the shader inputs needed to draw one
// MeshPrimitive. Owned by SkeletalMesh in a vector indexed by
// MeshPrimitive::material_index. Default-constructed Material =
// "white-tinted, no textures" -- equivalent to the engine's
// pre-textures behavior, so loading a .glb with no PBR material
// produces today's flat-shaded look.
//
// Adding a new texture slot: add a `GLuint xxx_tex` here, add the
// corresponding `sampler2D` to the FS, cache a uniform location in
// the renderer init, add binding+upload in the per-primitive draw
// path. ~4 mechanical edits, no callsite refactor.
struct Material
{
    // Albedo / base-color texture. 0 = no texture, sample uses
    // base_color_factor only. Must be uploaded as GL_SRGB8_ALPHA8
    // so hardware decodes sRGB-encoded bytes to linear at sample.
    GLuint base_color_tex = 0;

    // Per-material scalar tint, multiplied with the texture sample
    // (or used alone when base_color_tex == 0). Sourced from glTF
    // material.pbrMetallicRoughness.baseColorFactor at load.
    // Linear-space.
    glm::vec4 base_color_factor = glm::vec4(1.0f);

    // Render-state hints. Defaults are skin-suitable.
    bool double_sided = false;

    enum class AlphaMode
    {
        Opaque,
        Mask, // shader discards where sampled alpha < alpha_cutoff
        Blend
    };
    AlphaMode alpha_mode = AlphaMode::Opaque;
    float alpha_cutoff = 0.5f;

    // Semantic role, populated at load time by matching the glTF
    // material name against known submesh materials. Body / eye / hair
    // primitives ride the same mesh in the character bake; the
    // renderer needs to know WHICH one it's drawing to apply
    // per-role tinting (eye_tint overrides body color, hair uses
    // Colorize mode, etc.). None = the default body primitive.
    enum class Role
    {
        None = 0,
        Eyes,
    };
    Role role = Role::None;
};

// Per-primitive vertex + index buffers + morph data. A glTF mesh
// can ship multiple primitives (body + eyes + lips as one .glb with
// 3 primitives, each with its own material). One MeshPrimitive per
// glTF primitive.
//
// Morphs are per-primitive because glTF authors them per-primitive.
// Cross-primitive morph names are looked up at draw time -- if the
// caller's morph_weights map has "nose-hump-incr" and primitive 0
// has that morph but primitive 1 doesn't, primitive 1 just doesn't
// apply it.
struct MeshPrimitive
{
    GLuint vao = 0;
    GLuint vbo = 0;
    GLuint ebo = 0;
    GLsizei index_count = 0;
    int vertex_count = 0;

    // Index into the parent SkeletalMesh::materials vector.
    // -1 = use the default-constructed Material (white, no texture).
    int material_index = -1;

    // Morph delta SSBO: vec4 entries indexed by
    // [morph_idx * vertex_count + gl_VertexID]. .w padding for
    // std430 layout. 0 if morph_target_count == 0.
    GLuint morph_delta_ssbo = 0;
    int morph_target_count = 0;

    // Per-primitive morph names. Indexed parallel to morph_delta_ssbo
    // along the morph_idx axis. The shared SkeletalMesh::morph_names
    // is the union across all primitives (for UI / save-load); this
    // primitive's view is the subset it actually has data for.
    std::vector<std::string> morph_names;
};

// A skinned 3D mesh — geometry that deforms when its skeleton's
// bones move. Composed of one or more MeshPrimitive (typically 1 for
// the body, +1 for eyes if rigged as a separate primitive). Owns
// GPU buffers per primitive; shared bone palette + inverse-bind
// matrices live here at the mesh level.
//
// Texture ownership: Materials hold raw GLuint handles BORROWED
// from the mesh's `textures` vector. The mesh destructor deletes
// every texture in `textures`; Materials are pure value types and
// must not free the handles themselves. When multiple Materials
// or primitives reference the same image, they all hold the same
// GLuint -- no double-free because ownership is centralized here.
//
// Vertex layout in each primitive's VBO is interleaved:
//
//   struct Vertex {
//       vec3   position;    // location 0
//       vec3   normal;      // location 1
//       vec2   uv;          // location 2
//       ivec4  bone_indices;// location 3
//       vec4   bone_weights;// location 4
//   };
//
// Index buffer is unsigned int; cgltf gives us the source component
// type and we promote-on-load if it's smaller.
//
// Bones-per-vertex is hardcoded at 4. This is convention — works on
// every GPU since OpenGL 3.3 and is enough for almost all rigs
// (extra weights past the top-4 get re-normalised at load time).
struct SkeletalMesh
{
    // Per-primitive geometry + morph. Body is primitives[0]; eyes/hair
    // attachments would land at primitives[1..].
    std::vector<MeshPrimitive> primitives;

    // Per-material data, indexed by MeshPrimitive::material_index.
    // Empty vector = every primitive uses the default Material.
    std::vector<Material> materials;

    // GL texture handles owned by this mesh, deleted in the destructor.
    // Materials reference these via raw GLuint (borrowed, not owned).
    // Indexed by glTF image order; one entry per unique sampled image.
    std::vector<GLuint> textures;

    // Union of morph names across all primitives. Used by the UI
    // (which doesn't care which primitive owns a morph) and for
    // save/load. Per-primitive shader binding uses
    // MeshPrimitive::morph_names for its local indexing.
    std::vector<std::string> morph_names;

    // Cumulative transform of the mesh's ancestor nodes in the glTF
    // scene graph (e.g. a unit-conversion node applying a 0.01 cm→m
    // scale at the asset root).
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

    // Lowest vertex Y across all primitives in the baked rest pose.
    // Used by the renderer to plant the character's feet at world y=0
    // (draw with y_offset = -foot_offset_y).
    float foot_offset_y = 0.0f;

    SkeletalMesh() = default;
    SkeletalMesh(const SkeletalMesh&) = delete;
    SkeletalMesh& operator=(const SkeletalMesh&) = delete;
    SkeletalMesh(SkeletalMesh&&) noexcept;
    SkeletalMesh& operator=(SkeletalMesh&&) noexcept;
    ~SkeletalMesh();

    // True if at least one primitive is loaded + has triangles.
    bool isLoaded() const
    {
        for (const auto& p : primitives)
            if (p.vao != 0 && p.index_count > 0)
                return true;
        return false;
    }
};

// Load a skinned mesh from a glTF file (.glb or .gltf). Path is relative
// to the working directory. Reads the FIRST mesh's FIRST primitive — for
// humanoid.glb that's the single skinned body mesh. We can extend to
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
