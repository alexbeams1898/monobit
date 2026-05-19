#include "world/TreeAssets.h"

#include "render/Texture.h"

#include <cgltf.h>
#include <glm/gtc/matrix_transform.hpp>
#include <nlohmann/json.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <string>
#include <unordered_map>
#include <vector>

#include <glad/glad.h>

namespace selva::world
{

namespace
{

constexpr const char* kAssetDir = "assets/world/trees/low_poly_forest_tree_pack/";

struct Vertex
{
    float position[3];
    float normal[3];
    float uv[2];
};
static_assert(sizeof(Vertex) == 32, "TreeMesh vertex layout drifted");

std::vector<TreeVariant> sVariants;

const cgltf_accessor* findAttribute(const cgltf_primitive* prim, cgltf_attribute_type type,
                                    int index = 0)
{
    for (cgltf_size i = 0; i < prim->attributes_count; ++i)
    {
        const cgltf_attribute& a = prim->attributes[i];
        if (a.type == type && a.index == index)
            return a.data;
    }
    return nullptr;
}

// Apply a 4x4 transform to a position (M * vec4(p, 1)).
void transformPos(const float m[16], const float p[3], float out[3])
{
    out[0] = m[0] * p[0] + m[4] * p[1] + m[8] * p[2] + m[12];
    out[1] = m[1] * p[0] + m[5] * p[1] + m[9] * p[2] + m[13];
    out[2] = m[2] * p[0] + m[6] * p[1] + m[10] * p[2] + m[14];
}

// Apply the inverse-transpose's rotational part to a normal vector
// (3x3 of M, then renormalize). For uniform-scale + rotation only,
// just rotate; for non-uniform scale we'd need a real inverse-
// transpose, but the Sketchfab root matrix here is uniform-scale-
// plus-axis-swap so this is fine.
void transformNormal(const float m[16], const float n[3], float out[3])
{
    out[0] = m[0] * n[0] + m[4] * n[1] + m[8] * n[2];
    out[1] = m[1] * n[0] + m[5] * n[1] + m[9] * n[2];
    out[2] = m[2] * n[0] + m[6] * n[1] + m[10] * n[2];
    const float len = std::sqrt(out[0] * out[0] + out[1] * out[1] + out[2] * out[2]);
    if (len > 1e-6f)
    {
        out[0] /= len;
        out[1] /= len;
        out[2] /= len;
    }
}

// CPU-side mesh data, pre-GL-upload. Phase 1 collects these; phase 2
// re-centers paired trunk/branches together, then uploads.
struct CpuMesh
{
    std::vector<Vertex> verts;
    std::vector<std::uint32_t> indices;
    float centroid_x = 0.0f;
    float centroid_z = 0.0f;
    float min_y = 0.0f;
    float max_y = 0.0f;
    bool is_branches = false;
    bool is_atlas = false;
    bool is_rock = false;
    std::string family; // "01" or "02" for hero; "atlas"; "rock"
    std::string node_name;
};

bool loadPrimitiveToCpuMesh(const cgltf_primitive* prim, const float node_world[16], CpuMesh& out)
{
    const cgltf_accessor* pos_acc = findAttribute(prim, cgltf_attribute_type_position);
    const cgltf_accessor* norm_acc = findAttribute(prim, cgltf_attribute_type_normal);
    const cgltf_accessor* uv_acc = findAttribute(prim, cgltf_attribute_type_texcoord, 0);
    if (pos_acc == nullptr || uv_acc == nullptr)
        return false;

    const cgltf_size n = pos_acc->count;
    out.verts.resize(n);
    float min_y = 1e30f;
    float max_y = -1e30f;
    float sum_x = 0.0f;
    float sum_z = 0.0f;
    for (cgltf_size i = 0; i < n; ++i)
    {
        float p[3] = {0, 0, 0};
        float nrm[3] = {0, 1, 0};
        float uv[2] = {0, 0};
        cgltf_accessor_read_float(pos_acc, i, p, 3);
        if (norm_acc)
            cgltf_accessor_read_float(norm_acc, i, nrm, 3);
        cgltf_accessor_read_float(uv_acc, i, uv, 2);

        // Bake the node's world transform: Z-up → Y-up axis swap +
        // Sketchfab root scale. After this the vertex sits at the
        // artist's authored world position in our coordinate system.
        float wp[3];
        float wn[3];
        transformPos(node_world, p, wp);
        transformNormal(node_world, nrm, wn);

        out.verts[i].position[0] = wp[0];
        out.verts[i].position[1] = wp[1];
        out.verts[i].position[2] = wp[2];
        out.verts[i].normal[0] = wn[0];
        out.verts[i].normal[1] = wn[1];
        out.verts[i].normal[2] = wn[2];
        out.verts[i].uv[0] = uv[0];
        out.verts[i].uv[1] = uv[1];
        min_y = std::min(min_y, wp[1]);
        max_y = std::max(max_y, wp[1]);
        sum_x += wp[0];
        sum_z += wp[2];
    }
    out.min_y = min_y;
    out.max_y = max_y;
    out.centroid_x = sum_x / static_cast<float>(n);
    out.centroid_z = sum_z / static_cast<float>(n);

    if (prim->indices != nullptr)
    {
        const cgltf_accessor* idx_acc = prim->indices;
        out.indices.resize(idx_acc->count);
        for (cgltf_size i = 0; i < idx_acc->count; ++i)
        {
            cgltf_uint v = 0;
            cgltf_accessor_read_uint(idx_acc, i, &v, 1);
            out.indices[i] = static_cast<std::uint32_t>(v);
        }
    }
    else
    {
        out.indices.resize(n);
        for (cgltf_size i = 0; i < n; ++i)
            out.indices[i] = static_cast<std::uint32_t>(i);
    }
    return true;
}

// Per-variant root depth loaded from config.json. Lookup by canonical
// variant name; missing entries default to 0 (mesh's lowest vertex at
// Y=0). Authored by the level/art layer, not detected algorithmically.
std::unordered_map<std::string, float> sRootDepths;

void loadRootDepthConfig()
{
    sRootDepths.clear();
    // Config lives one level up so it's pack-independent (we can
    // swap mesh packs without losing the per-variant tuning).
    const std::string path = "assets/world/trees/config.json";
    std::ifstream f(path);
    if (!f.good())
    {
        std::fprintf(stderr, "[trees] no config at %s — all root_depth default to 0\n",
                     path.c_str());
        return;
    }
    try
    {
        nlohmann::json doc;
        f >> doc;
        if (doc.contains("variants") && doc["variants"].is_object())
        {
            for (auto it = doc["variants"].begin(); it != doc["variants"].end(); ++it)
            {
                if (it.value().is_object() && it.value().contains("root_depth"))
                    sRootDepths.emplace(it.key(), it.value()["root_depth"].get<float>());
            }
        }
    }
    catch (const std::exception& e)
    {
        std::fprintf(stderr, "[trees] config parse error: %s\n", e.what());
    }
    std::fprintf(stderr, "[trees] loaded %zu root_depth entries\n", sRootDepths.size());
}

float lookupRootDepth(const std::string& variant_name)
{
    auto it = sRootDepths.find(variant_name);
    return it != sRootDepths.end() ? it->second : 0.0f;
}

// Phase 2: upload a re-centered CpuMesh into GL with the given (cx, cz)
// XZ shift and Y base offset.
void uploadMesh(const CpuMesh& cpu, float cx_shift, float cz_shift, float base_y, TreeMesh& out)
{
    std::vector<Vertex> verts = cpu.verts;
    float max_xz = 0.0f;
    for (auto& v : verts)
    {
        v.position[0] -= cx_shift;
        v.position[1] -= base_y;
        v.position[2] -= cz_shift;
        const float r = std::sqrt(v.position[0] * v.position[0] + v.position[2] * v.position[2]);
        if (r > max_xz)
            max_xz = r;
    }
    out.height = cpu.max_y - cpu.min_y;
    out.trunk_radius = max_xz;
    out.index_count = static_cast<int>(cpu.indices.size());

    GLuint vao = 0;
    GLuint vbo = 0;
    GLuint ebo = 0;
    glGenVertexArrays(1, &vao);
    glGenBuffers(1, &vbo);
    glGenBuffers(1, &ebo);
    glBindVertexArray(vao);
    glBindBuffer(GL_ARRAY_BUFFER, vbo);
    glBufferData(GL_ARRAY_BUFFER, static_cast<GLsizeiptr>(verts.size() * sizeof(Vertex)),
                 verts.data(), GL_STATIC_DRAW);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, ebo);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER,
                 static_cast<GLsizeiptr>(cpu.indices.size() * sizeof(std::uint32_t)),
                 cpu.indices.data(), GL_STATIC_DRAW);

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
    glBindVertexArray(0);

