#include "world/TerrainModifiers.h"

#include <algorithm>
#include <cmath>

namespace engine::world
{

namespace
{
std::vector<TerrainModifier> sModifiers;

// Hermite smoothstep — 3t^2 - 2t^3. Returns 0 at t<=0, 1 at t>=1.
inline float smoothstep01(float t)
{
    if (t <= 0.0f) return 0.0f;
    if (t >= 1.0f) return 1.0f;
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

// Returns the modifier's influence at (x, z): 1.0 fully inside the
// rect, falls smoothly to 0.0 over blend_pad meters outside, 0
// beyond. blend_pad=0 → hard step (1 inside, 0 outside).
inline float modifierWeight(const TerrainModifier& m, float x, float z)
{
    const float d = rectSignedDistance(m, x, z);
    if (d <= 0.0f) return 1.0f;
    if (m.blend_pad <= 0.0f) return 0.0f;
    return 1.0f - smoothstep01(d / m.blend_pad);
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
        if (w <= 0.0f) continue;
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
            if (t < 0.0f) t = 0.0f;
            else if (t > 1.0f) t = 1.0f;
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
        if (m.mode != TerrainModifier::Mode::Hole) continue;
        if (rectSignedDistance(m, world_x, world_z) <= 0.0f) return true;
    }
    return false;
}

} // namespace engine::world
