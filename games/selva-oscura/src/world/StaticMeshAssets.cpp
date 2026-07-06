#include "world/StaticMeshAssets.h"

#include <cgltf.h>
#include <nlohmann/json.hpp>

#include <cstdio>
#include <cstring>
#include <vector>

#include <glad/glad.h>

namespace selva::world
{

namespace
{

// Parse a glTF node's `extras` JSON for `usage: "visual" | "collision"
// | "both"`. Missing / malformed / absent key → Both. The extras
// payload is a raw JSON string written by the upstream authoring
// pipeline (custom mesh properties promoted into glTF node extras at
// export time).
StaticMeshUsage parseUsageFromExtras(const char* extras_json)
{
    if (extras_json == nullptr || extras_json[0] == '\0')
        return StaticMeshUsage::Both;
    try
    {
        const auto doc = nlohmann::json::parse(extras_json);
        if (!doc.is_object() || !doc.contains("usage"))
            return StaticMeshUsage::Both;
        const auto& v = doc["usage"];
        if (!v.is_string())
            return StaticMeshUsage::Both;
        const std::string s = v.get<std::string>();
        if (s == "visual")
            return StaticMeshUsage::Visual;
        if (s == "collision")
            return StaticMeshUsage::Collision;
        return StaticMeshUsage::Both;
    }
    catch (const std::exception&)
    {
        return StaticMeshUsage::Both;
    }
}

// Vertex layout matches the RegionProgram (vec3 pos + float shade) so
// static meshes draw with the same shader path as ground geometry —
// atmosphere, shadow, half-Lambert lighting all consistent. Normals
// are computed at the fragment level via dFdx/dFdy on world position
// in the region fragment shader, so we don't store per-vertex normals.
struct Vertex
{
    float position[3];
    float shade;
};
static_assert(sizeof(Vertex) == 16, "StaticMesh vertex layout drifted");

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

void transformPos(const float m[16], const float p[3], float out[3])
{
    out[0] = m[0] * p[0] + m[4] * p[1] + m[8] * p[2] + m[12];
    out[1] = m[1] * p[0] + m[5] * p[1] + m[9] * p[2] + m[13];
    out[2] = m[2] * p[0] + m[6] * p[1] + m[10] * p[2] + m[14];
}

bool loadPrimitive(const cgltf_primitive* prim, const float node_world[16],
                   const float world_offset[3], const std::string& node_name,
                   const char* node_extras_json, StaticMeshPrimitive& out)
{
    out.usage = parseUsageFromExtras(node_extras_json);
    // Vertex layout is position-only + per-vertex shade=1.0; normal is
    // derived in the fragment shader via dFdx/dFdy on world position
    // (see RegionShaders), so we don't query the glTF normal accessor.
    const cgltf_accessor* pos_acc = findAttribute(prim, cgltf_attribute_type_position);
    if (pos_acc == nullptr)
        return false;
    const cgltf_size n = pos_acc->count;
    std::vector<Vertex> verts(n);
    float bb_min[3] = {1e30f, 1e30f, 1e30f};
    float bb_max[3] = {-1e30f, -1e30f, -1e30f};
    for (cgltf_size i = 0; i < n; ++i)
    {
        float p[3] = {0, 0, 0};
        cgltf_accessor_read_float(pos_acc, i, p, 3);

        float wp[3];
        transformPos(node_world, p, wp);
        wp[0] += world_offset[0];
        wp[1] += world_offset[1];
        wp[2] += world_offset[2];

        verts[i].position[0] = wp[0];
        verts[i].position[1] = wp[1];
        verts[i].position[2] = wp[2];
        verts[i].shade = 1.0f;
        for (int k = 0; k < 3; ++k)
        {
            if (wp[k] < bb_min[k])
                bb_min[k] = wp[k];
            if (wp[k] > bb_max[k])
                bb_max[k] = wp[k];
        }
    }

    std::vector<std::uint32_t> indices;
    if (prim->indices != nullptr)
    {
        indices.resize(prim->indices->count);
        for (cgltf_size i = 0; i < prim->indices->count; ++i)
        {
            cgltf_uint v = 0;
            cgltf_accessor_read_uint(prim->indices, i, &v, 1);
            indices[i] = static_cast<std::uint32_t>(v);
        }
    }
    else
    {
        indices.resize(n);
        for (cgltf_size i = 0; i < n; ++i)
            indices[i] = static_cast<std::uint32_t>(i);
    }

    if (prim->material != nullptr && prim->material->has_pbr_metallic_roughness)
    {
        const float* bcf = prim->material->pbr_metallic_roughness.base_color_factor;
        out.base_color[0] = bcf[0];
        out.base_color[1] = bcf[1];
        out.base_color[2] = bcf[2];
    }
    out.source_node_name = node_name;
    out.index_count = static_cast<int>(indices.size());
    out.vertex_count = static_cast<int>(verts.size());
    // Floor mask: any node whose name starts with "crypt_plinth" is
    // a chapel-floor slab. Used by the terrain stencil pass to carve
    // terrain rendering out of the chapel indoor perimeter.
    out.floor_mask = node_name.rfind("crypt_plinth", 0) == 0;
    // CPU copies for the physics layer to register as a static
    // trimesh body. Positions are already in world space here.
    out.cpu_positions.reserve(verts.size());
    for (const auto& v : verts)
        out.cpu_positions.emplace_back(v.position[0], v.position[1], v.position[2]);
    out.cpu_indices = indices;

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
                 static_cast<GLsizeiptr>(indices.size() * sizeof(std::uint32_t)), indices.data(),
                 GL_STATIC_DRAW);

