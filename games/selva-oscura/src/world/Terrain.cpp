#include "world/Terrain.h"

#include <nlohmann/json.hpp>
#include <stb_image.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <vector>

#include <glad/glad.h>

namespace selva::world
{

namespace
{

constexpr const char* kConfigPath = "assets/world/terrain/config.json";

struct Vertex
{
    float position[3];
    float normal[3];
    float uv[2];
};
static_assert(sizeof(Vertex) == 32, "Terrain vertex layout drifted");

std::vector<TerrainRegion> sRegions;

// Bilinear sample of `heights` (world Y meters) at (u, v) in [0..1].
float bilinearSample(const std::vector<float>& heights, int w, int h, float u, float v)
{
    if (heights.empty() || w <= 0 || h <= 0)
        return 0.0f;
    const float fx = u * static_cast<float>(w - 1);
    const float fy = v * static_cast<float>(h - 1);
    const int ix = std::clamp(static_cast<int>(std::floor(fx)), 0, w - 2);
    const int iy = std::clamp(static_cast<int>(std::floor(fy)), 0, h - 2);
    const float tx = fx - static_cast<float>(ix);
    const float ty = fy - static_cast<float>(iy);
    const float h00 = heights[iy * w + ix];
    const float h10 = heights[iy * w + (ix + 1)];
    const float h01 = heights[(iy + 1) * w + ix];
    const float h11 = heights[(iy + 1) * w + (ix + 1)];
    const float hx0 = h00 + (h10 - h00) * tx;
    const float hx1 = h01 + (h11 - h01) * tx;
    return hx0 + (hx1 - hx0) * ty;
}

bool loadHeightmapPng(const std::string& path, const TerrainRegion& meta,
                      std::vector<float>& out_heights, int& out_w, int& out_h)
{
    int w = 0;
    int h = 0;
    int channels = 0;
    stbi_set_flip_vertically_on_load(0);
    unsigned char* pixels = stbi_load(path.c_str(), &w, &h, &channels, 1);
    if (pixels == nullptr)
    {
        std::fprintf(stderr, "[terrain] heightmap load failed: %s (%s)\n", path.c_str(),
                     stbi_failure_reason());
        return false;
    }
    out_w = w;
    out_h = h;
    out_heights.resize(static_cast<std::size_t>(w) * static_cast<std::size_t>(h));
    const float range = meta.height_max - meta.height_min;
    for (int i = 0; i < w * h; ++i)
    {
        const float t = static_cast<float>(pixels[i]) / 255.0f;
        out_heights[i] = meta.height_min + t * range;
    }
    stbi_image_free(pixels);
    return true;
}

void buildRegionMesh(TerrainRegion& r, int subdivide)
{
    const int verts_per_side = subdivide + 1;
    const float half = r.world_extent * 0.5f;
    const float step = r.world_extent / static_cast<float>(subdivide);
    r.subdivide = subdivide;
    r.mesh_y.assign(
        static_cast<std::size_t>(verts_per_side) * static_cast<std::size_t>(verts_per_side), 0.0f);

    std::vector<Vertex> verts(static_cast<std::size_t>(verts_per_side) *
                              static_cast<std::size_t>(verts_per_side));

    // Sample heightmap per vertex (UV = [0..1] across the region).
    for (int iz = 0; iz < verts_per_side; ++iz)
    {
        for (int ix = 0; ix < verts_per_side; ++ix)
        {
            const float u = static_cast<float>(ix) / static_cast<float>(subdivide);
            const float v = static_cast<float>(iz) / static_cast<float>(subdivide);
            const float wx = r.world_origin.x + (u * 2.0f - 1.0f) * half;
            const float wz = r.world_origin.y + (v * 2.0f - 1.0f) * half;
            const float wy = bilinearSample(r.heights, r.hm_width, r.hm_height, u, v);
            const int vi = iz * verts_per_side + ix;
            verts[vi].position[0] = wx;
            verts[vi].position[1] = wy;
            verts[vi].position[2] = wz;
            verts[vi].uv[0] = u;
            verts[vi].uv[1] = v;
            // Normal computed in second pass below (needs neighbors).
            verts[vi].normal[0] = 0.0f;
            verts[vi].normal[1] = 1.0f;
            verts[vi].normal[2] = 0.0f;
            r.mesh_y[vi] = wy;
        }
    }

    // Per-vertex normals from central differences in the heightfield.
    // Reads neighbor heights via the same bilinear sampler (handles
    // border vertices by clamping the index).
    for (int iz = 0; iz < verts_per_side; ++iz)
    {
        for (int ix = 0; ix < verts_per_side; ++ix)
        {
            const float u = static_cast<float>(ix) / static_cast<float>(subdivide);
            const float v = static_cast<float>(iz) / static_cast<float>(subdivide);
            const float du = 1.0f / static_cast<float>(subdivide);
            const float hL =
                bilinearSample(r.heights, r.hm_width, r.hm_height, std::max(0.0f, u - du), v);
            const float hR =
                bilinearSample(r.heights, r.hm_width, r.hm_height, std::min(1.0f, u + du), v);
            const float hD =
                bilinearSample(r.heights, r.hm_width, r.hm_height, u, std::max(0.0f, v - du));
            const float hU =
                bilinearSample(r.heights, r.hm_width, r.hm_height, u, std::min(1.0f, v + du));
            // Tangent vectors along X and Z; cross gives the normal.
            const float dx = 2.0f * step;
            const float dz = 2.0f * step;
            const float nx = (hL - hR);
            const float nz = (hD - hU);
            const float ny = dx; // = dz; uniform spacing makes this work
            const float len = std::sqrt(nx * nx + ny * ny + nz * nz);
            const int vi = iz * verts_per_side + ix;
            if (len > 1e-6f)
            {
                verts[vi].normal[0] = nx / len;
                verts[vi].normal[1] = ny / len;
                verts[vi].normal[2] = nz / len;
            }
            (void)dz;
        }
    }

    // Indices.
    std::vector<std::uint32_t> indices;
    indices.reserve(static_cast<std::size_t>(subdivide) * static_cast<std::size_t>(subdivide) * 6u);
    for (int iz = 0; iz < subdivide; ++iz)
    {
        for (int ix = 0; ix < subdivide; ++ix)
        {
            const std::uint32_t i0 = static_cast<std::uint32_t>(iz * verts_per_side + ix);
            const std::uint32_t i1 = i0 + 1;
            const std::uint32_t i2 = i0 + static_cast<std::uint32_t>(verts_per_side);
            const std::uint32_t i3 = i2 + 1;
            indices.push_back(i0);
            indices.push_back(i3);
            indices.push_back(i1);
            indices.push_back(i0);
            indices.push_back(i2);
            indices.push_back(i3);
        }
    }
    r.index_count = static_cast<int>(indices.size());

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
    glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, stride,
                          reinterpret_cast<void*>(offsetof(Vertex, normal)));
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, stride,
                          reinterpret_cast<void*>(offsetof(Vertex, uv)));
    glEnableVertexAttribArray(2);
    glBindVertexArray(0);

