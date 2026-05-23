#include "world/StaticMeshAssets.h"

#include <cgltf.h>
#include <glad/glad.h>

#include <cstdio>
#include <vector>

namespace selva::world
{

namespace
{

// Vertex layout matches the SceneProgram (vec3 pos + float shade) so
// static meshes draw with the same shader path as ground geometry —
// atmosphere, shadow, half-Lambert lighting all consistent. Normals
// are computed at the fragment level via dFdx/dFdy on world position
// in the scene fragment shader, so we don't store per-vertex normals.
struct Vertex
{
    float position[3];
    float shade;
};
static_assert(sizeof(Vertex) == 16, "StaticMesh vertex layout drifted");

StaticMesh sCrypt;
bool sInitialized = false;

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
                   const std::string& node_name, StaticMeshPrimitive& out)
{
    // Vertex layout is position-only + per-vertex shade=1.0; normal is
    // derived in the fragment shader via dFdx/dFdy on world position
    // (see SceneShaders), so we don't query the glTF normal accessor.
    const cgltf_accessor* pos_acc = findAttribute(prim, cgltf_attribute_type_position);
    if (pos_acc == nullptr)
        return false;
    const cgltf_size n = pos_acc->count;
    std::vector<Vertex> verts(n);
    for (cgltf_size i = 0; i < n; ++i)
    {
        float p[3] = {0, 0, 0};
        cgltf_accessor_read_float(pos_acc, i, p, 3);

        float wp[3];
        transformPos(node_world, p, wp);

        verts[i].position[0] = wp[0];
        verts[i].position[1] = wp[1];
        verts[i].position[2] = wp[2];
        verts[i].shade = 1.0f;
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
                 static_cast<GLsizeiptr>(indices.size() * sizeof(std::uint32_t)),
                 indices.data(), GL_STATIC_DRAW);

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
    return true;
}

bool loadGltf(const char* path, StaticMesh& out)
{
    cgltf_options options{};
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
        for (cgltf_size pi = 0; pi < node.mesh->primitives_count; ++pi)
        {
            StaticMeshPrimitive prim;
            if (loadPrimitive(&node.mesh->primitives[pi], world, node_name, prim))
                out.primitives.push_back(std::move(prim));
        }
    }

    cgltf_free(data);
    return !out.primitives.empty();
}

} // namespace

bool initStaticMeshAssets()
{
    if (sInitialized)
        return true;
    sCrypt.asset_name = "crypt";
    const char* path = "assets/world/static_meshes/crypt.glb";
    if (!loadGltf(path, sCrypt))
    {
        std::fprintf(stderr, "[static-mesh] crypt load failed; static meshes disabled\n");
        return false;
    }
    std::fprintf(stderr, "[static-mesh] crypt loaded: %zu primitives\n",
                 sCrypt.primitives.size());
    sInitialized = true;
    return true;
}

void shutdownStaticMeshAssets()
{
    for (auto& p : sCrypt.primitives)
    {
        if (p.vao != 0)
            glDeleteVertexArrays(1, &p.vao);
        if (p.vbo != 0)
            glDeleteBuffers(1, &p.vbo);
        if (p.ebo != 0)
            glDeleteBuffers(1, &p.ebo);
    }
    sCrypt.primitives.clear();
    sInitialized = false;
}

const StaticMesh* cryptMesh()
{
    if (!sInitialized || sCrypt.primitives.empty())
        return nullptr;
    return &sCrypt;
}

} // namespace selva::world
