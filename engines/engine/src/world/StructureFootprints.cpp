#include "world/StructureFootprints.h"

#include "world/TerrainModifiers.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <vector>

namespace engine::world
{

namespace
{
std::vector<StructureFootprint> sFootprints;

bool regionMatches(const StructureFootprint& f, const char* query_region)
{
    if (query_region == nullptr)
        return true; // caller opted out of filtering
    if (f.region_name == nullptr)
        return true; // footprint applies to any region
    return std::strcmp(f.region_name, query_region) == 0;
}

bool insideRect(const StructureFootprint& f, float x, float z)
{
    return std::fabs(x - f.center_xz.x) <= f.half_extents_xz.x &&
           std::fabs(z - f.center_xz.y) <= f.half_extents_xz.y;
}
} // namespace

void registerStructureFootprint(const StructureFootprint& f)
{
    sFootprints.push_back(f);

    // Physics-side: a Hole modifier at this rect drops terrain
    // trimesh quads inside. Only emit when cuts_floor — a footprint
    // that only cuts the rim or ceiling shouldn't punch through the
    // physics floor (the structure isn't pierced by the floor).
    if (f.cuts_floor)
    {
        TerrainModifier hole;
        hole.center_xz = f.center_xz;
        hole.half_extents_xz = f.half_extents_xz;
        hole.mode = TerrainModifier::Mode::Hole;
        hole.region_name = f.region_name;
        hole.debug_name = f.debug_name;
        registerTerrainModifier(hole);
    }
}

void clearStructureFootprints()
{
    sFootprints.clear();
}

int structureFootprintCount()
{
    return static_cast<int>(sFootprints.size());
}

const StructureFootprint& structureFootprintAt(int idx)
{
    return sFootprints[idx];
}

bool serializeStructureRegistryJson(const char* out_path)
{
    std::FILE* fp = std::fopen(out_path, "wb");
    if (fp == nullptr)
    {
        std::fprintf(stderr, "[StructureFootprints] open failed: %s\n", out_path);
        return false;
    }

    // Hand-rolled JSON to keep this engine module dependency-free
    // (nlohmann::json is available in the engine but pulling it here
    // would make the dump binary heavier than it needs to be).
    // schema_version lets future tools detect/handle field additions
    // without silently breaking on old generated files.
    std::fprintf(fp, "{\n");
    std::fprintf(fp, "  \"schema_version\": 1,\n");
    std::fprintf(fp, "  \"structures\": [\n");
    for (size_t i = 0; i < sFootprints.size(); ++i)
    {
        const auto& f = sFootprints[i];
        std::fprintf(fp, "    {\n");
        std::fprintf(fp, "      \"name\": \"%s\",\n", f.debug_name ? f.debug_name : "");
        if (f.region_name != nullptr)
            std::fprintf(fp, "      \"region\": \"%s\",\n", f.region_name);
        else
            std::fprintf(fp, "      \"region\": null,\n");
        std::fprintf(fp, "      \"center_xz\": [%.6f, %.6f],\n", f.center_xz.x, f.center_xz.y);
        std::fprintf(fp, "      \"half_extents_xz\": [%.6f, %.6f],\n", f.half_extents_xz.x,
                     f.half_extents_xz.y);
        std::fprintf(fp, "      \"cuts\": {\n");
        std::fprintf(fp, "        \"floor\": %s,\n", f.cuts_floor ? "true" : "false");
        std::fprintf(fp, "        \"rim\": %s,\n", f.cuts_rim ? "true" : "false");
        std::fprintf(fp, "        \"ceiling\": %s,\n", f.cuts_ceiling ? "true" : "false");
        std::fprintf(fp, "        \"wall\": %s\n", f.cuts_wall ? "true" : "false");
        std::fprintf(fp, "      },\n");

        const auto& vp = f.vertical_profile;
        if (vp.nx > 0 && vp.nz > 0 &&
            vp.cells.size() == static_cast<size_t>(vp.nx) * static_cast<size_t>(vp.nz))
        {
            std::fprintf(fp, "      \"vertical_profile\": {\n");
            std::fprintf(fp, "        \"nx\": %d,\n", vp.nx);
            std::fprintf(fp, "        \"nz\": %d,\n", vp.nz);
            std::fprintf(fp, "        \"cells\": [\n");
            for (int iz = 0; iz < vp.nz; ++iz)
            {
                std::fprintf(fp, "          [");
                for (int ix = 0; ix < vp.nx; ++ix)
                {
                    const auto& c = vp.cells[static_cast<size_t>(iz) * static_cast<size_t>(vp.nx) +
                                             static_cast<size_t>(ix)];
                    std::fprintf(fp, "[%.6f, %.6f]%s", c.y_min, c.y_max,
                                 (ix + 1 < vp.nx) ? ", " : "");
                }
                std::fprintf(fp, "]%s\n", (iz + 1 < vp.nz) ? "," : "");
            }
            std::fprintf(fp, "        ]\n");
            std::fprintf(fp, "      }\n");
        }
        else
        {
            std::fprintf(fp, "      \"vertical_profile\": null\n");
        }
        std::fprintf(fp, "    }%s\n", (i + 1 < sFootprints.size()) ? "," : "");
    }
    std::fprintf(fp, "  ]\n");
    std::fprintf(fp, "}\n");
    std::fclose(fp);
    return true;
}

bool sampleStructureSlot(const StructureFootprint& f, float world_x, float world_z,
                         float& out_y_min, float& out_y_max)
{
    const auto& vp = f.vertical_profile;
    if (vp.nx <= 0 || vp.nz <= 0 || vp.cells.empty())
        return false;
    if (!insideRect(f, world_x, world_z))
        return false;

    const float u =
        (world_x - (f.center_xz.x - f.half_extents_xz.x)) / (2.0f * f.half_extents_xz.x);
    const float v =
        (world_z - (f.center_xz.y - f.half_extents_xz.y)) / (2.0f * f.half_extents_xz.y);
    const float fx = u * static_cast<float>(vp.nx - 1);
    const float fz = v * static_cast<float>(vp.nz - 1);
    const int ix0 = std::clamp(static_cast<int>(std::floor(fx)), 0, vp.nx - 1);
    const int iz0 = std::clamp(static_cast<int>(std::floor(fz)), 0, vp.nz - 1);
    const int ix1 = std::min(ix0 + 1, vp.nx - 1);
    const int iz1 = std::min(iz0 + 1, vp.nz - 1);
    const float tx = std::clamp(fx - static_cast<float>(ix0), 0.0f, 1.0f);
    const float tz = std::clamp(fz - static_cast<float>(iz0), 0.0f, 1.0f);

    auto cellAt = [&](int ix, int iz) -> const VerticalProfileCell&
    {
        return vp
            .cells[static_cast<size_t>(iz) * static_cast<size_t>(vp.nx) + static_cast<size_t>(ix)];
    };
    const auto& c00 = cellAt(ix0, iz0);
    const auto& c10 = cellAt(ix1, iz0);
    const auto& c01 = cellAt(ix0, iz1);
    const auto& c11 = cellAt(ix1, iz1);

    auto isEmpty = [](const VerticalProfileCell& c) { return c.y_min > c.y_max; };
    if (isEmpty(c00) || isEmpty(c10) || isEmpty(c01) || isEmpty(c11))
        return false;

    const float y_min_x0 = c00.y_min + (c01.y_min - c00.y_min) * tz;
    const float y_min_x1 = c10.y_min + (c11.y_min - c10.y_min) * tz;
    const float y_max_x0 = c00.y_max + (c01.y_max - c00.y_max) * tz;
    const float y_max_x1 = c10.y_max + (c11.y_max - c10.y_max) * tz;
    out_y_min = y_min_x0 + (y_min_x1 - y_min_x0) * tx;
    out_y_max = y_max_x0 + (y_max_x1 - y_max_x0) * tx;
    return true;
}

bool isInsideStructureFootprint(const char* region_name, float world_x, float world_z,
                                SurfaceCut surface)
{
    for (const auto& f : sFootprints)
    {
        if (!regionMatches(f, region_name))
            continue;
        bool cuts = false;
        switch (surface)
        {
        case SurfaceCut::Floor:
            cuts = f.cuts_floor;
            break;
        case SurfaceCut::Rim:
            cuts = f.cuts_rim;
            break;
        case SurfaceCut::Ceiling:
            cuts = f.cuts_ceiling;
            break;
        case SurfaceCut::Wall:
            cuts = f.cuts_wall;
            break;
        }
        if (!cuts)
            continue;
        if (insideRect(f, world_x, world_z))
            return true;
    }
    return false;
}

bool isInsideStructureSlot(const char* region_name, float world_x, float world_y, float world_z,
                           SurfaceCut surface)
{
    for (const auto& f : sFootprints)
    {
        if (!regionMatches(f, region_name))
            continue;
        bool cuts = false;
        switch (surface)
        {
        case SurfaceCut::Floor:
            cuts = f.cuts_floor;
            break;
        case SurfaceCut::Rim:
            cuts = f.cuts_rim;
            break;
        case SurfaceCut::Ceiling:
            cuts = f.cuts_ceiling;
            break;
        case SurfaceCut::Wall:
            cuts = f.cuts_wall;
            break;
        }
        if (!cuts)
            continue;

        float y_min = 0.0f;
        float y_max = 0.0f;
        if (sampleStructureSlot(f, world_x, world_z, y_min, y_max))
        {
            if (world_y >= y_min && world_y <= y_max)
                return true;
        }
        else if (f.vertical_profile.nx == 0 || f.vertical_profile.nz == 0)
        {
            // No profile authored — legacy "drop everything in rect".
            if (insideRect(f, world_x, world_z))
                return true;
        }
    }
    return false;
}

namespace
{
// Subtract subtractor rect from rect r, write up to 4 sub-rects to out.
// All rects are AABB in (var, y). Returns count of sub-rects.
int subtractRect(const ClipRect& r, const ClipRect& s, ClipRect* out)
{
    // Compute overlap.
    const float ov_vmin = std::max(r.var_min, s.var_min);
    const float ov_vmax = std::min(r.var_max, s.var_max);
    const float ov_ymin = std::max(r.y_min, s.y_min);
    const float ov_ymax = std::min(r.y_max, s.y_max);
    if (ov_vmin >= ov_vmax || ov_ymin >= ov_ymax)
    {
        // No overlap: r survives unchanged.
        out[0] = r;
        return 1;
    }
    int n = 0;
    // Left strip (var < ov_vmin)
    if (ov_vmin > r.var_min)
        out[n++] = {r.var_min, ov_vmin, r.y_min, r.y_max};
    // Right strip (var > ov_vmax)
    if (ov_vmax < r.var_max)
        out[n++] = {ov_vmax, r.var_max, r.y_min, r.y_max};
    // Bottom strip in overlap V column (y < ov_ymin)
    if (ov_ymin > r.y_min)
        out[n++] = {ov_vmin, ov_vmax, r.y_min, ov_ymin};
    // Top strip in overlap V column (y > ov_ymax)
    if (ov_ymax < r.y_max)
        out[n++] = {ov_vmin, ov_vmax, ov_ymax, r.y_max};
    return n;
}
} // namespace

namespace
{
// Does footprint f's `surface` flag indicate it should cut?
bool footprintCutsSurface(const StructureFootprint& f, SurfaceCut surface)
{
    switch (surface)
    {
    case SurfaceCut::Floor:
        return f.cuts_floor;
    case SurfaceCut::Rim:
        return f.cuts_rim;
    case SurfaceCut::Ceiling:
        return f.cuts_ceiling;
    case SurfaceCut::Wall:
        return f.cuts_wall;
    }
    return false;
}

// Resolve a footprint's V-axis range on the wall plane (var_axis=0:
// X-constant wall → V=Z; var_axis=2: Z-constant wall → V=X). Returns
// false if the footprint doesn't straddle the wall plane.
bool footprintVarRangeOnPlane(const StructureFootprint& f, int var_axis, float const_pos,
                              float& out_var_min, float& out_var_max)
{
    if (var_axis == 0)
    {
        if (std::fabs(const_pos - f.center_xz.x) > f.half_extents_xz.x)
            return false;
        out_var_min = f.center_xz.y - f.half_extents_xz.y;
        out_var_max = f.center_xz.y + f.half_extents_xz.y;
    }
    else
    {
        if (std::fabs(const_pos - f.center_xz.y) > f.half_extents_xz.y)
            return false;
        out_var_min = f.center_xz.x - f.half_extents_xz.x;
        out_var_max = f.center_xz.x + f.half_extents_xz.x;
    }
    return true;
}

// Slot Y range at the wall plane × footprint-center cross. Returns
// false when the footprint has a profile but the sample point falls
// in an empty cell. When the footprint has no profile, fills with
// the input fallback range (legacy "drop everything in rect").
bool resolveSlotY(const StructureFootprint& f, int var_axis, float const_pos, float fallback_min,
                  float fallback_max, float& out_y_min, float& out_y_max)
{
    out_y_min = fallback_min;
    out_y_max = fallback_max;
    if (f.vertical_profile.nx <= 0 || f.vertical_profile.nz <= 0)
        return true;
    const float sample_x = (var_axis == 0) ? const_pos : f.center_xz.x;
    const float sample_z = (var_axis == 0) ? f.center_xz.y : const_pos;
    return sampleStructureSlot(f, sample_x, sample_z, out_y_min, out_y_max);
}

// Expand (y_min, y_max) outward to the nearest grid lines. Origin
// + step define the grid; step <= 0 disables snapping.
void snapYToGrid(float& y_min, float& y_max, float y_origin, float y_step)
{
    if (y_step <= 0.0f)
        return;
    const float steps_min = std::floor((y_min - y_origin) / y_step);
    const float steps_max = std::ceil((y_max - y_origin) / y_step);
    y_min = y_origin + steps_min * y_step;
    y_max = y_origin + steps_max * y_step;
}

// Subtract slot from every rect in `work`; rewrite work in place.
// Capped at work_cap entries (silently drops overflow).
constexpr int kMaxClipWork = 16;
void subtractSlotFromWorkList(const ClipRect& slot, ClipRect* work, int& work_n)
{
    ClipRect next[kMaxClipWork];
    int next_n = 0;
    for (int i = 0; i < work_n; ++i)
    {
        ClipRect sub[4];
        const int sub_n = subtractRect(work[i], slot, sub);
        for (int s = 0; s < sub_n && next_n < kMaxClipWork; ++s)
            next[next_n++] = sub[s];
    }
    work_n = next_n;
    for (int i = 0; i < work_n; ++i)
        work[i] = next[i];
}
} // namespace

int clipQuadAgainstStructureSlots(const ClipQuadQuery& q, ClipRect* out, int out_capacity)
{
    ClipRect work[kMaxClipWork];
    int work_n = 1;
    work[0] = q.quad;

    for (const auto& f : sFootprints)
    {
        if (!regionMatches(f, q.region_name))
            continue;
        if (!footprintCutsSurface(f, q.surface))
            continue;

        float fp_var_min;
        float fp_var_max;
        if (!footprintVarRangeOnPlane(f, q.var_axis, q.const_pos, fp_var_min, fp_var_max))
            continue;

        float slot_y_min;
        float slot_y_max;
        if (!resolveSlotY(f, q.var_axis, q.const_pos, q.quad.y_min, q.quad.y_max, slot_y_min,
                          slot_y_max))
            continue;

        snapYToGrid(slot_y_min, slot_y_max, q.y_grid_origin, q.y_grid_step);

        const ClipRect slot{fp_var_min, fp_var_max, slot_y_min, slot_y_max};
        subtractSlotFromWorkList(slot, work, work_n);
    }

    const int n = std::min(work_n, out_capacity);
    for (int i = 0; i < n; ++i)
        out[i] = work[i];
    return n;
}

} // namespace engine::world