    r.vao = vao;
    r.vbo = vbo;
    r.ebo = ebo;
}

} // namespace

bool initTerrain()
{
    sRegions.clear();
    std::ifstream f(kConfigPath);
    if (!f.good())
    {
        std::fprintf(stderr, "[terrain] config not found at %s\n", kConfigPath);
        return false;
    }
    nlohmann::json doc;
    try
    {
        f >> doc;
    }
    catch (const std::exception& e)
    {
        std::fprintf(stderr, "[terrain] config parse error: %s\n", e.what());
        return false;
    }
    if (!doc.contains("regions") || !doc["regions"].is_object())
    {
        std::fprintf(stderr, "[terrain] config missing 'regions' object\n");
        return false;
    }

    for (auto it = doc["regions"].begin(); it != doc["regions"].end(); ++it)
    {
        const auto& cfg = it.value();
        TerrainRegion r;
        r.name = it.key();
        const std::string hm_path = cfg.value("heightmap", std::string());
        const auto origin = cfg.value("world_origin", std::vector<float>{0.0f, 0.0f});
        if (origin.size() >= 2)
            r.world_origin = glm::vec2(origin[0], origin[1]);
        r.world_extent = cfg.value("world_extent", 256.0f);
        r.height_min = cfg.value("height_range_min", -2.0f);
        r.height_max = cfg.value("height_range_max", 15.0f);
        const int subdivide = cfg.value("subdivide", 128);
        if (cfg.contains("base_color") && cfg["base_color"].is_array() &&
            cfg["base_color"].size() == 3)
        {
            r.base_color[0] = cfg["base_color"][0].get<float>();
            r.base_color[1] = cfg["base_color"][1].get<float>();
            r.base_color[2] = cfg["base_color"][2].get<float>();
        }
        if (!loadHeightmapPng(hm_path, r, r.heights, r.hm_width, r.hm_height))
            continue;
        buildRegionMesh(r, subdivide);
        std::fprintf(stderr,
                     "[terrain] region '%s' loaded: %dx%d hm, %dm extent, "
                     "Y=[%.2f..%.2f], subdivide=%d\n",
                     r.name.c_str(), r.hm_width, r.hm_height, static_cast<int>(r.world_extent),
                     r.height_min, r.height_max, subdivide);
        sRegions.push_back(std::move(r));
    }
    return !sRegions.empty();
}

