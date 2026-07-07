#include "world/TerrainModifiers.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <limits>

namespace engine::world
{

namespace
{
std::vector<TerrainModifier> sModifiers;

// Hermite smoothstep — 3t^2 - 2t^3. Returns 0 at t<=0, 1 at t>=1.
inline float smoothstep01(float t)
{
    if (t <= 0.0f)
        return 0.0f;
    if (t >= 1.0f)
        return 1.0f;
    return t * t * (3.0f - 2.0f * t);
}

// ---------------------------------------------------------------------
// Polygon geometry primitives — used by PolygonFlushAt mode.
// Support concave polygons; verts assumed counter-clockwise-wound (see
// header). Point-in-polygon uses winding number for robustness across
// concavity (crossing-number can flake on edge-crossing near-vertex
// cases; winding number is stable). Distance to polygon is min distance
// to any edge segment, signed by inside/outside.
// ---------------------------------------------------------------------

// Distance from point p to segment ab, plus t of nearest point on the
// segment (clamped to [0,1]) and the actual foot point. Squared
// distance returned to avoid a sqrt in the hot loop; caller sqrts
// when it needs the real distance.
inline float pointSegmentDistanceSq(const glm::vec2& p, const glm::vec2& a, const glm::vec2& b)
{
    const glm::vec2 ab = b - a;
    const glm::vec2 ap = p - a;
    const float ab_len_sq = ab.x * ab.x + ab.y * ab.y;
    if (ab_len_sq <= 1e-8f)
    {
        // Degenerate zero-length edge; treat as a point.
        return ap.x * ap.x + ap.y * ap.y;
    }
    float t = (ap.x * ab.x + ap.y * ab.y) / ab_len_sq;
    if (t < 0.0f)
        t = 0.0f;
    else if (t > 1.0f)
        t = 1.0f;
    const glm::vec2 foot = a + ab * t;
    const glm::vec2 diff = p - foot;
    return diff.x * diff.x + diff.y * diff.y;
}

// Winding number for point p against polygon verts. Non-zero = inside,
// zero = outside. Standard implementation — for each edge, if it
// crosses the horizontal ray from p going in +X direction, add +1 for
// upward crossing (edge goes -Z to +Z) and -1 for downward. Robust to
// concavity, self-intersections, and edge coincidence with p (returns
// 0 for on-edge; the caller treats "inside" as winding != 0 and edges
// as separately-handled via distance).
inline int polygonWindingNumber(const glm::vec2& p, const std::vector<glm::vec2>& verts)
{
    int wn = 0;
    const int n = static_cast<int>(verts.size());
    for (int i = 0; i < n; ++i)
    {
        const glm::vec2& a = verts[i];
        const glm::vec2& b = verts[(i + 1) % n];
        if (a.y <= p.y)
        {
            if (b.y > p.y)
            {
                // Upward crossing candidate; check if edge is left of p.
                const float cross = (b.x - a.x) * (p.y - a.y) - (p.x - a.x) * (b.y - a.y);
                if (cross > 0.0f)
                    ++wn;
            }
        }
        else
        {
            if (b.y <= p.y)
            {
                // Downward crossing candidate.
                const float cross = (b.x - a.x) * (p.y - a.y) - (p.x - a.x) * (b.y - a.y);
                if (cross < 0.0f)
                    --wn;
            }
        }
    }
    return wn;
}

// Nearest-edge distance + edge index. Populated even for interior
// points (they're distanced to the closest edge). Signed distance is
// (distance) for outside points and (-distance) for inside points; the
// weight function interprets the sign to gate whether to apply pad.
struct PolygonEdgeQuery
{
    float distance; // unsigned distance to the nearest edge
    int edge_index; // index of the nearest edge (edge i = verts[i]->verts[i+1])
    bool inside;    // true if point is inside polygon
};

inline PolygonEdgeQuery polygonNearestEdge(const glm::vec2& p, const std::vector<glm::vec2>& verts)
{
    PolygonEdgeQuery q{};
    q.distance = std::numeric_limits<float>::max();
    q.edge_index = 0;
    const int n = static_cast<int>(verts.size());
    for (int i = 0; i < n; ++i)
    {
        const float d_sq = pointSegmentDistanceSq(p, verts[i], verts[(i + 1) % n]);
        if (d_sq < q.distance)
        {
            q.distance = d_sq;
            q.edge_index = i;
        }
    }
    q.distance = std::sqrt(q.distance);
    q.inside = (polygonWindingNumber(p, verts) != 0);
    return q;
}

// Modifier weight for polygon modes: 1.0 inside, falls to 0.0 over the
// nearest edge's blend_pad outside. Falls-off computation ignores
// polygon interior distance (weight is always 1 inside the polygon,
// even close to an edge).
inline float polygonModifierWeight(const TerrainModifier& m, float x, float z)
{
    // AABB rejection: if the point is outside the polygon's AABB
    // plus the maximum blend pad, the modifier definitely does not
    // apply. Avoids polygon SDF for the vast majority of terrain
    // vertices that are nowhere near the water.
    const float max_pad = m.polygon_max_blend_pad;
    if (x < m.polygon_aabb_min_x - max_pad || x > m.polygon_aabb_max_x + max_pad ||
        z < m.polygon_aabb_min_z - max_pad || z > m.polygon_aabb_max_z + max_pad)
    {
        return 0.0f;
    }
    const glm::vec2 p(x, z);
    const PolygonEdgeQuery q = polygonNearestEdge(p, m.polygon_vertices_xz);
    if (q.inside)
        return 1.0f;
    // Outside — look up the nearest edge's blend pad.
    float pad = m.blend_pad;
    if (!m.polygon_edge_blend_pads.empty())
    {
        const float per_edge = m.polygon_edge_blend_pads[q.edge_index];
        if (per_edge >= 0.0f)
            pad = per_edge;
    }
    if (pad <= 0.0f)
        return 0.0f;
    if (q.distance >= pad)
        return 0.0f;
    return 1.0f - smoothstep01(q.distance / pad);
}

// Signed distance from (x, z) to the modifier's rect, in meters.
// Negative inside the rect, 0 on the boundary, positive outside.
// Computed as the max of per-axis signed distances (Chebyshev-style
// rect SDF — fine for AABBs with axis-aligned blending).
inline float rectSignedDistance(const TerrainModifier& m, float x, float z)
{
    const float dx = std::abs(x - m.center_xz.x) - m.half_extents_xz.x;
    const float dz = std::abs(z - m.center_xz.y) - m.half_extents_xz.y;
    return std::max(dx, dz);
}

// Pick the blend_pad that applies for an axis when the point is OUTSIDE
// on that axis. side_sign tells which side: <0 = negative-axis side,
// >0 = positive-axis side.
inline float blendPadForSide(float per_side, float fallback)
{
    return (per_side < 0.0f) ? fallback : per_side;
}

// Returns the modifier's influence at (x, z): 1.0 fully inside the
// rect, falls smoothly to 0.0 outside.
//
// Outside one axis only: use that axis's per-side blend_pad.
// Outside both axes (corner zone): use the SOFTER side's blend_pad
// in both axes — so a sharp-edge side stays sharp along its own
// outside zone, but doesn't wrap the corner and amputate the soft
// side's ramp.
inline float modifierWeight(const TerrainModifier& m, float x, float z)
{
    if (m.mode == TerrainModifier::Mode::PolygonFlushAt)
        return polygonModifierWeight(m, x, z);

    const float dx = (x - m.center_xz.x);
    const float dz = (z - m.center_xz.y);
    const float dx_out = std::abs(dx) - m.half_extents_xz.x;
    const float dz_out = std::abs(dz) - m.half_extents_xz.y;
    if (dx_out <= 0.0f && dz_out <= 0.0f)
        return 1.0f;

    const float pad_x = (dx < 0.0f) ? blendPadForSide(m.blend_pad_neg_x, m.blend_pad)
                                    : blendPadForSide(m.blend_pad_pos_x, m.blend_pad);
    const float pad_z = (dz < 0.0f) ? blendPadForSide(m.blend_pad_neg_z, m.blend_pad)
                                    : blendPadForSide(m.blend_pad_pos_z, m.blend_pad);

    auto axisBlend = [](float d_out, float pad)
    {
        if (d_out <= 0.0f)
            return 1.0f;
        if (pad <= 0.0f)
            return 0.0f;
        return 1.0f - smoothstep01(d_out / pad);
    };

    // Corner zone: outside on both axes. Pick the SOFTER side (larger
    // pad); use it for both. The sharp side's "amputation" only
    // applies in its own pure-side zone (not the corner), so a chapel
    // with sharp-back + soft-everywhere-else gets a smooth corner.
    if (dx_out > 0.0f && dz_out > 0.0f)
    {
        const float pad = std::max(pad_x, pad_z);
        // Use 2D distance from corner, scaled by the chosen pad.
        const float d = std::sqrt(dx_out * dx_out + dz_out * dz_out);
        return axisBlend(d, pad);
    }

    // Pure-side zone: outside on exactly one axis.
    const float wx = axisBlend(dx_out, pad_x);
    const float wz = axisBlend(dz_out, pad_z);
    return std::min(wx, wz);
}

} // namespace

void registerTerrainModifier(const TerrainModifier& mod)
{
    // Polygon modes: validate the vertex list and precompute the AABB
    // + max blend pad so the hot path (per-terrain-vertex weight
    // computation) doesn't have to scan the polygon.
    if (mod.mode == TerrainModifier::Mode::PolygonFlushAt)
    {
        const auto& verts = mod.polygon_vertices_xz;
        if (verts.size() < 3)
        {
            std::fprintf(stderr,
                         "[terrain-modifier] '%s' PolygonFlushAt needs >=3 vertices; got %zu — "
                         "skipping registration.\n",
                         mod.debug_name ? mod.debug_name : "(no name)", verts.size());
            return;
        }
        if (!mod.polygon_edge_blend_pads.empty() &&
            mod.polygon_edge_blend_pads.size() != verts.size())
        {
            std::fprintf(stderr,
                         "[terrain-modifier] '%s' PolygonFlushAt edge_blend_pads size %zu != "
                         "vertex count %zu — skipping registration.\n",
                         mod.debug_name ? mod.debug_name : "(no name)",
                         mod.polygon_edge_blend_pads.size(), verts.size());
            return;
        }
        TerrainModifier stored = mod;
        // AABB.
        float mnx = verts[0].x, mxx = verts[0].x;
        float mnz = verts[0].y, mxz = verts[0].y;
        for (const auto& v : verts)
        {
            mnx = std::min(mnx, v.x);
            mxx = std::max(mxx, v.x);
            mnz = std::min(mnz, v.y);
            mxz = std::max(mxz, v.y);
        }
        stored.polygon_aabb_min_x = mnx;
        stored.polygon_aabb_max_x = mxx;
        stored.polygon_aabb_min_z = mnz;
        stored.polygon_aabb_max_z = mxz;
        // Max blend pad: max of scalar blend_pad and any per-edge overrides
        // (skipping negative-sentinel entries that mean "fall back to scalar").
        float max_pad = stored.blend_pad;
        for (float p : stored.polygon_edge_blend_pads)
        {
            if (p >= 0.0f)
                max_pad = std::max(max_pad, p);
        }
        stored.polygon_max_blend_pad = max_pad;
        sModifiers.push_back(std::move(stored));
        return;
    }
    sModifiers.push_back(mod);
}

void clearTerrainModifiers()
{
    sModifiers.clear();
}

int terrainModifierCount()
{
    return static_cast<int>(sModifiers.size());
}

const TerrainModifier& terrainModifierAt(int idx)
{
    return sModifiers[idx];
}

namespace
{
// Resolve the target Y a modifier wants to push toward at (world_x,
// world_z), regardless of weight. Mirrors the switch in
// applyTerrainModifiers (Hole short-circuits at the call site).
float modifierTargetY(const TerrainModifier& m, float world_x, float world_z, float current_y)
{
    switch (m.mode)
    {
    case TerrainModifier::Mode::FlushAt:
    case TerrainModifier::Mode::DepressTo:
    case TerrainModifier::Mode::PolygonFlushAt:
        return m.value;
    case TerrainModifier::Mode::AddDelta:
        return current_y + m.value;
    case TerrainModifier::Mode::FlushSlope:
    {
        const float axis_pos = (m.slope_axis == 0) ? world_x : world_z;
        const float axis_center = (m.slope_axis == 0) ? m.center_xz.x : m.center_xz.y;
        const float axis_half = (m.slope_axis == 0) ? m.half_extents_xz.x : m.half_extents_xz.y;
        float t = (axis_pos - (axis_center - axis_half)) / (2.0f * axis_half);
        if (t < 0.0f)
            t = 0.0f;
        else if (t > 1.0f)
            t = 1.0f;
        return m.value * (1.0f - t) + m.value_far * t;
    }
    case TerrainModifier::Mode::Hole:
        break; // Hole never affects Y; fall through to default return
    }
    return current_y;
}

// Dump one modifier's header line + (when it contributes) its target
// + before/after Y transition. Returns the new Y after this modifier.
float dumpOneModifier(const TerrainModifier& m, float world_x, float world_z, float y)
{
    const float w = modifierWeight(m, world_x, world_z);
    if (m.mode == TerrainModifier::Mode::PolygonFlushAt)
    {
        const glm::vec2 p(world_x, world_z);
        const PolygonEdgeQuery q = polygonNearestEdge(p, m.polygon_vertices_xz);
        std::fprintf(stderr,
                     "  '%s' mode=Polygon  inside=%d nearest_edge=%d dist=%.3f  weight=%.4f\n",
                     m.debug_name ? m.debug_name : "(no name)", q.inside ? 1 : 0, q.edge_index,
                     q.distance, w);
    }
    else
    {
        const float dx = (world_x - m.center_xz.x);
        const float dz = (world_z - m.center_xz.y);
        const float dx_out = std::abs(dx) - m.half_extents_xz.x;
        const float dz_out = std::abs(dz) - m.half_extents_xz.y;
        const float pad_x = (dx < 0.0f) ? blendPadForSide(m.blend_pad_neg_x, m.blend_pad)
                                        : blendPadForSide(m.blend_pad_pos_x, m.blend_pad);
        const float pad_z = (dz < 0.0f) ? blendPadForSide(m.blend_pad_neg_z, m.blend_pad)
                                        : blendPadForSide(m.blend_pad_pos_z, m.blend_pad);
        std::fprintf(
            stderr, "  '%s' mode=%d  dx_out=%.3f dz_out=%.3f  pad_x=%.2f pad_z=%.2f  weight=%.4f\n",
            m.debug_name ? m.debug_name : "(no name)", static_cast<int>(m.mode), dx_out, dz_out,
            pad_x, pad_z, w);
    }
    if (w <= 0.0f || m.mode == TerrainModifier::Mode::Hole)
        return y;
    const float target = modifierTargetY(m, world_x, world_z, y);
    const float new_y = y * (1.0f - w) + target * w;
    std::fprintf(stderr, "      target=%.2f  y: %.2f -> %.2f\n", target, y, new_y);
    return new_y;
}
} // namespace

// Diagnostic: dump per-modifier weight + contribution for a single
// XZ. Call this from probe sites; do NOT call per-vertex (would
// flood stderr).
void debugDumpModifierStack(float world_x, float world_z, float base_y, const char* label)
{
    std::fprintf(stderr, "[modstack] (%.2f,%.2f) base_y=%.2f  | %s\n", world_x, world_z, base_y,
                 label);
    float y = base_y;
    for (const auto& m : sModifiers)
        y = dumpOneModifier(m, world_x, world_z, y);
    std::fprintf(stderr, "  FINAL: y=%.2f\n", y);
}

namespace
{
// True if the modifier applies to the given region. Modifiers with
// region_name=nullptr apply to any region (today's behavior); modifiers
// with a non-null region only apply to that named region. Caller
// passing region_name=nullptr means "skip the filter, all modifiers
// eligible" — used by legacy code paths that don't yet pass a region.
bool modifierMatchesRegion(const TerrainModifier& m, const char* query_region)
{
    if (query_region == nullptr)
        return true; // caller opted out of filtering
    if (m.region_name == nullptr)
        return true; // modifier applies to any region
    return std::strcmp(m.region_name, query_region) == 0;
}
} // namespace

float applyTerrainModifiers(const char* region_name, float world_x, float world_z, float base_y)
{
    float y = base_y;
    // Stack modifiers in registration order. Each modifier's target Y
    // is resolved via modifierTargetY (one source of truth — debug
    // path uses the same helper) and blended with the current Y by
    // the modifier's weight. Skip modifiers whose region doesn't
    // match the caller's region; Hole skips per-vertex Y entirely
    // (operates at quad-removal level).
    for (const auto& m : sModifiers)
    {
        if (!modifierMatchesRegion(m, region_name))
            continue;
        if (m.mode == TerrainModifier::Mode::Hole)
            continue;
        const float w = modifierWeight(m, world_x, world_z);
        if (w <= 0.0f)
            continue;
        const float target = modifierTargetY(m, world_x, world_z, y);
        y = y * (1.0f - w) + target * w;
    }
    return y;
}

bool insideTerrainHole(const char* region_name, float world_x, float world_z)
{
    for (const auto& m : sModifiers)
    {
        if (m.mode != TerrainModifier::Mode::Hole)
            continue;
        if (!modifierMatchesRegion(m, region_name))
            continue;
        if (rectSignedDistance(m, world_x, world_z) <= 0.0f)
            return true;
    }
    return false;
}

} // namespace engine::world
