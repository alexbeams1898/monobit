#include "anim/SkeletalMesh.h"

#include "anim/Skeleton.h"

// cgltf is single-header — define IMPLEMENTATION in exactly one .cpp.
#define CGLTF_IMPLEMENTATION
#include <cgltf.h>
#include <glm/glm.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <ozz/animation/runtime/skeleton.h>
#include <stb_image.h>

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

namespace
{
// Free GL resources owned by one primitive. Sets the handles to 0
// after release so a subsequent destroy is a no-op (safe to call
// after the owning vector is moved-from).
void releasePrimitiveGpu(MeshPrimitive& p) noexcept
{
    if (p.vao != 0)
    {
        glDeleteVertexArrays(1, &p.vao);
        p.vao = 0;
    }
    if (p.vbo != 0)
    {
        glDeleteBuffers(1, &p.vbo);
        p.vbo = 0;
    }
    if (p.ebo != 0)
    {
        glDeleteBuffers(1, &p.ebo);
        p.ebo = 0;
    }
    if (p.morph_delta_ssbo != 0)
    {
        glDeleteBuffers(1, &p.morph_delta_ssbo);
        p.morph_delta_ssbo = 0;
    }
}
} // namespace

// Move ctor / assignment: transfer primitives + materials + textures
// + everything else by std::move. The vector move steals ownership of
// contained GPU handles automatically -- no per-handle reset needed
// on the source vector because the moved-from vector is empty.
SkeletalMesh::SkeletalMesh(SkeletalMesh&& other) noexcept
    : primitives(std::move(other.primitives)), materials(std::move(other.materials)),
      textures(std::move(other.textures)), morph_names(std::move(other.morph_names)),
      asset_root_transform(other.asset_root_transform),
      inverse_bind_matrices(std::move(other.inverse_bind_matrices)),
      foot_offset_y(other.foot_offset_y)
{
    other.asset_root_transform = glm::mat4(1.0f);
    other.foot_offset_y = 0.0f;
}

SkeletalMesh& SkeletalMesh::operator=(SkeletalMesh&& other) noexcept
{
    if (this != &other)
    {
        for (auto& p : primitives)
            releasePrimitiveGpu(p);
        for (GLuint t : textures)
            if (t != 0)
                glDeleteTextures(1, &t);
        primitives = std::move(other.primitives);
        materials = std::move(other.materials);
        textures = std::move(other.textures);
        morph_names = std::move(other.morph_names);
        asset_root_transform = other.asset_root_transform;
        inverse_bind_matrices = std::move(other.inverse_bind_matrices);
        foot_offset_y = other.foot_offset_y;
        other.asset_root_transform = glm::mat4(1.0f);
        other.foot_offset_y = 0.0f;
    }
    return *this;
}