void shutdownTerrain()
{
    for (auto& r : sRegions)
    {
        if (r.vao != 0)
            glDeleteVertexArrays(1, &r.vao);
        if (r.vbo != 0)
            glDeleteBuffers(1, &r.vbo);
        if (r.ebo != 0)
            glDeleteBuffers(1, &r.ebo);
    }
    sRegions.clear();
}

int terrainRegionCount()
{
    return static_cast<int>(sRegions.size());
}

const TerrainRegion& terrainRegion(int idx)
{
    return sRegions[idx];
}

float sampleHeight(float world_x, float world_z)
{
    if (sRegions.empty())
        return 0.0f;
    const TerrainRegion& r = sRegions[0];
    if (r.subdivide <= 0 || r.mesh_y.empty())
        return 0.0f;
    // Locate the (x, z) within the mesh grid. Each quad spans
    // `world_extent / subdivide` meters.
    const float half = r.world_extent * 0.5f;
    const float u = (world_x - r.world_origin.x + half) / r.world_extent;
    const float v = (world_z - r.world_origin.y + half) / r.world_extent;
    if (u < 0.0f || u > 1.0f || v < 0.0f || v > 1.0f)
        return 0.0f;

    const int verts_per_side = r.subdivide + 1;
    const float fx = u * static_cast<float>(r.subdivide);
    const float fz = v * static_cast<float>(r.subdivide);
    const int ix = std::clamp(static_cast<int>(std::floor(fx)), 0, r.subdivide - 1);
    const int iz = std::clamp(static_cast<int>(std::floor(fz)), 0, r.subdivide - 1);
    const float tx = fx - static_cast<float>(ix); // 0..1 within the quad
    const float tz = fz - static_cast<float>(iz);

    // Quad corners — same indexing the mesh-build uses:
    //   i0 = (iz, ix)       i1 = (iz, ix+1)
    //   i2 = (iz+1, ix)     i3 = (iz+1, ix+1)
    const float y00 = r.mesh_y[iz * verts_per_side + ix];
    const float y10 = r.mesh_y[iz * verts_per_side + (ix + 1)];
    const float y01 = r.mesh_y[(iz + 1) * verts_per_side + ix];
    const float y11 = r.mesh_y[(iz + 1) * verts_per_side + (ix + 1)];

    // Quad split: triangle A = (i0, i3, i1), triangle B = (i0, i2, i3).
    // For point (tx, tz) inside the unit quad, A covers tz < tx
    // (lower-right half), B covers tz >= tx (upper-left half).
    // Barycentric within each triangle gives an exact match to
    // what the renderer interpolates.
    if (tz < tx)
    {
        // Triangle A: vertices at (0,0)=y00, (1,1)=y11, (1,0)=y10
        // Barycentric: A=(1-tx), B=tz, C=(tx-tz). Sums to 1.
        return (1.0f - tx) * y00 + tz * y11 + (tx - tz) * y10;
    }
    // Triangle B: vertices at (0,0)=y00, (0,1)=y01, (1,1)=y11
    // Barycentric: A=(1-tz), B=(tz-tx), C=tx. Sums to 1.
    return (1.0f - tz) * y00 + (tz - tx) * y01 + tx * y11;
}

} // namespace selva::world
