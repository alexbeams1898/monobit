#include "world/Terrain.h"

#include "Tunables.h"
#include "physics/PhysicsWorld.h"
#include "world/Collision.h"
#include "world/CryptLayout.h"
#include "world/StructureFootprints.h"
#include "world/TerrainModifiers.h"

#include <nlohmann/json.hpp>
#include <stb_image.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <limits>
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

// Player spawn X/Z loaded from config.json's top-level player_spawn
// block. Y is sampled from the heightmap at the time of use, not
// stored here.
glm::vec2 sPlayerSpawnXZ{0.0f, 0.0f};

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

// Cavern ceiling layer: one downward-facing quad per floor cell at
// Y = ceiling_y. Quads inside a registered cuts_ceiling slot at the
// ceiling Y are dropped (structure pierces here).
void buildEnclosureCeiling(TerrainRegion& r, int subdivide, float half, float step,
                           std::vector<Vertex>& verts, std::vector<std::uint32_t>& indices)
{
    const int verts_per_side = subdivide + 1;
    const std::uint32_t ceiling_vertex_base = static_cast<std::uint32_t>(verts.size());
    for (int iz = 0; iz < verts_per_side; ++iz)
    {
        for (int ix = 0; ix < verts_per_side; ++ix)
        {
            const float u = static_cast<float>(ix) / static_cast<float>(subdivide);
            const float v = static_cast<float>(iz) / static_cast<float>(subdivide);
            Vertex vert{};
            vert.position[0] = r.world_origin.x + (u * 2.0f - 1.0f) * half;
            vert.position[1] = r.ceiling_y;
            vert.position[2] = r.world_origin.y + (v * 2.0f - 1.0f) * half;
            vert.uv[0] = u;
            vert.uv[1] = v;
            vert.normal[0] = 0.0f;
            vert.normal[1] = -1.0f; // ceiling faces DOWN
            vert.normal[2] = 0.0f;
            verts.push_back(vert);
        }
    }
    for (int iz = 0; iz < subdivide; ++iz)
    {
        for (int ix = 0; ix < subdivide; ++ix)
        {
            const float quad_cx = r.world_origin.x - half + (static_cast<float>(ix) + 0.5f) * step;
            const float quad_cz = r.world_origin.y - half + (static_cast<float>(iz) + 0.5f) * step;
            if (engine::world::isInsideStructureSlot(r.name.c_str(), quad_cx, r.ceiling_y, quad_cz,
                                                     engine::world::SurfaceCut::Ceiling))
                continue;
            const std::uint32_t i0 =
                ceiling_vertex_base + static_cast<std::uint32_t>(iz * verts_per_side + ix);
            const std::uint32_t i1 = i0 + 1;
            const std::uint32_t i2 = i0 + static_cast<std::uint32_t>(verts_per_side);
            const std::uint32_t i3 = i2 + 1;
            // Reversed winding vs floor so the visible face is the bottom.
            indices.push_back(i0);
            indices.push_back(i1);
            indices.push_back(i3);
            indices.push_back(i0);
            indices.push_back(i3);
            indices.push_back(i2);
        }
    }
}

// One lateral wall, sub-clipped against cuts_wall slots. Fresh-vertex
// sub-quad emission lets the wall conform exactly to the slot's V
// bounds regardless of quad-grid alignment.
//
// Winding: triangle (i00,i10,i11) cross-product face normal is -X for
// X-constant walls and +Z for Z-constant walls. Flip when that points
// AWAY from the wall's desired inward_normal.
struct WallSpec
{
    int axis;             // 0 = X-constant wall; 2 = Z-constant wall
    float const_axis_pos; // X or Z value of the wall plane
    float var_axis_min;
    float var_axis_max;
    glm::vec3 inward_normal;
};