SkeletalMesh::~SkeletalMesh()
{
    for (auto& p : primitives)
        releasePrimitiveGpu(p);
    for (GLuint t : textures)
        if (t != 0)
            glDeleteTextures(1, &t);
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

// Read all glTF morph targets (mesh.primitives[].targets[] +
// mesh.target_names[]) into a flat per-(target, vertex) vec3 buffer +
// a names vector. Returns the number of targets read; 0 means the
// mesh has no morph targets, which is the normal/expected case for
// every actor that hasn't been authored with shape keys.
//
// Layout of out_deltas: [target_idx * vertex_count + vertex_idx] ->
// vec3 position delta in asset-local space. asset_root_transform is
// applied to each delta so the morph and the base mesh live in the
// same baked space (otherwise extreme morphs would visibly diverge
// from where the bones expect the verts to be).
//
// Normal/tangent deltas are deliberately ignored in v1 -- shader
// re-lights from base normals; mild visual drift at extreme weights
// only. Re-adding them is a one-line attribute lookup if/when it
// matters.
//
// names[i] comes from mesh.target_names (a sibling string array
// glTF stores at mesh scope). Falls back to "morph_<i>" if the
// exporter didn't write names (which Blender always does for shape
// keys, so this fallback is paranoia).
int readMorphTargets(const cgltf_primitive* prim, const cgltf_mesh* mesh, std::size_t vertex_count,
                     const glm::mat3& asset_root_basis, std::vector<glm::vec3>& out_deltas,
                     std::vector<std::string>& out_names)
{
    out_deltas.clear();
    out_names.clear();
    const std::size_t target_count = prim->targets_count;
    if (target_count == 0)
        return 0;
    out_deltas.assign(target_count * vertex_count, glm::vec3(0.0f));
    out_names.reserve(target_count);
    for (std::size_t t = 0; t < target_count; ++t)
    {
        const cgltf_morph_target& mt = prim->targets[t];
        const cgltf_accessor* acc = nullptr;
        for (cgltf_size a = 0; a < mt.attributes_count; ++a)
        {
            if (mt.attributes[a].type == cgltf_attribute_type_position)
            {
                acc = mt.attributes[a].data;
                break;
            }
        }
        if (acc == nullptr || acc->count != vertex_count)
            continue; // skip this target; loop below leaves deltas at 0
        // cgltf_accessor_unpack_floats handles BOTH sparse and dense
        // accessors -- it writes the fully-resolved per-element data
        // into a flat float buffer, filling sparse-implicit zeros for
        // us. cgltf_accessor_read_float (the per-element API) only
        // works on dense accessors; sparse morph targets (which is
        // how Blender writes them by default to save space) come back
        // as all zeros through that path.
        std::vector<float> raw_xyz(vertex_count * 3);
        const cgltf_size unpacked =
            cgltf_accessor_unpack_floats(acc, raw_xyz.data(), raw_xyz.size());
        if (unpacked != raw_xyz.size())
        {
            std::fprintf(stderr,
                         "[SkeletalMesh] morph target[%zu]: unpack_floats wrote %zu floats "
                         "(expected %zu) -- skipping\n",
                         t, unpacked, raw_xyz.size());
            continue;
        }
        for (std::size_t v = 0; v < vertex_count; ++v)
        {
            // Asset root scale/orient applied so delta lives in the
            // same baked space as the vertex positions we wrote in
            // bakeVertex(). Translation component of asset_root is
            // NOT applied (deltas are displacements, not points).
            const glm::vec3 raw(raw_xyz[v * 3 + 0], raw_xyz[v * 3 + 1], raw_xyz[v * 3 + 2]);
            out_deltas[t * vertex_count + v] = asset_root_basis * raw;
        }
        const char* nm = (t < mesh->target_names_count) ? mesh->target_names[t] : nullptr;
        out_names.emplace_back(nm ? nm : ("morph_" + std::to_string(t)));
    }
    return static_cast<int>(out_names.size());
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

// Find the region node that uses a given mesh. glTF doesn't store the
// reverse mapping, so we walk all nodes once. Returns nullptr if no node
// references the mesh (rare — would be a malformed file).
//
// We need this to compute the mesh's world-space transform: the root
// node typically applies a unit-conversion scale (e.g. cm→m on some
// FBX exports) that we have to bake into the rest-pose vertices,
// otherwise they'll be in centimeters while the bone palette (which
// gltf2ozz already region-graph-corrected) is in meters.
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
// Some exports apply a 0.01 cm→m scale on a top-level container node;
// we bake it in at load time. Identity if there's no parent.
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
// vertex attribute pointers. Stores VAO/VBO/EBO + index_count on the
// target primitive.
static void uploadPrimitiveGpu(MeshPrimitive& prim, const std::vector<Vertex>& verts,
                               const std::vector<uint32_t>& indices)
{
    glGenVertexArrays(1, &prim.vao);
    glGenBuffers(1, &prim.vbo);
    glGenBuffers(1, &prim.ebo);
    glBindVertexArray(prim.vao);

    glBindBuffer(GL_ARRAY_BUFFER, prim.vbo);
    glBufferData(GL_ARRAY_BUFFER, static_cast<GLsizeiptr>(verts.size() * sizeof(Vertex)),
                 verts.data(), GL_STATIC_DRAW);

    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, prim.ebo);
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
    prim.index_count = static_cast<GLsizei>(indices.size());
    prim.vertex_count = static_cast<int>(verts.size());
}

// Load a glTF image into a GL texture and return the handle. Handles
// the two glTF image-encoding paths:
//   - Embedded (.glb): bytes live in a buffer_view; read via
//     cgltf_buffer_view_data().
//   - External (.gltf + URI): URI points at a sibling .png/.jpg on
//     disk; load via stb_image's file path. Not exercised today but
//     supported for forward-compat.
//
// Returns 0 on any error (logs to stderr). Texture is uploaded as
// GL_SRGB8_ALPHA8 (per the sRGB pipeline doctrine -- diffuse art is
// authored sRGB; shader expects linear; GPU decodes on sample).
static GLuint loadGltfImageToGl(const cgltf_image* image, const std::string& gltf_path)
{
    if (image == nullptr)
        return 0;

    unsigned char* pixels = nullptr;
    int w = 0;
    int h = 0;
    int channels = 0;
    // glTF stores textures with top-left origin matching UV space;
    // OpenGL's default is bottom-left. Don't flip -- gen_humanoid +
    // every other glTF tool authors against the spec, so a flip
    // would invert the texture and break sampling.
    stbi_set_flip_vertically_on_load(0);

    if (image->buffer_view != nullptr)
    {
        // Embedded: bytes are within the .glb's binary chunk.
        const uint8_t* data = cgltf_buffer_view_data(image->buffer_view);
        const cgltf_size size = image->buffer_view->size;
        pixels = stbi_load_from_memory(data, static_cast<int>(size), &w, &h, &channels, 4);
    }
    else if (image->uri != nullptr)
    {
        // External: resolve URI relative to the .gltf file's dir.
        std::string full_path = image->uri;
        const auto slash = gltf_path.find_last_of("/\\");
        if (slash != std::string::npos)
            full_path = gltf_path.substr(0, slash + 1) + image->uri;
        pixels = stbi_load(full_path.c_str(), &w, &h, &channels, 4);
    }

    if (pixels == nullptr)
    {
        std::fprintf(stderr, "[SkeletalMesh] glTF image '%s' failed to decode (stbi: %s)\n",
                     image->name ? image->name : "<unnamed>", stbi_failure_reason());
        return 0;
    }

    GLuint tex = 0;
    glGenTextures(1, &tex);
    glBindTexture(GL_TEXTURE_2D, tex);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_SRGB8_ALPHA8, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, pixels);
    glGenerateMipmap(GL_TEXTURE_2D);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    constexpr GLenum kTextureMaxAnisotropyExt = 0x84FE;
    constexpr GLenum kMaxTextureMaxAnisotropyExt = 0x84FF;
    GLfloat max_aniso = 1.0f;
    glGetFloatv(kMaxTextureMaxAnisotropyExt, &max_aniso);
    glTexParameterf(GL_TEXTURE_2D, kTextureMaxAnisotropyExt, std::min(16.0f, max_aniso));
    glBindTexture(GL_TEXTURE_2D, 0);

    stbi_image_free(pixels);
    return tex;
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

    if (data->meshes_count == 0)
    {
        std::fprintf(stderr, "[SkeletalMesh] %s has no meshes\n", path.c_str());
        cgltf_free(data);
        return out;
    }
    if (data->skins_count == 0)
    {
        std::fprintf(stderr, "[SkeletalMesh] %s has no skin (not a skinned mesh)\n", path.c_str());
        cgltf_free(data);
        return out;
    }
    const cgltf_skin* skin = &data->skins[0];

    const glm::mat4 asset_root = computeAssetRoot(data);
    out.asset_root_transform = asset_root;
    const glm::mat3 normal_xform = glm::transpose(glm::inverse(glm::mat3(asset_root)));
    const glm::mat3 asset_root_basis = glm::mat3(asset_root);
    const std::vector<int32_t> joint_remap = buildJointRemap(skin, *skeleton.ozz_skeleton);

    if (!buildInverseBindMatrices(skin, skeleton, joint_remap, asset_root, path,
                                  out.inverse_bind_matrices))
    {
        cgltf_free(data);
        return out;
    }

    // Union-of-morph-names accumulator. Every primitive contributes
    // its own morph names; the union is used by callers (UI, save/
    // load) that don't care which primitive owns which morph. The
    // per-primitive view stays on MeshPrimitive::morph_names.
    std::vector<std::string> union_morph_names;

    // Track lowest Y across ALL primitives for foot-plant offset.
    float min_y = std::numeric_limits<float>::max();

    // Walk EVERY mesh and EVERY primitive. Blender's glTF exporter
    // produces one glTF mesh per Blender object; the eye asset is a
    // separate Blender object so it becomes meshes[1]. Both share the
    // scene's single skin (skins[0]), which is why joint_remap +
    // inverse_bind_matrices are computed once above.
    for (cgltf_size mi = 0; mi < data->meshes_count; ++mi)
    {
        const cgltf_mesh& mesh_gltf = data->meshes[mi];
        for (cgltf_size pi = 0; pi < mesh_gltf.primitives_count; ++pi)
        {
            const cgltf_primitive& prim = mesh_gltf.primitives[pi];
            MeshAttributes attrs;
            if (!readMeshAttributes(prim, path, attrs))
                continue; // logged inside readMeshAttributes
            const std::size_t n = attrs.positions.size();

            // Bake this primitive's vertex buffer.
            std::vector<Vertex> verts(n);
            for (std::size_t i = 0; i < n; ++i)
            {
                bakeVertex(verts[i], i, attrs, asset_root, normal_xform, joint_remap);
                min_y = std::min(min_y, verts[i].position[1]);
            }

            // Morph targets: per-primitive (glTF authors them per-
            // primitive). Body primitive carries the player-creator
            // face morphs; eye primitive typically has none.
            std::vector<glm::vec3> morph_deltas;
            std::vector<std::string> morph_names;
            const int target_count =
                readMorphTargets(&prim, &mesh_gltf, n, asset_root_basis, morph_deltas, morph_names);
            if (target_count > 0)
            {
                std::fprintf(stderr,
                             "[SkeletalMesh] %s: prim %zu.%zu loaded %d morph target(s) "
                             "over %zu verts\n",
                             path.c_str(), mi, pi, target_count, n);
            }

            // Read glTF material into our engine Material.
            Material mat;
            if (prim.material != nullptr && prim.material->has_pbr_metallic_roughness)
            {
                const auto& pbr = prim.material->pbr_metallic_roughness;
                const float* bcf = pbr.base_color_factor;
                mat.base_color_factor = glm::vec4(bcf[0], bcf[1], bcf[2], bcf[3]);
                if (pbr.base_color_texture.texture != nullptr &&
                    pbr.base_color_texture.texture->image != nullptr)
                {
                    const GLuint tex =
                        loadGltfImageToGl(pbr.base_color_texture.texture->image, path);
                    if (tex != 0)
                    {
                        out.textures.push_back(tex);
                        mat.base_color_tex = tex;
                    }
                }
            }
            if (prim.material != nullptr)
            {
                mat.double_sided = (prim.material->double_sided != 0);
                switch (prim.material->alpha_mode)
                {
                case cgltf_alpha_mode_mask:
                    mat.alpha_mode = Material::AlphaMode::Mask;
                    break;
                case cgltf_alpha_mode_blend:
                    mat.alpha_mode = Material::AlphaMode::Blend;
                    break;
                case cgltf_alpha_mode_opaque:
                default:
                    mat.alpha_mode = Material::AlphaMode::Opaque;
                    break;
                }
                mat.alpha_cutoff = prim.material->alpha_cutoff;
                // Semantic role from material name. The character bake
                // pipeline names the eye submesh's material
                // "HumanoidEyes" so the renderer can tint it
                // independently from the body. Future roles (Teeth,
                // Tongue) follow the same pattern.
                if (prim.material->name != nullptr)
                {
                    const std::string mname(prim.material->name);
                    if (mname == "HumanoidEyes")
                        mat.role = Material::Role::Eyes;
                }
            }
            const int material_index = static_cast<int>(out.materials.size());
            out.materials.push_back(mat);

            // Upload this primitive's GPU buffers.
            out.primitives.emplace_back();
            MeshPrimitive& out_prim = out.primitives.back();
            uploadPrimitiveGpu(out_prim, verts, attrs.indices);
            out_prim.material_index = material_index;
            out_prim.morph_target_count = target_count;
            out_prim.morph_names = morph_names;
            for (const auto& nm : morph_names)
            {
                if (std::find(union_morph_names.begin(), union_morph_names.end(), nm) ==
                    union_morph_names.end())
                {
                    union_morph_names.push_back(nm);
                }
            }
            if (target_count > 0)
            {
                std::vector<glm::vec4> padded(morph_deltas.size());
                for (std::size_t i = 0; i < morph_deltas.size(); ++i)
                    padded[i] = glm::vec4(morph_deltas[i], 0.0f);
                glGenBuffers(1, &out_prim.morph_delta_ssbo);
                glBindBuffer(GL_SHADER_STORAGE_BUFFER, out_prim.morph_delta_ssbo);
                glBufferData(GL_SHADER_STORAGE_BUFFER,
                             static_cast<GLsizeiptr>(padded.size() * sizeof(glm::vec4)),
                             padded.data(), GL_STATIC_DRAW);
                glBindBuffer(GL_SHADER_STORAGE_BUFFER, 0);
            }
        }
    }

    cgltf_free(data);

    out.morph_names = std::move(union_morph_names);
    out.foot_offset_y = (min_y < std::numeric_limits<float>::max()) ? min_y : 0.0f;
    std::fprintf(stderr,
                 "[SkeletalMesh] %s: loaded %zu primitive(s), %zu material(s), %zu morph(s)\n",
                 path.c_str(), out.primitives.size(), out.materials.size(), out.morph_names.size());
    return out;
}

} // namespace selva::anim
