#pragma once

#include <glm/vec2.hpp>

#include <vector>

namespace engine::world
{

// VerticalProfile: per-XZ Y-range slot the structure occupies inside
// its footprint. Drives cuts_wall, cuts_ceiling, cuts_rim consumers
// — the cavern surface drops ONLY quads whose Y falls inside this
// slot at that XZ, instead of dropping every quad inside the 2D
// footprint rect.
//
// Grid is NxM cells over the footprint's XZ rect. nx columns along
// X, nz rows along Z. cells[iz * nx + ix] = (y_min, y_max). Empty
// slot at (ix, iz) is represented by y_min > y_max.
//
// Resolution is per-footprint:
//   - 1x1: constant slot (flat-roofed chamber, low wall).
//   - 1xN or Nx1: linear-along-one-axis (sloped corridor).
//   - NxM: general (irregular foundation; round tower).
//
// Bilinear sample inside the grid; outside the footprint rect, slot
// is empty (consumers fall back to "no cut at all" — wall stays).
struct VerticalProfileCell
{
    float y_min = 1.0f; // sentinel: y_min > y_max means "no slot"
    float y_max = 0.0f;
};
struct VerticalProfile
{
    int nx = 0;
    int nz = 0;
    std::vector<VerticalProfileCell> cells; // size = nx * nz
};

// Sample the slot at world (x, z) by bilinear interpolation of the
// footprint's profile grid. Returns true and writes (out_y_min,
// out_y_max) when the XZ is inside the footprint AND the sampled
// slot is non-empty. Returns false otherwise (no cut at this XZ).
struct StructureFootprint;
bool sampleStructureSlot(const StructureFootprint& f, float world_x, float world_z,
                         float& out_y_min, float& out_y_max);

// A structure footprint declares an XZ rect where a built structure
// (chapel, descent corridor, future castle, etc.) occupies space —
// and which terrain surfaces must yield to make room for it.
//
// One footprint, many consumers. Every terrain surface that needs to
// know "is there a structure here?" reads from the same list:
//
//   * Physics trimesh — quads dropped where cuts_floor is set
//   * Floor mesh (render) — quads dropped, fragment-shader discard
//   * Disc-rim heightmap — rim stays at floor Y inside cuts_rim rects
//   * Cavern ceiling mesh — quads dropped inside cuts_ceiling rects
//   * Cavern lateral walls — quads dropped inside cuts_wall rects
//
// Per-surface flags so a structure can poke through one surface but
// not others (low wall: cuts_floor only; descent corridor: cuts_floor
// in selva region, cuts_rim+cuts_ceiling in limbo region).
//
// region_name scopes the footprint to a terrain region. The descent
// corridor occupies BOTH selva_inner (where it enters at the chapel
// back wall) AND limbo (where it exits at the rim/ceiling) — that's
// TWO footprints, one per region, each with the right `cuts_*` flags
// for the surfaces it actually pierces in that region.
//
// See games/selva-oscura/docs/design/DEV_PILLARS.md#8.
struct StructureFootprint
{
    glm::vec2 center_xz{0.0f, 0.0f};
    glm::vec2 half_extents_xz{0.0f, 0.0f};

    // Region this footprint applies to. nullptr means "all regions"
    // (legacy / global). Multi-region structures register one
    // footprint per region they touch.
    const char* region_name = nullptr;

    // Per-surface cut flags. Default: only the floor — matches the
    // legacy chapel-on-selva behavior. A structure that ALSO breaches
    // a cavern ceiling sets cuts_ceiling, etc.
    bool cuts_floor = true;    // physics trimesh + render floor mesh + shader discard
    bool cuts_rim = false;     // disc-shape rim heightmap meets structure top inside
    bool cuts_ceiling = false; // cavern ceiling mesh skips quads inside structure Y-slot
    bool cuts_wall = false;    // cavern lateral wall mesh skips quads inside structure Y-slot

    // Vertical profile: 3D slot through cuts_rim / cuts_ceiling /
    // cuts_wall surfaces. Empty profile (nx==0 || nz==0) falls back
    // to "drop everything inside the rect" — the legacy behavior, fine
    // for cuts_floor-only structures that don't have a vertical
    // extent.
    VerticalProfile vertical_profile;