    out.vao = vao;
    out.vbo = vbo;
    out.ebo = ebo;
}

// Lookup table of "mesh-name-prefix → which variant + role (trunk vs
// branches)". The glTF nodes are named like Tree_Trunk_01_Tree_Trunk_01_0
// or Tree_Trunk_01.001_Tree_Trunk_01_0, so we match by prefix.
bool nodeMatches(const char* node_name, const char* prefix)
{
    if (node_name == nullptr)
        return false;
    return std::strncmp(node_name, prefix, std::strlen(prefix)) == 0;
}

void destroyMesh(TreeMesh& m)
{
    if (m.vao != 0)
        glDeleteVertexArrays(1, &m.vao);
    if (m.vbo != 0)
        glDeleteBuffers(1, &m.vbo);
    if (m.ebo != 0)
        glDeleteBuffers(1, &m.ebo);
    selva::render::destroyTexture(m.base_color_tex);
    m = {};
}

} // namespace

bool initTreeAssets()
{
    loadRootDepthConfig();

    const std::string gltf_path = std::string(kAssetDir) + "scene.gltf";

    cgltf_options options = {};
    cgltf_data* data = nullptr;
    cgltf_result res = cgltf_parse_file(&options, gltf_path.c_str(), &data);
    if (res != cgltf_result_success)
    {
        std::fprintf(stderr, "[trees] parse failed: %s (err %d)\n", gltf_path.c_str(), res);
        return false;
    }
    res = cgltf_load_buffers(&options, data, gltf_path.c_str());
    if (res != cgltf_result_success)
    {
        std::fprintf(stderr, "[trees] load_buffers failed (err %d)\n", res);
        cgltf_free(data);
        return false;
    }

    // Phase 1: collect CPU meshes for hero trunks/branches, atlas
    // trees (flat low-poly fillers), and rocks.
    std::vector<CpuMesh> trunks;
    std::vector<CpuMesh> branches;
    std::vector<CpuMesh> atlas;
    std::vector<CpuMesh> rocks;
    for (cgltf_size i = 0; i < data->nodes_count; ++i)
    {
        const cgltf_node& node = data->nodes[i];
        if (node.mesh == nullptr || node.name == nullptr)
            continue;
        const bool t01 = nodeMatches(node.name, "Tree_Trunk_01");
        const bool t02 = nodeMatches(node.name, "Tree_Trunk_02");
        const bool b01 = nodeMatches(node.name, "Tree_Branches_01");
        const bool b02 = nodeMatches(node.name, "Tree_Branches_02");
        const bool atl = nodeMatches(node.name, "Background_Tree_Atlas");
        const bool rock = nodeMatches(node.name, "Rocks");
        if (!t01 && !t02 && !b01 && !b02 && !atl && !rock)
            continue;
        if (node.mesh->primitives_count == 0)
            continue;
        CpuMesh cm;
        float node_world[16];
        cgltf_node_transform_world(&node, node_world);
        if (!loadPrimitiveToCpuMesh(&node.mesh->primitives[0], node_world, cm))
            continue;
        cm.is_branches = (b01 || b02);
        cm.is_atlas = atl;
        cm.is_rock = rock;
        cm.family = atl ? "atlas" : rock ? "rock" : ((t01 || b01) ? "01" : "02");
        cm.node_name = node.name;
        std::fprintf(stderr, "[trees] %s family=%s name=%s y=[%.2f..%.2f] centroid=(%.2f, %.2f)\n",
                     rock             ? "rock"
                     : atl            ? "atlas"
                     : cm.is_branches ? "branches"
                                      : "trunk",
                     cm.family.c_str(), node.name, cm.min_y, cm.max_y, cm.centroid_x,
                     cm.centroid_z);
        if (rock)
            rocks.push_back(std::move(cm));
        else if (atl)
            atlas.push_back(std::move(cm));
        else if (cm.is_branches)
            branches.push_back(std::move(cm));
        else
            trunks.push_back(std::move(cm));
    }
    cgltf_free(data);

    // Phase 2: pair each trunk with its nearest branches mesh (same
    // family, closest XZ). Re-center the pair together using the
    // trunk's XZ centroid + the trunk's min-Y as the base.
    sVariants.clear();
    for (const CpuMesh& trunk : trunks)
    {
        int best = -1;
        float best_d2 = 1e30f;
        for (int j = 0; j < static_cast<int>(branches.size()); ++j)
        {
            if (branches[j].family != trunk.family)
                continue;
            const float dx = branches[j].centroid_x - trunk.centroid_x;
            const float dz = branches[j].centroid_z - trunk.centroid_z;
            const float d2 = dx * dx + dz * dz;
            if (d2 < best_d2)
            {
                best_d2 = d2;
                best = j;
            }
        }
        if (best < 0)
            continue;
        const CpuMesh& canopy = branches[best];

        TreeVariant variant;
        const float cx = trunk.centroid_x;
        const float cz = trunk.centroid_z;
        // Canonical variant name. Family-01 trees are pine_a/b/c in
        // load order; family-02 is pine_short. Loader-order is stable
        // (cgltf node-walk order), so the names map to config.json
        // entries consistently across runs.
        static int s_hero_01_count = 0;
        std::string variant_name;
        if (trunk.family == "01")
        {
            static const char* kHeroNames[] = {"pine_a", "pine_b", "pine_c"};
            const int idx = std::min(s_hero_01_count++, 2);
            variant_name = kHeroNames[idx];
        }
        else
        {
            variant_name = "pine_short";
        }
        const float root_depth = lookupRootDepth(variant_name);
        const float base = trunk.min_y + root_depth;
        std::fprintf(stderr, "[trees] variant '%s' root_depth=%.2f\n", variant_name.c_str(),
                     root_depth);
        uploadMesh(trunk, cx, cz, base, variant.trunk);
        uploadMesh(canopy, cx, cz, base, variant.branches);
        variant.trunk.alpha_cutoff = 0.0f;
        variant.branches.alpha_cutoff = 0.5f;
        variant.trunk.base_color_tex = selva::render::loadTexture2D(
            std::string(kAssetDir) + "textures/Tree_Trunk_" + trunk.family + "_baseColor.png");
        variant.branches.base_color_tex = selva::render::loadTexture2D(
            std::string(kAssetDir) + "textures/Tree_Branches_" + trunk.family + "_baseColor.png");
        std::fprintf(stderr,
                     "[trees] [variant %zu] paired trunk family=%s height=%.2f "
                     "with branches (centroid distance=%.2f)\n",
                     sVariants.size(), trunk.family.c_str(), variant.trunk.height,
                     std::sqrt(best_d2));
        sVariants.push_back(std::move(variant));
    }

    // Rocks: solid 3D geometry (not alpha-blended), render via the
    // same tree shader pass with no alpha cutoff. Used as scattered
    // ground accents alongside the trees.
    for (int ri = 0; ri < static_cast<int>(rocks.size()); ++ri)
    {
        const CpuMesh& r = rocks[ri];
        TreeVariant variant;
        const float cx = r.centroid_x;
        const float cz = r.centroid_z;
        // Rocks usually sit ON the ground, not below. Skip root_depth.
        const float base = r.min_y;
        uploadMesh(r, cx, cz, base, variant.trunk);
        variant.trunk.alpha_cutoff = 0.0f;
        variant.trunk.base_color_tex =
            selva::render::loadTexture2D(std::string(kAssetDir) + "textures/Rocks_baseColor.png");
        std::fprintf(stderr, "[trees] [variant %zu] rock_%02d height=%.2f node=%s\n",
                     sVariants.size(), ri, variant.trunk.height, r.node_name.c_str());
        sVariants.push_back(std::move(variant));
    }

    // Atlas trees deferred: see atlas-rendering note above.
    (void)atlas;

    std::fprintf(stderr, "[trees] loaded %zu variants\n", sVariants.size());
    return !sVariants.empty();
}

void shutdownTreeAssets()
{
    for (auto& v : sVariants)
    {
        destroyMesh(v.trunk);
        destroyMesh(v.branches);
    }
    sVariants.clear();
}

int treeVariantCount()
{
    return static_cast<int>(sVariants.size());
}

const TreeVariant& treeVariant(int idx)
{
    return sVariants[idx];
}

} // namespace selva::world