    constexpr GLsizei stride = sizeof(Vertex);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, stride,
                          reinterpret_cast<void*>(offsetof(Vertex, position)));
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(1, 1, GL_FLOAT, GL_FALSE, stride,
                          reinterpret_cast<void*>(offsetof(Vertex, shade)));
    glEnableVertexAttribArray(1);
    glBindVertexArray(0);

    out.vao = vao;
    out.vbo = vbo;
    out.ebo = ebo;

    std::fprintf(stderr,
                 "[static-mesh-prim] node='%s' verts=%d tris=%d "
                 "bbox=[%.2f,%.2f,%.2f .. %.2f,%.2f,%.2f] "
                 "color=(%.2f,%.2f,%.2f) usage=%s\n",
                 node_name.c_str(), out.vertex_count, out.index_count / 3, bb_min[0], bb_min[1],
                 bb_min[2], bb_max[0], bb_max[1], bb_max[2], out.base_color[0], out.base_color[1],
                 out.base_color[2],
                 out.usage == StaticMeshUsage::Visual      ? "visual"
                 : out.usage == StaticMeshUsage::Collision ? "collision"
                                                           : "both");
    // Diagnostic: for the castle specifically, dump every vertex's
    // final world-space position so we can see EXACTLY what the GPU
    // will render (positions are baked here; renderer uses identity
    // model matrix). If the bbox looks right but the visual looks
    // wrong, this catches any per-vertex weirdness the aggregate
    // bbox line might hide.
    if (node_name.find("castle") != std::string::npos)
    {
        for (cgltf_size i = 0; i < n; ++i)
        {
            std::fprintf(stderr, "  [castle-vert %zu] world=(%.3f, %.3f, %.3f)\n", i,
                         verts[i].position[0], verts[i].position[1], verts[i].position[2]);
        }
    }
    return true;
}

bool loadGltf(const char* path, const float world_offset[3], StaticMesh& out)
{
    const cgltf_options options{};
    cgltf_data* data = nullptr;
    cgltf_result r = cgltf_parse_file(&options, path, &data);
    if (r != cgltf_result_success || data == nullptr)
    {
        std::fprintf(stderr, "[static-mesh] failed to parse %s (cgltf=%d)\n", path,
                     static_cast<int>(r));
        return false;
    }
    r = cgltf_load_buffers(&options, data, path);
    if (r != cgltf_result_success)
    {
        std::fprintf(stderr, "[static-mesh] failed to load buffers for %s (cgltf=%d)\n", path,
                     static_cast<int>(r));
        cgltf_free(data);
        return false;
    }

    for (cgltf_size ni = 0; ni < data->nodes_count; ++ni)
    {
        const cgltf_node& node = data->nodes[ni];
        if (node.mesh == nullptr)
            continue;
        float world[16];
        cgltf_node_transform_world(&node, world);
        const std::string node_name = node.name != nullptr ? node.name : "";
        const char* extras_json = node.extras.data;
        for (cgltf_size pi = 0; pi < node.mesh->primitives_count; ++pi)
        {
            StaticMeshPrimitive prim;
            if (loadPrimitive(&node.mesh->primitives[pi], world, world_offset, node_name,
                              extras_json, prim))
                out.primitives.push_back(std::move(prim));
        }
    }

    cgltf_free(data);
    return !out.primitives.empty();
}

} // namespace

bool loadStaticMesh(const char* glb_path, const glm::vec3& world_origin, StaticMesh& out)
{
    out.primitives.clear();
    out.asset_name = glb_path;
    const float offset[3] = {world_origin.x, world_origin.y, world_origin.z};
    // Diagnostic: log every incoming world_origin so we can catch any
    // mismatch between region.json values and what actually reaches
    // the loader. Especially useful when a config edit doesn't seem to
    // move a mesh at runtime.
    std::fprintf(stderr,
                 "[static-mesh-load] path='%s' world_origin=(%.3f, %.3f, %.3f)\n",
                 glb_path, world_origin.x, world_origin.y, world_origin.z);
    return loadGltf(glb_path, offset, out);
}

void freeStaticMeshGLResources(StaticMesh& mesh)
{
    for (auto& p : mesh.primitives)
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
    }
}

} // namespace selva::world
