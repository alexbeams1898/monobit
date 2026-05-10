#include "anim/SkeletalMesh.h"

#include "anim/Skeleton.h"

// cgltf is single-header — define IMPLEMENTATION in exactly one .cpp.
#define CGLTF_IMPLEMENTATION
#include <cgltf.h>
#include <glm/glm.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <ozz/animation/runtime/skeleton.h>

#include <algorithm>
#include <array>
#include <cstdio>
#include <cstring>
#include <limits>
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

struct MeshAttributes
{
    std::vector<std::array<float, 3>> positions;
    std::vector<std::array<float, 3>> normals;
    std::vector<std::array<float, 2>> uvs;
    std::vector<std::array<uint32_t, 4>> joints;
    std::vector<std::array<float, 4>> weights;
    std::vector<uint32_t> indices;
};

// Read POSITION, NORMAL, UV, JOINTS_0, WEIGHTS_0, indices. Returns
// true on success. UVs are optional. Logs and returns false otherwise.
static bool readMeshAttributes(const cgltf_primitive& prim, const std::string& path,
                               MeshAttributes& attrs)
{
    if (!readVec3(findAttribute(&prim, cgltf_attribute_type_position), attrs.positions))
    {
        std::fprintf(stderr, "[SkeletalMesh] %s: missing/invalid POSITION\n", path.c_str());
        return false;
    }
    if (!readVec3(findAttribute(&prim, cgltf_attribute_type_normal), attrs.normals))
    {
        std::fprintf(stderr, "[SkeletalMesh] %s: missing/invalid NORMAL\n", path.c_str());
        return false;
    }
    readVec2(findAttribute(&prim, cgltf_attribute_type_texcoord, 0), attrs.uvs);
    if (!readJoints(findAttribute(&prim, cgltf_attribute_type_joints, 0), attrs.joints))
    {
        std::fprintf(stderr, "[SkeletalMesh] %s: missing/invalid JOINTS_0\n", path.c_str());
        return false;
    }
    if (!readWeights(findAttribute(&prim, cgltf_attribute_type_weights, 0), attrs.weights))
    {
        std::fprintf(stderr, "[SkeletalMesh] %s: missing/invalid WEIGHTS_0\n", path.c_str());
        return false;
    }
    if (!readIndices(prim.indices, attrs.indices))
    {
        std::fprintf(stderr, "[SkeletalMesh] %s: missing/invalid index buffer\n", path.c_str());
        return false;
    }
    const std::size_t n = attrs.positions.size();
    if (attrs.normals.size() != n || attrs.joints.size() != n || attrs.weights.size() != n ||
        (attrs.uvs.size() != n && !attrs.uvs.empty()))
    {
        std::fprintf(stderr, "[SkeletalMesh] %s: attribute arrays differ in length\n",
                     path.c_str());
        return false;
    }
    return true;
}

// Compute the asset root transform from the mesh node's parent chain.
// Mixamo's "Character" node has a 0.01 cm→m scale baked in; we apply
// it at load time. Identity if there's no parent.
static glm::mat4 computeAssetRoot(const cgltf_data* data)
{
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
    return asset_root;
}

// Build inverse bind matrices indexed by ozz joint, with the asset
// root post-multiplied so they sit in the same post-root space as
// the bone palette + baked vertex positions.
static bool buildInverseBindMatrices(const cgltf_skin* skin, const Skeleton& skeleton,
                                     const std::vector<int32_t>& joint_remap,
                                     const glm::mat4& asset_root, const std::string& path,
                                     std::vector<glm::mat4>& out)
{
    out.assign(skeleton.boneCount(), glm::mat4(1.0f));
    if (skin->inverse_bind_matrices == nullptr)
    {
        std::fprintf(stderr,
                     "[SkeletalMesh] %s: skin has no inverseBindMatrices accessor — "
                     "regenerate the .glb (FBX2glTF always emits them)\n",
                     path.c_str());
        return false;
    }
    std::vector<glm::mat4> native_inv_binds;
    if (!readMat4Array(skin->inverse_bind_matrices, native_inv_binds))
    {
        std::fprintf(stderr, "[SkeletalMesh] %s: inverse_bind_matrices accessor invalid\n",
                     path.c_str());
        return false;
    }
    const glm::mat4 inv_asset_root = glm::inverse(asset_root);
    for (cgltf_size i = 0; i < skin->joints_count && i < native_inv_binds.size(); ++i)
    {
        const int32_t ozz_idx = i < joint_remap.size() ? joint_remap[i] : -1;
        if (ozz_idx >= 0 && ozz_idx < static_cast<int32_t>(out.size()))
            out[ozz_idx] = native_inv_binds[i] * inv_asset_root;
    }
    return true;
}