void emitWallSubQuad(const WallSpec& w, const engine::world::ClipRect& kr, bool flip_winding,
                     std::vector<Vertex>& verts, std::vector<std::uint32_t>& indices)
{
    const std::uint32_t base = static_cast<std::uint32_t>(verts.size());
    // Corner order: 0=(v_min,y_min), 1=(v_max,y_min), 2=(v_max,y_max), 3=(v_min,y_max).
    for (int corner = 0; corner < 4; ++corner)
    {
        const float yy = (corner < 2) ? kr.y_min : kr.y_max;
        const float vv = ((corner == 0) || (corner == 3)) ? kr.var_min : kr.var_max;
        Vertex vert{};
        if (w.axis == 0)
        {
            vert.position[0] = w.const_axis_pos;
            vert.position[2] = vv;
        }
        else
        {
            vert.position[0] = vv;
            vert.position[2] = w.const_axis_pos;
        }
        vert.position[1] = yy;
        vert.uv[0] = 0.0f;
        vert.uv[1] = 0.0f;
        vert.normal[0] = w.inward_normal.x;
        vert.normal[1] = w.inward_normal.y;
        vert.normal[2] = w.inward_normal.z;
        verts.push_back(vert);
    }
    const std::uint32_t i00 = base + 0;
    const std::uint32_t i10 = base + 1;
    const std::uint32_t i11 = base + 2;
    const std::uint32_t i01 = base + 3;
    if (flip_winding)
    {
        indices.push_back(i00);
        indices.push_back(i11);
        indices.push_back(i10);
        indices.push_back(i00);
        indices.push_back(i01);
        indices.push_back(i11);
    }
    else
    {
        indices.push_back(i00);
        indices.push_back(i10);
        indices.push_back(i11);
        indices.push_back(i00);
        indices.push_back(i11);
        indices.push_back(i01);
    }
}

void buildWallMesh(const TerrainRegion& r, const WallSpec& w, int wall_cells_horiz,
                   int wall_cells_vert, float wall_step_v, float y_bot, std::vector<Vertex>& verts,
                   std::vector<std::uint32_t>& indices)
{
    const float var_step = (w.var_axis_max - w.var_axis_min) / static_cast<float>(wall_cells_horiz);
    const bool flip_winding =
        (w.axis == 0) ? (w.inward_normal.x > 0.0f) : (w.inward_normal.z < 0.0f);
    for (int iy = 0; iy < wall_cells_vert; ++iy)
    {
        const float y_quad_min = y_bot + static_cast<float>(iy) * wall_step_v;
        const float y_quad_max = y_quad_min + wall_step_v;
        for (int iv = 0; iv < wall_cells_horiz; ++iv)
        {
            const float v_quad_min = w.var_axis_min + static_cast<float>(iv) * var_step;
            const float v_quad_max = v_quad_min + var_step;

            engine::world::ClipQuadQuery q{};
            q.region_name = r.name.c_str();
            q.var_axis = w.axis;
            q.const_pos = w.const_axis_pos;
            q.quad = {v_quad_min, v_quad_max, y_quad_min, y_quad_max};
            q.surface = engine::world::SurfaceCut::Wall;
            engine::world::ClipRect kept[8];
            const int n_kept = engine::world::clipQuadAgainstStructureSlots(q, kept, 8);
            for (int k = 0; k < n_kept; ++k)
                emitWallSubQuad(w, kept[k], flip_winding, verts, indices);
        }
    }
}