    const char* debug_name = nullptr;
};

// Register a footprint. The implementation wires the appropriate
// engine-side hooks (physics-side Hole modifier when cuts_floor is
// set, listing for render consumers regardless).
//
// MUST be called BEFORE initTerrain() so all surfaces see it at
// mesh-build time.
void registerStructureFootprint(const StructureFootprint& f);

// Clear all registered footprints (used by scene transitions).
void clearStructureFootprints();

// Count + indexed accessor for render consumers that walk the list.
int structureFootprintCount();
const StructureFootprint& structureFootprintAt(int idx);

// Predicate: is the XZ point inside any footprint that cuts the
// named surface in the given region? Surface options are the
// `cuts_*` flag names. Pass region_name=nullptr to skip the region
// filter (any region matches).
enum class SurfaceCut
{
    Floor,
    Rim,
    Ceiling,
    Wall,
};
bool isInsideStructureFootprint(const char* region_name, float world_x, float world_z,
                                SurfaceCut surface);

// 3D variant: is (world_x, world_y, world_z) inside any cuts_<surface>
// footprint's vertical-profile slot in `region_name`? Wall + ceiling
// consumers use this to drop ONLY quads the structure actually
// occupies in 3D, not every quad inside the 2D rect. When a matching
// footprint has no vertical_profile, falls back to the 2D predicate
// (preserves legacy "drop everything inside the rect" behavior for
// structures without a slot).
bool isInsideStructureSlot(const char* region_name, float world_x, float world_y, float world_z,
                           SurfaceCut surface);

// Sub-quad clipping: for a 2D wall/ceiling quad with the given world
// (var_min, var_max, y_min, y_max) extent on the surface plane,
// compute up to 4 sub-quad rectangles describing the KEPT region
// (quad minus all matching slots). `var_axis` is 0 for X-axis walls
// (variable is X), 2 for Z-axis walls. `const_pos` is the fixed
// coord on that wall (the X or Z of the plane). `surface` selects
// cuts_wall vs cuts_ceiling vs cuts_rim.
//
// Returns the number of sub-quads written to `out` (0-4). Each
// sub-quad is a 4-tuple (var_min, var_max, y_min, y_max) in the
// same units. Caller emits one quad per sub-rect using the wall's
// inward normal + winding.
//
// Greedy clip: for each slot rect that overlaps the input quad,
// subtract it. Slots are sampled at the input quad's centroid (slot
// Y bounds treated as constant across the quad's V extent — fine
// for v1; per-V slot variation handled at sub-quad granularity in
// the next iteration if needed).
struct ClipRect
{
    float var_min;
    float var_max;
    float y_min;
    float y_max;
};

// Bundled input for the clipper. Cuts the parameter count down and
// makes call sites self-documenting.
//
// y_grid_step > 0 enables snap-to-grid (wall consumer uses this:
// snaps slot Y bounds to the wall's quad rows so the cut takes
// whole rows in Y, no mid-quad slivers).
struct ClipQuadQuery
{
    const char* region_name;
    int var_axis;    // 0 = X-constant wall (V=Z); 2 = Z-constant wall (V=X)
    float const_pos; // X or Z of the wall plane
    ClipRect quad;   // input quad bounds in (V, Y)
    SurfaceCut surface;
    float y_grid_origin = 0.0f;
    float y_grid_step = 0.0f; // 0 = no snap
};

int clipQuadAgainstStructureSlots(const ClipQuadQuery& q, ClipRect* out, int out_capacity);

// Serialize the registered structure footprints to a JSON file at
// `out_path`. Consumed by build-time tools (e.g. the Python heightmap
// baker) that need the same per-surface footprint list the C++ runtime
// uses — single source of truth: the registration code runs once, the
// resulting JSON feeds every consumer that can't link the engine.
//
// Schema is versioned (`schema_version`) so future field additions
// don't silently break older tool versions. See games/selva-oscura/
// tools/dump_world.cpp for the canonical writer call site.
//
// Returns true on success; false on file-write failure.
bool serializeStructureRegistryJson(const char* out_path);

} // namespace engine::world