// Bake one vertex: apply asset_root to position + normal_xform to
// normal, copy UVs (or zero), remap joint indices, normalize weights.
static void bakeVertex(Vertex& v, std::size_t i, const MeshAttributes& attrs,
                       const glm::mat4& asset_root, const glm::mat3& normal_xform,
                       const std::vector<int32_t>& joint_remap)
{
    const glm::vec4 transformed_pos =
        asset_root *
        glm::vec4(attrs.positions[i][0], attrs.positions[i][1], attrs.positions[i][2], 1.0f);
    v.position[0] = transformed_pos.x;
    v.position[1] = transformed_pos.y;
    v.position[2] = transformed_pos.z;
    const glm::vec3 transformed_n = glm::normalize(
        normal_xform * glm::vec3(attrs.normals[i][0], attrs.normals[i][1], attrs.normals[i][2]));
    v.normal[0] = transformed_n.x;
    v.normal[1] = transformed_n.y;
    v.normal[2] = transformed_n.z;
    if (!attrs.uvs.empty())
    {
        v.uv[0] = attrs.uvs[i][0];
        v.uv[1] = attrs.uvs[i][1];
    }
    else
    {
        v.uv[0] = v.uv[1] = 0.0f;
    }
    float weight_sum = 0.0f;
    for (int j = 0; j < 4; ++j)
    {
        const uint32_t gltf_joint = attrs.joints[i][j];
        const int32_t ozz_joint = gltf_joint < joint_remap.size() ? joint_remap[gltf_joint] : -1;
        v.bone_indices[j] = ozz_joint >= 0 ? ozz_joint : 0;
        v.bone_weights[j] = attrs.weights[i][j];
        weight_sum += attrs.weights[i][j];
    }
    if (weight_sum > 0.0f && std::abs(weight_sum - 1.0f) > 1e-4f)
    {
        const float inv = 1.0f / weight_sum;
        for (auto& w : v.bone_weights)
            w *= inv;
    }
}

// Upload an interleaved Vertex buffer + index buffer to GL, set up
// vertex attribute pointers. Stores VAO/VBO/EBO + index_count on `out`.
static void uploadMeshToGpu(SkeletalMesh& out, const std::vector<Vertex>& verts,
                            const std::vector<uint32_t>& indices)
{
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
    glVertexAttribIPointer(3, 4, GL_INT, stride,
                           reinterpret_cast<void*>(offsetof(Vertex, bone_indices)));
    glEnableVertexAttribArray(3);
    glVertexAttribPointer(4, 4, GL_FLOAT, GL_FALSE, stride,
                          reinterpret_cast<void*>(offsetof(Vertex, bone_weights)));
    glEnableVertexAttribArray(4);

    glBindVertexArray(0);
    out.index_count = static_cast<GLsizei>(indices.size());
}

SkeletalMesh loadSkeletalMesh(const std::string& path, const Skeleton& skeleton)
{
    SkeletalMesh out;

    if (!skeleton.isLoaded())
    {
        std::fprintf(stderr, "[SkeletalMesh] cannot load %s: skeleton is not loaded\n",
                     path.c_str());
        return out;
    }

    const cgltf_options options{};
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
    if (data->skins_count == 0)
    {
        std::fprintf(stderr, "[SkeletalMesh] %s has no skin (not a skinned mesh)\n", path.c_str());
        cgltf_free(data);
        return out;
    }
    const cgltf_primitive& prim = data->meshes[0].primitives[0];
    const cgltf_skin* skin = &data->skins[0];

    MeshAttributes attrs;
    if (!readMeshAttributes(prim, path, attrs))
    {
        cgltf_free(data);
        return out;
    }
    const std::size_t n = attrs.positions.size();
    const std::vector<int32_t> joint_remap = buildJointRemap(skin, *skeleton.ozz_skeleton);

    const glm::mat4 asset_root = computeAssetRoot(data);
    out.asset_root_transform = asset_root;
    const glm::mat3 normal_xform = glm::transpose(glm::inverse(glm::mat3(asset_root)));

    if (!buildInverseBindMatrices(skin, skeleton, joint_remap, asset_root, path,
                                  out.inverse_bind_matrices))
    {
        cgltf_free(data);
        return out;
    }

    // Bake vertex buffer. Track lowest Y for foot-plant offset.
    std::vector<Vertex> verts(n);
    float min_y = std::numeric_limits<float>::max();
    for (std::size_t i = 0; i < n; ++i)
    {
        bakeVertex(verts[i], i, attrs, asset_root, normal_xform, joint_remap);
        min_y = std::min(min_y, verts[i].position[1]);
    }

    cgltf_free(data); // we have everything we need; release glTF buffers

    uploadMeshToGpu(out, verts, attrs.indices);
    out.foot_offset_y = (min_y < std::numeric_limits<float>::max()) ? min_y : 0.0f;
    return out;
}

} // namespace selva::anim