// Build all four lateral walls + ceiling for an enclosed region.
// Structure footprints (cuts_ceiling / cuts_wall) carve their slots.
void buildEnclosureGeometry(TerrainRegion& r, int subdivide, float half, float step,
                            std::vector<Vertex>& verts, std::vector<std::uint32_t>& indices)
{
    buildEnclosureCeiling(r, subdivide, half, step, verts, indices);

    const float x_min = r.world_origin.x - half;
    const float x_max = r.world_origin.x + half;
    const float z_min = r.world_origin.y - half;
    const float z_max = r.world_origin.y + half;
    const float y_bot = r.wall_min_y;
    const float wall_height = r.ceiling_y - y_bot;
    const int wall_cells_horiz = subdivide;
    const int wall_cells_vert = std::max(1, static_cast<int>(wall_height / step));
    const float wall_step_v = wall_height / static_cast<float>(wall_cells_vert);

    const WallSpec walls[4] = {
        {0, x_min, z_min, z_max, {+1.0f, 0.0f, 0.0f}}, // -X wall
        {0, x_max, z_min, z_max, {-1.0f, 0.0f, 0.0f}}, // +X wall
        {2, z_min, x_min, x_max, {0.0f, 0.0f, +1.0f}}, // -Z wall
        {2, z_max, x_min, x_max, {0.0f, 0.0f, -1.0f}}, // +Z wall
    };
    for (const auto& w : walls)
        buildWallMesh(r, w, wall_cells_horiz, wall_cells_vert, wall_step_v, y_bot, verts, indices);
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

    // Sample heightmap per vertex (UV = [0..1] across the region),
    // then apply any registered TerrainModifiers (chapel plateau,
    // pits, etc) so the final Y reflects both the natural terrain
    // and any architecture-driven deformations. Same final Y feeds
    // render mesh + physics trimesh — single source.
    for (int iz = 0; iz < verts_per_side; ++iz)
    {
        for (int ix = 0; ix < verts_per_side; ++ix)
        {
            const float u = static_cast<float>(ix) / static_cast<float>(subdivide);
            const float v = static_cast<float>(iz) / static_cast<float>(subdivide);
            const float wx = r.world_origin.x + (u * 2.0f - 1.0f) * half;
            const float wz = r.world_origin.y + (v * 2.0f - 1.0f) * half;
            // Heightmap PNG encodes Y in [height_min, height_max] which
            // is treated as an offset; r.y_offset places the encoded
            // range anywhere in world Y. Selva surface: y_offset=0 so
            // height_min/max ARE world Y directly. Underground regions
            // (Limbo, etc.): y_offset is large/negative so the same
            // PNG-encoding can place terrain anywhere without losing
            // precision.
            const float base_y =
                bilinearSample(r.heights, r.hm_width, r.hm_height, u, v) + r.y_offset;
            const float wy = engine::world::applyTerrainModifiers(r.name.c_str(), wx, wz, base_y);
            const int vi = iz * verts_per_side + ix;
            verts[vi].position[0] = wx;
            verts[vi].position[1] = wy;
            verts[vi].position[2] = wz;
            verts[vi].uv[0] = u;
            verts[vi].uv[1] = v;
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

    // Indices: render + physics use the SAME index buffer (every quad).
    // Quads whose centroid falls inside a registered Hole-mode modifier
    // are dropped entirely — neither rendered nor collidable. This is
    // how true vertical holes (stair shafts, well openings) get carved
    // out: the heightmap vertex grid is too coarse (2.7m spacing at
    // current Selva settings) to express a small flat-bottomed hole
    // via vertex Y alone — bilinear interpolation between rim vertices
    // fills it back in. Quad removal sidesteps the resolution limit.
    std::vector<std::uint32_t> indices;
    indices.reserve(static_cast<std::size_t>(subdivide) * static_cast<std::size_t>(subdivide) * 6u);
    for (int iz = 0; iz < subdivide; ++iz)
    {
        for (int ix = 0; ix < subdivide; ++ix)
        {
            const float quad_cx = r.world_origin.x - half + (static_cast<float>(ix) + 0.5f) * step;
            const float quad_cz = r.world_origin.y - half + (static_cast<float>(iz) + 0.5f) * step;
            if (engine::world::insideTerrainHole(r.name.c_str(), quad_cx, quad_cz))
                continue;
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
    // Optional enclosure: ceiling + four lateral walls. Underground
    // regions declare ceiling_y + wall_min_y in config; surface
    // regions skip this entirely. Geometry is appended to the same
    // vertex + index buffers so the whole enclosure draws as one mesh
    // with one Jolt body. Ceiling normals point DOWN (lit from below);
    // wall normals point INWARD (toward region interior).
    if (r.has_ceiling)
    {
        buildEnclosureGeometry(r, subdivide, half, step, verts, indices);
    }

    r.index_count = static_cast<int>(indices.size());

    // Physics + render share the same vertex positions: terrain Y
    // already includes any registered TerrainModifiers (applied above).
    // No separate "physics pit" hack — modifiers are the source of
    // truth for terrain deformation.
    r.cpu_positions.reserve(verts.size());
    for (const auto& v : verts)
        r.cpu_positions.emplace_back(v.position[0], v.position[1], v.position[2]);
    r.cpu_indices = indices;

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

// Forward decl: sampleHeight is defined later (namespace-scope) but
// the audit helper below calls it.
} // namespace

float sampleHeight(float world_x, float world_z);

namespace
{

const TerrainRegion* findRegionByName(const std::vector<TerrainRegion>& regions, const char* name)
{
    if (name == nullptr)
        return nullptr;
    for (const auto& r : regions)
        if (r.name == name)
            return &r;
    return nullptr;
}

void dumpRimAuditSample(std::FILE* audit, const engine::world::StructureFootprint& fp,
                        const TerrainRegion& tr, float x, float z)
{
    const float half = tr.world_extent * 0.5f;
    const float yrange = tr.height_max - tr.height_min;
    const float u = (x - (tr.world_origin.x - half)) / tr.world_extent;
    const float v = (z - (tr.world_origin.y - half)) / tr.world_extent;
    int png_raw = -1;
    float png_decoded_y = 0.0f;
    const bool in_png =
        (u >= 0.0f && u <= 1.0f && v >= 0.0f && v <= 1.0f && tr.hm_width > 0 && tr.hm_height > 0);
    if (in_png)
    {
        const int px = static_cast<int>(u * static_cast<float>(tr.hm_width - 1));
        const int py = static_cast<int>(v * static_cast<float>(tr.hm_height - 1));
        const float decoded =
            tr.heights[static_cast<size_t>(py) * static_cast<size_t>(tr.hm_width) +
                       static_cast<size_t>(px)];
        png_decoded_y = decoded;
        const float t = (decoded - tr.height_min) / yrange;
        png_raw = static_cast<int>(std::lround(t * 255.0f));
    }
    const float bilinear_y =
        bilinearSample(tr.heights, tr.hm_width, tr.hm_height, u, v) + tr.y_offset;
    const float modified_y =
        engine::world::applyTerrainModifiers(tr.name.c_str(), x, z, bilinear_y);
    const float mesh_y = selva::world::sampleHeight(x, z);
    const bool inside = std::fabs(x - fp.center_xz.x) <= fp.half_extents_xz.x &&
                        std::fabs(z - fp.center_xz.y) <= fp.half_extents_xz.y;
    std::fprintf(audit,
                 "(%7.2f,%7.2f) | png=%3d decoded=%7.3f bilin=%7.3f mod=%7.3f mesh=%7.3f "
                 "| inside=%d\n",
                 x, z, png_raw, png_decoded_y, bilinear_y, modified_y, mesh_y, inside ? 1 : 0);
}

void dumpRimAuditFootprint(std::FILE* audit, const engine::world::StructureFootprint& fp,
                           const TerrainRegion& tr)
{
    std::fprintf(audit,
                 "===== footprint '%s' region=%s center=(%.2f,%.2f) "
                 "half=(%.2f,%.2f) cuts_rim=%d =====\n",
                 fp.debug_name ? fp.debug_name : "(no name)",
                 fp.region_name ? fp.region_name : "(global)", fp.center_xz.x, fp.center_xz.y,
                 fp.half_extents_xz.x, fp.half_extents_xz.y, fp.cuts_rim ? 1 : 0);
    constexpr float kSampleStep = 2.0f;
    constexpr float kMargin = 20.0f;
    const float x_lo = fp.center_xz.x - fp.half_extents_xz.x - kMargin;
    const float x_hi = fp.center_xz.x + fp.half_extents_xz.x + kMargin;
    const float z_lo = fp.center_xz.y - fp.half_extents_xz.y - kMargin;
    const float z_hi = fp.center_xz.y + fp.half_extents_xz.y + kMargin;
    const int nz = static_cast<int>((z_hi - z_lo) / kSampleStep) + 1;
    const int nx = static_cast<int>((x_hi - x_lo) / kSampleStep) + 1;
    for (int iz = 0; iz < nz; ++iz)
    {
        const float z = z_lo + static_cast<float>(iz) * kSampleStep;
        for (int ix = 0; ix < nx; ++ix)
        {
            const float x = x_lo + static_cast<float>(ix) * kSampleStep;
            dumpRimAuditSample(audit, fp, tr, x, z);
        }
        std::fprintf(audit, "\n");
    }
}

void dumpRimAuditLog(const std::vector<TerrainRegion>& regions)
{
    std::FILE* audit = std::fopen("rim-audit.log", "w");
    if (audit == nullptr)
        return;
    std::fprintf(audit, "# Rim-audit dump. One block per cuts_rim StructureFootprint.\n"
                        "# Columns: x z | png_raw[0..255] png_decoded_y bilinear_y "
                        "after_modifiers_y mesh_y_nearest | inside_footprint\n#\n");
    const int fp_count = engine::world::structureFootprintCount();
    for (int fi = 0; fi < fp_count; ++fi)
    {
        const auto& fp = engine::world::structureFootprintAt(fi);
        if (!fp.cuts_rim)
            continue;
        const TerrainRegion* tr = findRegionByName(regions, fp.region_name);
        if (tr == nullptr)
            continue;
        dumpRimAuditFootprint(audit, fp, *tr);
    }
    std::fclose(audit);
    std::fprintf(stderr, "[terrain] wrote rim-audit.log\n");
}

} // namespace

namespace
{
// Load a JSON array of 3 floats into a float[3] dest if present and
// well-formed. Missing or wrong-shaped keys leave dest untouched.
void readVec3Field(const nlohmann::json& cfg, const char* key, float dest[3])
{
    if (!cfg.contains(key) || !cfg[key].is_array() || cfg[key].size() != 3)
        return;
    dest[0] = cfg[key][0].get<float>();
    dest[1] = cfg[key][1].get<float>();
    dest[2] = cfg[key][2].get<float>();
}

void readPlayerSpawn(const nlohmann::json& doc)
{
    if (doc.contains("player_spawn") && doc["player_spawn"].is_object())
    {
        const auto& s = doc["player_spawn"];
        sPlayerSpawnXZ.x = s.value("x", 0.0f);
        sPlayerSpawnXZ.y = s.value("z", 0.0f);
        std::fprintf(stderr, "[terrain] player spawn XZ = (%.2f, %.2f)\n", sPlayerSpawnXZ.x,
                     sPlayerSpawnXZ.y);
    }
    else
    {
        std::fprintf(stderr, "[terrain] config missing 'player_spawn'; defaulting to (0, 0)\n");
        sPlayerSpawnXZ = glm::vec2{0.0f, 0.0f};
    }
}

// Populate fields on `r` from one region's config entry. Returns the
// subdivide count and stamps the heightmap path into hm_path_out for
// the caller to load.
int parseRegionConfig(const std::string& name, const nlohmann::json& cfg, TerrainRegion& r,
                      std::string& hm_path_out)
{
    r.name = name;
    hm_path_out = cfg.value("heightmap", std::string());
    const auto origin = cfg.value("world_origin", std::vector<float>{0.0f, 0.0f});
    if (origin.size() >= 2)
        r.world_origin = glm::vec2(origin[0], origin[1]);
    r.world_extent = cfg.value("world_extent", 256.0f);
    r.height_min = cfg.value("height_range_min", -2.0f);
    r.height_max = cfg.value("height_range_max", 15.0f);
    r.y_offset = cfg.value("y_offset", 0.0f);
    readVec3Field(cfg, "base_color", r.base_color);
    readVec3Field(cfg, "tone_dark", r.tone_dark);
    readVec3Field(cfg, "tone_light", r.tone_light);
    r.footstep_sound_id = cfg.value("footstep_sound_id", std::string{"footstep_grass"});
    // Optional enclosure (ceiling + walls). Presence of "ceiling_y"
    // in config enables it; underground layers declare a ceiling and
    // wall bottom. Hole cutouts come from registered
    // StructureFootprints (cuts_ceiling / cuts_wall flags), not a
    // per-region config list.
    if (cfg.contains("ceiling_y"))
    {
        r.has_ceiling = true;
        r.ceiling_y = cfg.value("ceiling_y", 0.0f);
        r.wall_min_y = cfg.value("wall_min_y", r.ceiling_y - 10.0f);
    }
    r.sun_multiplier = cfg.value("sun_multiplier", 1.0f);
    readVec3Field(cfg, "sky_ambient", r.sky_ambient);
    readVec3Field(cfg, "ground_ambient", r.ground_ambient);
    return cfg.value("subdivide", 128);
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

    readPlayerSpawn(doc);

    for (auto it = doc["regions"].begin(); it != doc["regions"].end(); ++it)
    {
        TerrainRegion r;
        std::string hm_path;
        const int subdivide = parseRegionConfig(it.key(), it.value(), r, hm_path);
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

    dumpRimAuditLog(sRegions);
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

const TerrainRegion* terrainRegionAt(float world_x, float world_z)
{
    // First region whose XZ AABB contains (x, z) wins. Regions should
    // not overlap in XZ. For per-surface lookups (e.g. footstep bank),
    // don't use XZ — use the body the foot raycast hit and look up
    // its region by name via terrainRegionAtName.
    for (const auto& r : sRegions)
    {
        const float half = r.world_extent * 0.5f;
        const float dx = std::abs(world_x - r.world_origin.x);
        const float dz = std::abs(world_z - r.world_origin.y);
        if (dx <= half && dz <= half)
            return &r;
    }
    return nullptr;
}

const TerrainRegion* terrainRegionAtName(const char* name)
{
    if (name == nullptr)
        return nullptr;
    for (const auto& r : sRegions)
        if (r.name == name)
            return &r;
    return nullptr;
}

namespace
{
// Returns true and writes the sampled Y to *out_y if (world_x, world_z)
// falls within this region's XZ AABB. Returns false if outside.
bool sampleRegion(const TerrainRegion& r, float world_x, float world_z, float* out_y)
{
    if (r.subdivide <= 0 || r.mesh_y.empty())
        return false;
    const float half = r.world_extent * 0.5f;
    const float u = (world_x - r.world_origin.x + half) / r.world_extent;
    const float v = (world_z - r.world_origin.y + half) / r.world_extent;
    if (u < 0.0f || u > 1.0f || v < 0.0f || v > 1.0f)
        return false;

    const int verts_per_side = r.subdivide + 1;
    const float fx = u * static_cast<float>(r.subdivide);
    const float fz = v * static_cast<float>(r.subdivide);
    const int ix = std::clamp(static_cast<int>(std::floor(fx)), 0, r.subdivide - 1);
    const int iz = std::clamp(static_cast<int>(std::floor(fz)), 0, r.subdivide - 1);
    const float tx = fx - static_cast<float>(ix);
    const float tz = fz - static_cast<float>(iz);

    const float y00 = r.mesh_y[iz * verts_per_side + ix];
    const float y10 = r.mesh_y[iz * verts_per_side + (ix + 1)];
    const float y01 = r.mesh_y[(iz + 1) * verts_per_side + ix];
    const float y11 = r.mesh_y[(iz + 1) * verts_per_side + (ix + 1)];

    // Quad split: triangle A = (i0, i3, i1), triangle B = (i0, i2, i3).
    // A covers tz < tx (lower-right half), B covers tz >= tx (upper-
    // left half). Barycentric within each triangle gives an exact
    // match to what the renderer interpolates.
    if (tz < tx)
        *out_y = (1.0f - tx) * y00 + tz * y11 + (tx - tz) * y10;
    else
        *out_y = (1.0f - tz) * y00 + (tz - tx) * y01 + tx * y11;
    return true;
}
} // namespace

float sampleHeight(float world_x, float world_z)
{
    // Iterate regions in registration order — first whose XZ AABB
    // contains (x, z) wins. Regions should not overlap in XZ; if
    // they do, the system can't disambiguate the answer here. For
    // surface-specific queries (e.g. footstep bank by region), don't
    // use sampleHeight — use the body returned by the foot raycast
    // and look up its tagged region instead.
    for (const auto& r : sRegions)
    {
        float y = 0.0f;
        if (sampleRegion(r, world_x, world_z, &y))
            return y;
    }
    return 0.0f;
}

glm::vec2 playerSpawnXZ()
{
    return sPlayerSpawnXZ;
}

float groundHeight(float world_x, float world_z, float current_y)
{
    // Real-physics doctrine: ground is whatever solid surface the
    // sun-of-gravity ray hits at this XZ. Cast straight down from
    // well above the actor; the nearest hit IS the ground.
    constexpr float kRayStartAbove = 100.0f;
    constexpr float kRayMaxDistance = 500.0f;
    const bool use_ceiling = current_y != -std::numeric_limits<float>::infinity();
    const float origin_y = use_ceiling ? (current_y + kRayStartAbove) : kRayStartAbove;
    const glm::vec3 origin(world_x, origin_y, world_z);
    const auto hit =
        engine::physics::raycast(origin, glm::vec3(0.0f, -1.0f, 0.0f), kRayMaxDistance);
    if (!hit.hit)
    {
        // Outside any physics geometry — fall back to heightmap sample.
        // Happens before physics bodies are loaded for the active scene
        // (init order) or in zones with no terrain/architecture present.
        return sampleHeight(world_x, world_z);
    }

    const bool log_on = selva::tuning::current().debug_ground_height_log;
    if (log_on)
    {
        static FILE* sGroundLog = nullptr;
        static int sGroundFrame = 0;
        if (sGroundLog == nullptr)
            sGroundLog = std::fopen("ground-debug.log", "w");
        if (sGroundLog != nullptr)
        {
            const char* nm = engine::physics::bodyDebugName(hit.body);
            std::fprintf(sGroundLog, "[%d] xz=(%.3f,%.3f) cur_y=%.3f -> y=%.3f body='%s'\n",
                         sGroundFrame, world_x, world_z, current_y, hit.position.y, nm);
            std::fflush(sGroundLog);
            ++sGroundFrame;
        }
    }
    return hit.position.y;
}

} // namespace selva::world
