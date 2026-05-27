#include "world/TerrainModifiers.h"

#include <algorithm>
#include <cmath>
#include <cstdio>

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

// Diagnostic: dump per-modifier weight + contribution for a single
// XZ. Call this from probe sites; do NOT call per-vertex (would
// flood stderr).
void debugDumpModifierStack(float world_x, float world_z, float base_y, const char* label)
{
    std::fprintf(stderr, "[modstack] (%.2f,%.2f) base_y=%.2f  | %s\n", world_x, world_z, base_y,
                 label);
    float y = base_y;
    for (const auto& m : sModifiers)
    {
        const float dx = (world_x - m.center_xz.x);
        const float dz = (world_z - m.center_xz.y);
        const float dx_out = std::abs(dx) - m.half_extents_xz.x;
        const float dz_out = std::abs(dz) - m.half_extents_xz.y;
        const float w = modifierWeight(m, world_x, world_z);
        const float pad_x = (dx < 0.0f) ? blendPadForSide(m.blend_pad_neg_x, m.blend_pad)
                                        : blendPadForSide(m.blend_pad_pos_x, m.blend_pad);
        const float pad_z = (dz < 0.0f) ? blendPadForSide(m.blend_pad_neg_z, m.blend_pad)
                                        : blendPadForSide(m.blend_pad_pos_z, m.blend_pad);
        std::fprintf(
            stderr, "  '%s' mode=%d  dx_out=%.3f dz_out=%.3f  pad_x=%.2f pad_z=%.2f  weight=%.4f\n",
            m.debug_name ? m.debug_name : "(no name)", static_cast<int>(m.mode), dx_out, dz_out,
            pad_x, pad_z, w);
        if (w <= 0.0f)
            continue;
        if (m.mode == TerrainModifier::Mode::Hole)
            continue;
        float target = y;
        if (m.mode == TerrainModifier::Mode::FlushAt)
            target = m.value;
        else if (m.mode == TerrainModifier::Mode::DepressTo)
            target = m.value;
        else if (m.mode == TerrainModifier::Mode::AddDelta)
            target = y + m.value;
        else if (m.mode == TerrainModifier::Mode::FlushSlope)
        {
            const float axis_pos = (m.slope_axis == 0) ? world_x : world_z;
            const float axis_center = (m.slope_axis == 0) ? m.center_xz.x : m.center_xz.y;
            const float axis_half = (m.slope_axis == 0) ? m.half_extents_xz.x : m.half_extents_xz.y;
            float t = (axis_pos - (axis_center - axis_half)) / (2.0f * axis_half);
            if (t < 0.0f)
                t = 0.0f;
            else if (t > 1.0f)
                t = 1.0f;
            target = m.value * (1.0f - t) + m.value_far * t;
        }
        const float new_y = y * (1.0f - w) + target * w;
        std::fprintf(stderr, "      target=%.2f  y: %.2f -> %.2f\n", target, y, new_y);
        y = new_y;
    }
    std::fprintf(stderr, "  FINAL: y=%.2f\n", y);
}

float applyTerrainModifiers(float world_x, float world_z, float base_y)
{
    float y = base_y;
    // Stack modifiers in registration order. Each modifier's target Y
    // is computed (FlushAt → value; DepressTo → value; AddDelta →
    // current + value) and blended with the current Y by the
    // modifier's weight at this point.
    for (const auto& m : sModifiers)
    {
        const float w = modifierWeight(m, world_x, world_z);
        if (w <= 0.0f)
            continue;
        float target = y;
        switch (m.mode)
        {
        case TerrainModifier::Mode::FlushAt:
            target = m.value;
            break;
        case TerrainModifier::Mode::FlushSlope:
        {
            // Linear interp from `value` at -axis edge of rect to
            // `value_far` at +axis edge.
            const float axis_pos = (m.slope_axis == 0) ? world_x : world_z;
            const float axis_center = (m.slope_axis == 0) ? m.center_xz.x : m.center_xz.y;
            const float axis_half = (m.slope_axis == 0) ? m.half_extents_xz.x : m.half_extents_xz.y;
            // t = 0 at -axis edge, 1 at +axis edge; clamp outside.
            float t = (axis_pos - (axis_center - axis_half)) / (2.0f * axis_half);
            if (t < 0.0f)
                t = 0.0f;
            else if (t > 1.0f)
                t = 1.0f;
            target = m.value * (1.0f - t) + m.value_far * t;
            break;
        }
        case TerrainModifier::Mode::DepressTo:
            target = m.value;
            break;
        case TerrainModifier::Mode::AddDelta:
            target = y + m.value;
            break;
        case TerrainModifier::Mode::Hole:
            // Hole mode operates at quad-removal level, not vertex Y.
            // Skip this modifier when computing per-vertex Y.
            continue;
        }
        y = y * (1.0f - w) + target * w;
    }
    return y;
}

bool insideTerrainHole(float world_x, float world_z)
{
    for (const auto& m : sModifiers)
    {
        if (m.mode != TerrainModifier::Mode::Hole)
            continue;
        if (rectSignedDistance(m, world_x, world_z) <= 0.0f)
            return true;
    }
    return false;
}

} // namespace engine::world
