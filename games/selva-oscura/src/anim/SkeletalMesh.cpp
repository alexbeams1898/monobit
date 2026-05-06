#include "anim/SkeletalMesh.h"

#include "anim/Skeleton.h"

// cgltf is single-header — define IMPLEMENTATION in exactly one .cpp.
#define CGLTF_IMPLEMENTATION
#include <algorithm>
#include <array>
#include <cgltf.h>
#include <cstdio>
#include <cstring>
#include <glm/glm.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <limits>
#include <ozz/animation/runtime/skeleton.h>
#include <unordered_map>
#include <vector>

namespace selva::anim
{

// ---------------------------------------------------------------------------
// Move/destroy plumbing for the GPU resource handles. SkeletalMesh owns
// VAO/VBO/EBO; the destructor frees them, move-assignment transfers them
// and zeroes the source so the moved-from mesh's destructor is a no-op.
// ---------------------------------------------------------------------------

// Move ctor / assignment: transfer ALL members. Forgetting non-handle
// members in either move op = silent data loss after the move; that
// cost a long debug session once already. When adding a new member,
// add it in BOTH places.
SkeletalMesh::SkeletalMesh(SkeletalMesh&& other) noexcept
    : vao(other.vao), vbo(other.vbo), ebo(other.ebo), index_count(other.index_count),
      asset_root_transform(other.asset_root_transform),
      inverse_bind_matrices(std::move(other.inverse_bind_matrices)),
      foot_offset_y(other.foot_offset_y)
{
    other.vao = 0;
    other.vbo = 0;
    other.ebo = 0;
    other.index_count = 0;
    other.asset_root_transform = glm::mat4(1.0f);
    other.foot_offset_y = 0.0f;
}

SkeletalMesh& SkeletalMesh::operator=(SkeletalMesh&& other) noexcept
{
    if (this != &other)
    {
        if (vao != 0)
            glDeleteVertexArrays(1, &vao);
        if (vbo != 0)
            glDeleteBuffers(1, &vbo);
        if (ebo != 0)
            glDeleteBuffers(1, &ebo);
        vao = other.vao;
        vbo = other.vbo;
        ebo = other.ebo;
        index_count = other.index_count;
        asset_root_transform = other.asset_root_transform;
        inverse_bind_matrices = std::move(other.inverse_bind_matrices);
        foot_offset_y = other.foot_offset_y;
        other.vao = 0;
        other.vbo = 0;
        other.ebo = 0;
        other.index_count = 0;
        other.asset_root_transform = glm::mat4(1.0f);
        other.foot_offset_y = 0.0f;
    }
    return *this;
}

SkeletalMesh::~SkeletalMesh()
{
    if (vao != 0)
        glDeleteVertexArrays(1, &vao);
    if (vbo != 0)
        glDeleteBuffers(1, &vbo);
    if (ebo != 0)
        glDeleteBuffers(1, &ebo);
}

// ---------------------------------------------------------------------------
// Load implementation
// ---------------------------------------------------------------------------

namespace
{

// Vertex layout matching SkeletalMesh.h's commented schema. Tightly packed,
// 64 bytes per vertex (3+3+2 floats = 32B + 4 ints (16B as i32) + 4 floats
// (16B) = 64B). The shader's vertex attribute pointers must match.
struct alignas(4) Vertex
{
    float position[3];
    float normal[3];
    float uv[2];
    int32_t bone_indices[4];
    float bone_weights[4];
};
static_assert(sizeof(Vertex) == 64, "SkeletalMesh vertex layout drifted");

// Find an attribute by type+set on a primitive. cgltf primitives carry a
// flat list of attributes (POSITION, NORMAL, TEXCOORD_0, JOINTS_0, WEIGHTS_0,
// etc.); we look up the one we need by enum + index. Returns nullptr if not
// present, which is a hard error for required attributes.
const cgltf_accessor* findAttribute(const cgltf_primitive* prim, cgltf_attribute_type type,
                                    int index = 0)
{
    for (cgltf_size i = 0; i < prim->attributes_count; ++i)
    {
        const cgltf_attribute& attr = prim->attributes[i];
        if (attr.type == type && attr.index == index)
            return attr.data;
    }
    return nullptr;
}

// Read a vec3 attribute into a contiguous float[3] per vertex. cgltf
// handles the underlying buffer view + accessor offsets; we just walk.
bool readVec3(const cgltf_accessor* acc, std::vector<std::array<float, 3>>& out)
{
    if (!acc || acc->type != cgltf_type_vec3 || acc->component_type != cgltf_component_type_r_32f)
        return false;
    out.resize(acc->count);
    for (cgltf_size i = 0; i < acc->count; ++i)
    {
        if (!cgltf_accessor_read_float(acc, i, out[i].data(), 3))
            return false;
    }
    return true;
}

bool readVec2(const cgltf_accessor* acc, std::vector<std::array<float, 2>>& out)
{
    if (!acc || acc->type != cgltf_type_vec2 || acc->component_type != cgltf_component_type_r_32f)
        return false;
    out.resize(acc->count);
    for (cgltf_size i = 0; i < acc->count; ++i)
    {
        if (!cgltf_accessor_read_float(acc, i, out[i].data(), 2))
            return false;
    }
    return true;
}

// JOINTS_0 attributes are typically u8 or u16 in glTF (smaller is fine
// since rigs rarely exceed 256 bones). cgltf_accessor_read_uint promotes
// to uint. We then cast to int32 for the GPU layout.
bool readJoints(const cgltf_accessor* acc, std::vector<std::array<uint32_t, 4>>& out)
{
    if (!acc || acc->type != cgltf_type_vec4)
        return false;
    out.resize(acc->count);
    for (cgltf_size i = 0; i < acc->count; ++i)
    {
        if (!cgltf_accessor_read_uint(acc, i, out[i].data(), 4))
            return false;
    }
    return true;
}

bool readWeights(const cgltf_accessor* acc, std::vector<std::array<float, 4>>& out)
{
    if (!acc || acc->type != cgltf_type_vec4)
        return false;
    out.resize(acc->count);
    for (cgltf_size i = 0; i < acc->count; ++i)
    {
        if (!cgltf_accessor_read_float(acc, i, out[i].data(), 4))
            return false;
    }
    return true;
}

// Triangle indices — handle both u16 and u32 by promoting everything to u32.
bool readIndices(const cgltf_accessor* acc, std::vector<uint32_t>& out)
{
    if (!acc)
        return false;
    out.resize(acc->count);
    for (cgltf_size i = 0; i < acc->count; ++i)
        out[i] = static_cast<uint32_t>(cgltf_accessor_read_index(acc, i));
    return true;
}

// Read all mat4 entries from an accessor into a vector. glTF stores
// matrices column-major (same as glm), 16 floats each.
bool readMat4Array(const cgltf_accessor* acc, std::vector<glm::mat4>& out)
{
    if (!acc || acc->type != cgltf_type_mat4 || acc->component_type != cgltf_component_type_r_32f)
        return false;
    out.resize(acc->count);
    for (cgltf_size i = 0; i < acc->count; ++i)
    {
        if (!cgltf_accessor_read_float(acc, i, glm::value_ptr(out[i]), 16))
            return false;
    }
    return true;
}

// Find the scene node that uses a given mesh. glTF doesn't store the
// reverse mapping, so we walk all nodes once. Returns nullptr if no node
// references the mesh (rare — would be a malformed file).
//
// We need this to compute the mesh's world-space transform: the Character
// root node typically applies a unit-conversion scale (e.g. cm→m for
// Mixamo) that we have to bake into the rest-pose vertices, otherwise
// they'll be in centimeters while the bone palette (which gltf2ozz
// already scene-graph-corrected) is in meters.
const cgltf_node* findNodeForMesh(const cgltf_data* data, const cgltf_mesh* mesh)
{
    for (cgltf_size i = 0; i < data->nodes_count; ++i)
    {
        if (data->nodes[i].mesh == mesh)
            return &data->nodes[i];
    }
    return nullptr;
}

// Build a glTF-joint → ozz-joint remap. glTF's `skin.joints` array gives
// node indices in glTF order; each node has a name. ozz's Skeleton has the
// same names but possibly in a different order (ozz reorders for cache
// efficiency). We match by name.
//
// Returns a vector of size skin->joints_count where remap[gltf_joint_idx]
// is the ozz joint index, or -1 if no match (which would be a load error).
std::vector<int32_t> buildJointRemap(const cgltf_skin* skin,
                                     const ozz::animation::Skeleton& ozz_skel)
{
    // Lookup table: ozz joint name → ozz joint index.
    std::unordered_map<std::string, int32_t> ozz_name_to_index;
    const auto names = ozz_skel.joint_names();
    for (int i = 0; i < ozz_skel.num_joints(); ++i)
        ozz_name_to_index.emplace(names[i] ? names[i] : "", i);

    std::vector<int32_t> remap(skin->joints_count, -1);
    for (cgltf_size i = 0; i < skin->joints_count; ++i)
    {
        const cgltf_node* node = skin->joints[i];
        const char* gltf_name = node && node->name ? node->name : "";
        auto it = ozz_name_to_index.find(gltf_name);
        if (it != ozz_name_to_index.end())
            remap[i] = it->second;
    }
    return remap;
}

} // namespace

SkeletalMesh loadSkeletalMesh(const std::string& path, const Skeleton& skeleton)
{
    SkeletalMesh out;

    if (!skeleton.isLoaded())
    {
        std::fprintf(stderr, "[SkeletalMesh] cannot load %s: skeleton is not loaded\n",
                     path.c_str());
        return out;
    }

    cgltf_options options{};
    cgltf_data* data = nullptr;
    cgltf_result res = cgltf_parse_file(&options, path.c_str(), &data);
    if (res != cgltf_result_success)
    {
        std::fprintf(stderr, "[SkeletalMesh] cgltf_parse_file failed on %s (code %d)\n",
                     path.c_str(), static_cast<int>(res));
        return out;
    }
    res = cgltf_load_buffers(&options, data, path.c_str());
    if (res != cgltf_result_success)
    {
        std::fprintf(stderr, "[SkeletalMesh] cgltf_load_buffers failed on %s (code %d)\n",
                     path.c_str(), static_cast<int>(res));
        cgltf_free(data);
        return out;
    }

    if (data->meshes_count == 0 || data->meshes[0].primitives_count == 0)
    {
        std::fprintf(stderr, "[SkeletalMesh] %s has no meshes/primitives\n", path.c_str());
        cgltf_free(data);
        return out;
    }
    const cgltf_primitive& prim = data->meshes[0].primitives[0];

    // The skin block — must exist for a skinned mesh. We pick the first
    // skin in the file, matching gltf2ozz's "all skins merged into one
    // skeleton" behaviour.
    if (data->skins_count == 0)
    {
        std::fprintf(stderr, "[SkeletalMesh] %s has no skin (not a skinned mesh)\n", path.c_str());
        cgltf_free(data);
        return out;
    }
    const cgltf_skin* skin = &data->skins[0];

    // Read each vertex attribute into a temporary parallel array.
    std::vector<std::array<float, 3>> positions;
    std::vector<std::array<float, 3>> normals;
    std::vector<std::array<float, 2>> uvs;
    std::vector<std::array<uint32_t, 4>> joints;
    std::vector<std::array<float, 4>> weights;
    std::vector<uint32_t> indices;

    if (!readVec3(findAttribute(&prim, cgltf_attribute_type_position), positions))
    {
        std::fprintf(stderr, "[SkeletalMesh] %s: missing/invalid POSITION\n", path.c_str());
        cgltf_free(data);
        return out;
    }
    if (!readVec3(findAttribute(&prim, cgltf_attribute_type_normal), normals))
    {
        std::fprintf(stderr, "[SkeletalMesh] %s: missing/invalid NORMAL\n", path.c_str());
        cgltf_free(data);
        return out;
    }
    // UVs are optional but Soldier.glb has them; if absent we'd fill zeros.
    readVec2(findAttribute(&prim, cgltf_attribute_type_texcoord, 0), uvs);
    if (!readJoints(findAttribute(&prim, cgltf_attribute_type_joints, 0), joints))
    {
        std::fprintf(stderr, "[SkeletalMesh] %s: missing/invalid JOINTS_0\n", path.c_str());
        cgltf_free(data);
        return out;
    }
    if (!readWeights(findAttribute(&prim, cgltf_attribute_type_weights, 0), weights))
    {
        std::fprintf(stderr, "[SkeletalMesh] %s: missing/invalid WEIGHTS_0\n", path.c_str());
        cgltf_free(data);
        return out;
    }
    if (!readIndices(prim.indices, indices))
    {
        std::fprintf(stderr, "[SkeletalMesh] %s: missing/invalid index buffer\n", path.c_str());
        cgltf_free(data);
        return out;
    }

    // Sanity: all attribute arrays must be parallel.
    const std::size_t n = positions.size();
    if (normals.size() != n || joints.size() != n || weights.size() != n ||
        (uvs.size() != n && !uvs.empty()))
    {
        std::fprintf(stderr, "[SkeletalMesh] %s: attribute arrays differ in length\n",
                     path.c_str());
        cgltf_free(data);
        return out;
    }

    // glTF→ozz joint remap. Without this, vertex bone indices would point
    // at the wrong rows of the bone palette uniformat draw time.
    const std::vector<int32_t> joint_remap = buildJointRemap(skin, *skeleton.ozz_skeleton);

    // Compute the asset root transform — the cumulative world-space transform
    // of the mesh node's ancestors (NOT the mesh node itself, since that is
    // just identity for static meshes; what we need is the chain that ends
    // *above* the mesh, e.g. Mixamo's "Character" node with its 0.01 cm→m
    // scale). We bake this into vertex positions/normals at load time AND
    // store it on the SkeletalMesh so the caller can pass the same transform
    // into LocalToModelJob's `root` parameter — keeping bone palette and
    // mesh in the same coordinate space.
    glm::mat4 asset_root(1.0f);
    if (const cgltf_node* mesh_node = findNodeForMesh(data, &data->meshes[0]))
    {
        if (mesh_node->parent)
        {
            cgltf_float m[16];
            cgltf_node_transform_world(mesh_node->parent, m);
            asset_root = glm::make_mat4(m);
        }
    }
    out.asset_root_transform = asset_root;
    const glm::mat3 normal_xform = glm::transpose(glm::inverse(glm::mat3(asset_root)));

    // Inverse bind matrices, remapped into ozz order and rebased into our
    // post-asset-root space.
    //
    // glTF stores one inv_bind per joint (in skin->joints order, asset's
    // native space). Each is the inverse of that joint's bind-pose world
    // transform. The skinning equation we use:
    //
    //     skin_matrix[i] = current_model[i] * inverse_bind[i]
    //
    // is correct when current_model and inverse_bind sit in the same space.
    // We baked vertex positions into post-root space; the sampler's
    // bone palette also lives in post-root space (LocalToModelJob.root =
    // asset_root). So inverse_bind needs the same lift:
    //
    //     post_root_inv_bind = native_inv_bind * inverse(asset_root)
    //
    // (post-multiply: applying inverse(asset_root) to a vertex first lands
    // it in native space; then native_inv_bind takes it to bind-local;
    // which is exactly what we want a "post-root inverse bind" to do.)
    out.inverse_bind_matrices.assign(skeleton.boneCount(), glm::mat4(1.0f));
    if (skin->inverse_bind_matrices != nullptr)
    {
        std::vector<glm::mat4> native_inv_binds;
        if (readMat4Array(skin->inverse_bind_matrices, native_inv_binds))
        {
            const glm::mat4 inv_asset_root = glm::inverse(asset_root);
            for (cgltf_size i = 0; i < skin->joints_count && i < native_inv_binds.size(); ++i)
            {
                const int32_t ozz_idx = i < joint_remap.size() ? joint_remap[i] : -1;
                if (ozz_idx >= 0 &&
                    ozz_idx < static_cast<int32_t>(out.inverse_bind_matrices.size()))
                {
                    out.inverse_bind_matrices[ozz_idx] = native_inv_binds[i] * inv_asset_root;
                }
            }
        }
        else
        {
            std::fprintf(stderr, "[SkeletalMesh] %s: inverse_bind_matrices accessor invalid\n",
                         path.c_str());
        }
    }

    // Build the interleaved vertex buffer. Apply the joint remap to every
    // vertex's bone indices. Re-normalize weights (sometimes glTF authors
    // ship slightly off-1.0 sums). Bake the asset root transform into
    // positions and normals so they live in the same space as the bone
    // palette (which the PoseSampler will produce in that same space when
    // given the matching root).
    std::vector<Vertex> verts(n);
    // Track the lowest Y across all baked vertices. Used downstream to
    // plant the character's feet on the floor regardless of where in the
    // bind pose the rig's origin sits.
    float min_y = std::numeric_limits<float>::max();
    for (std::size_t i = 0; i < n; ++i)
    {
        Vertex& v = verts[i];
        const glm::vec4 transformed_pos =
            asset_root * glm::vec4(positions[i][0], positions[i][1], positions[i][2], 1.0f);
        v.position[0] = transformed_pos.x;
        v.position[1] = transformed_pos.y;
        v.position[2] = transformed_pos.z;
        if (transformed_pos.y < min_y)
            min_y = transformed_pos.y;
        const glm::vec3 transformed_n =
            glm::normalize(normal_xform * glm::vec3(normals[i][0], normals[i][1], normals[i][2]));
        v.normal[0] = transformed_n.x;
        v.normal[1] = transformed_n.y;
        v.normal[2] = transformed_n.z;
        if (!uvs.empty())
        {
            v.uv[0] = uvs[i][0];
            v.uv[1] = uvs[i][1];
        }
        else
        {
            v.uv[0] = v.uv[1] = 0.0f;
        }

        // Remap bone indices and copy weights.
        float weight_sum = 0.0f;
        for (int j = 0; j < 4; ++j)
        {
            const uint32_t gltf_joint = joints[i][j];
            const int32_t ozz_joint =
                gltf_joint < joint_remap.size() ? joint_remap[gltf_joint] : -1;
            // Fall back to bone 0 (root) if the remap fails so a misnamed
            // bone doesn't crash the GPU. The vertex will look wrong but
            // won't take down the game.
            v.bone_indices[j] = ozz_joint >= 0 ? ozz_joint : 0;
            v.bone_weights[j] = weights[i][j];
            weight_sum += weights[i][j];
        }
        if (weight_sum > 0.0f && std::abs(weight_sum - 1.0f) > 1e-4f)
        {
            const float inv = 1.0f / weight_sum;
            for (int j = 0; j < 4; ++j)
                v.bone_weights[j] *= inv;
        }
    }

    cgltf_free(data); // we have everything we need; release glTF buffers

    // Upload to GPU.
    glGenVertexArrays(1, &out.vao);
    glGenBuffers(1, &out.vbo);
    glGenBuffers(1, &out.ebo);
    glBindVertexArray(out.vao);

    glBindBuffer(GL_ARRAY_BUFFER, out.vbo);
    glBufferData(GL_ARRAY_BUFFER, static_cast<GLsizeiptr>(verts.size() * sizeof(Vertex)),
                 verts.data(), GL_STATIC_DRAW);

    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, out.ebo);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER,
                 static_cast<GLsizeiptr>(indices.size() * sizeof(uint32_t)), indices.data(),
                 GL_STATIC_DRAW);

    constexpr GLsizei stride = sizeof(Vertex);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, stride,
                          reinterpret_cast<void*>(offsetof(Vertex, position)));
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, stride,
                          reinterpret_cast<void*>(offsetof(Vertex, normal)));
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, stride,
                          reinterpret_cast<void*>(offsetof(Vertex, uv)));
    glEnableVertexAttribArray(2);
    // Bone indices are integer attributes — use glVertexAttribIPointer
    // (with capital I), not glVertexAttribPointer with GL_INT. The latter
    // would normalize/cast to float, losing the index.
    glVertexAttribIPointer(3, 4, GL_INT, stride,
                           reinterpret_cast<void*>(offsetof(Vertex, bone_indices)));
    glEnableVertexAttribArray(3);
    glVertexAttribPointer(4, 4, GL_FLOAT, GL_FALSE, stride,
                          reinterpret_cast<void*>(offsetof(Vertex, bone_weights)));
    glEnableVertexAttribArray(4);

    glBindVertexArray(0);

    out.index_count = static_cast<GLsizei>(indices.size());
    out.foot_offset_y = (min_y < std::numeric_limits<float>::max()) ? min_y : 0.0f;
    return out;
}

} // namespace selva::anim
