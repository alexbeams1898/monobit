#pragma once

#include <glm/vec2.hpp>

#include <vector>

namespace engine::world
{

// A terrain modifier alters the terrain Y inside an XZ rect, with an
// optional soft-edge falloff for smooth blending into surrounding
// natural terrain.
//
// The terrain mesh builder asks the registry per-vertex what the
// final Y should be, applying all registered modifiers stacked on
// top of the base heightmap Y. Same Y feeds render mesh + physics
// trimesh — one source, no drift.
//
// Real-physics doctrine: terrain shape is runtime data, not a baked
// PNG. Any system that introduces walkable architecture / pits /
// plateaus / paths registers its modifier; terrain meets it
// automatically.
struct TerrainModifier
{
    enum class Mode : unsigned char
    {
        // Y = `value` inside the rect (terrain becomes flat at the
        // given elevation). Use for chapel plateaus, roads, raised
        // platforms, anything where terrain MEETS authored geometry
        // flush at a constant elevation.
        FlushAt,
        // Y interpolates linearly along an axis between `value` (at
        // -axis edge of rect) and `value_far` (at +axis edge).
        // `slope_axis` selects axis: 0=X, 2=Z. Use for terrain that
        // FOLLOWS a sloped architectural feature (descent tunnel
        // ceiling, ramp, river spine).
        FlushSlope,
        // Y = `value` inside the rect (terrain drops to a pit floor).
        // Use for caves, wells, depressions, dug-out areas.
        DepressTo,
        // Y += `value` inside the rect (heightmap shape preserved,
        // shifted up or down). Use for mounds, divots, gradual
        // bias.
        AddDelta,
        // Drop terrain quads inside the rect entirely (no triangles
        // emitted). Use for true vertical holes the player can fall
        // through — stair shafts, well openings, cave mouths. The
        // vertex grid can't express a small flat-bottomed hole
        // (bilinear interpolation between rim vertices fills it in);
        // skipping the quads removes them from both the render mesh
        // AND the physics trimesh. blend_pad / value / value_far are
        // ignored for this mode; a quad is "in" or "out" by whether
        // its centroid falls inside the rect.
        Hole,
    };

    glm::vec2 center_xz{0.0f, 0.0f};
    glm::vec2 half_extents_xz{0.0f, 0.0f};
    Mode mode = Mode::FlushAt;
    float value = 0.0f;
    // Used by FlushSlope only — Y at the +axis edge of the rect.
    // `value` is Y at the -axis edge. slope_axis picks the axis.
    float value_far = 0.0f;
    // 0 = X axis, 2 = Z axis (matches glm convention). Only used
    // when mode == FlushSlope. Defaults to Z (most common: descent
    // along chapel-local Y / world -Z).
    int slope_axis = 2;
    // Outside the rect, modifier strength falls smoothly to zero
    // over the blend_pad on that side. 0 = sharp edge (sheer cliff
    // at the pit/plateau boundary). Positive values produce a smooth
    // ramp that prevents the rim-cliff artifacts that cause Jolt
    // CharacterVirtual to snag.
    //
    // Default value applied to all four sides. Use the per-side
    // overrides below when a side needs different behavior — e.g.
    // a chapel plateau with a tunnel exiting the back wall wants
    // a soft ramp on the front/sides but a SHARP edge on the back
    // so the soft ramp doesn't depress terrain into the tunnel.
    float blend_pad = 0.0f;
    // Per-side blend_pad overrides. -1.0f = use the default
    // blend_pad above. Lets a single modifier express asymmetric
    // transitions to surrounding terrain without spawning multiple
    // overlapping modifiers / counter-modifiers.
    float blend_pad_neg_x = -1.0f;
    float blend_pad_pos_x = -1.0f;
    float blend_pad_neg_z = -1.0f;
    float blend_pad_pos_z = -1.0f;
    // Static-storage string literal for the F1 modifier overlay.
    // nullptr = no label drawn.
    const char* debug_name = nullptr;
    // Static-storage region name (matches TerrainRegion::name). Modifier
    // only applies when the mesh builder queries this region. nullptr =
    // no region filter, modifier applies to whichever region's XZ AABB
    // contains the query (today's behavior — backward compatible with
    // existing callers that register modifiers without a region tag).
    const char* region_name = nullptr;
};

// Register a modifier. Called by world-setup code (e.g. chapel
// initialization). Order matters only for overlapping modifiers
// (later-registered overrides earlier, weighted by blend factor).
void registerTerrainModifier(const TerrainModifier& mod);
void clearTerrainModifiers();

// Number of currently-registered modifiers (for the F1 debug overlay).
int terrainModifierCount();
const TerrainModifier& terrainModifierAt(int idx);

// Per-vertex query used by the terrain mesh builder. Given a base Y
// (from the heightmap) at world XZ, returns the modified Y after
// applying all registered modifiers with their blend pads.
//
// `region_name` is the calling region's name (matches
// TerrainRegion::name). Modifiers whose region_name is set apply
// only when their region matches; modifiers with region_name=nullptr
// apply to any region. Pass nullptr to opt out of filtering (skips
// the check; all modifiers eligible) — matches pre-multi-region
// behavior for callers that don't care.
//
// If no modifier covers (x, z), returns base_y unchanged.
float applyTerrainModifiers(const char* region_name,
                            float world_x, float world_z, float base_y);

// Per-quad query used by the terrain mesh builder. Returns true if
// the XZ point falls inside any registered Hole-mode modifier's
// rect AND that modifier matches the given region (per same rules
// as applyTerrainModifiers). Mesh builder skips quads whose centroid
// returns true here — those triangles never enter the render or
// physics mesh.
bool insideTerrainHole(const char* region_name, float world_x, float world_z);

// Diagnostic: dump per-modifier weight + contribution at this XZ to
// stderr. Use sparingly (one-shot probes only, never per-vertex).
void debugDumpModifierStack(float world_x, float world_z, float base_y, const char* label);

} // namespace engine::world
