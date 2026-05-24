#include "world/Collision.h"

#include "Tunables.h"
#include "WallClock.h"
#include "world/CryptLayout.h"
#include "world/Terrain.h"

#include <glm/geometric.hpp>

#include <cmath>
#include <cstdarg>
#include <cstdio>
#include <limits>
#include <random>
#include <utility>

namespace selva::world
{

namespace
{
CollisionScene sScene;

FILE* sCollisionLog = nullptr;
int sCollisionFrame = 0;

// Caller MUST gate on tun.debug_collision_log before invoking to keep
// the disabled-state cost at zero (no variadic arg evaluation).
void collisionLog(const char* fmt, ...)
{
    if (sCollisionLog == nullptr)
        sCollisionLog = std::fopen("collision-debug.log", "w");
    if (sCollisionLog == nullptr)
        return;
    va_list args;
    va_start(args, fmt);
    std::vfprintf(sCollisionLog, fmt, args);
    va_end(args);
    std::fflush(sCollisionLog);
}

// Selva-oscura hub layout. Authored placement: trees flank the
// gothic walkway on the approach side (spawn -> colle), the
// walkway / plateau / back-of-plateau view-strip stay clear, and
// the rest of the wood gets a sparse scatter biased around the
// colle ridge.
//
// World layout (Z runs into the colle along -Z; matches the
// heightmap from gen_terrain_heightmap.py):
//   Z = 0               : spawn (center of wake-zone)
//   Z = 0 .. -110       : approach walkway (gentle ramp + steep climb)
//   Z = -190..-230      : plateau (the dilettoso monte summit)
//   Z = -230..-290      : back-of-plateau clear view-strip (no scatter)
//   Z < -290            : back wood (sparse scatter resumes)
//   |X| < 4m on walkway -> clear corridor
//   |X| in [4, 14] on walkway -> dense aisle of flanking trees
//   |X| < 35m on plateau -> clear hub
//   outside corridor + plateau + view-strip -> background scatter
//
// Design intent: the player spawns in the wake-zone, walks south up
// the framed corridor of trees, reaches the plateau, and looks
// forward into a CLEAR view (no trees behind the plateau for ~60m).
// That clear view is where Beatrice's threshold-light is strongest;
// it is the canonical "look toward the dawn" beat.
constexpr float kHubBoundaryRadius = 230.0f;
constexpr float kHubMinTreeSpacing = 3.5f;
constexpr unsigned int kHubSeed = 0xDA17EU;

constexpr float kWalkwayHalfWidth = 4.0f;
constexpr float kAisleOuterX = 14.0f;

// Approach side (spawn -> colle):
constexpr float kWalkwayStartZ = -5.0f;
constexpr float kWalkwayEndZ = -110.0f;

// Plateau (hub):
constexpr float kPlateauStartZ = -190.0f;
constexpr float kPlateauEndZ = -230.0f;
constexpr float kPlateauHalfX = 35.0f;

// Clear-view strip behind the plateau. No trees scatter here so the
// view from the plateau toward the light is uninterrupted.
constexpr float kClearViewDepth = 60.0f;
constexpr float kClearViewEndZ = kPlateauEndZ - kClearViewDepth; // -290

constexpr int kAisleTreeCount = 30;      // approach aisle only (was 40 split between two)
constexpr int kBackgroundTreeCount = 80; // scattered through the wider wood

bool tooCloseToExisting(const std::vector<CylinderCollider>& placed, float x, float z, float radius)
{
    for (const auto& c : placed)
    {
        const float dx = x - c.center.x;
        const float dz = z - c.center.z;
        const float min_dist = radius + c.radius + kHubMinTreeSpacing;
        if (dx * dx + dz * dz < min_dist * min_dist)
            return true;
    }
    return false;
}

bool inApproachWalkway(float x, float z)
{
    return z <= kWalkwayStartZ && z >= kWalkwayEndZ && std::abs(x) <= kWalkwayHalfWidth;
}

bool inWalkwayCorridor(float x, float z)
{
    return inApproachWalkway(x, z);
}

bool onPlateau(float x, float z)
{
    return z <= kPlateauStartZ && z >= kPlateauEndZ && std::abs(x) <= kPlateauHalfX;
}

// The clear-view strip immediately behind the plateau. No trees here
// (not even scatter) so the view from the plateau toward the light
// is uninterrupted.
bool inClearViewStrip(float z)
{
    return z < kPlateauEndZ && z >= kClearViewEndZ;
}

// Generic aisle population: lays out trees on either side of a
// walkway corridor between (z_start, z_end). Jitter randomizes the
// X and Z within the aisle band so the line isn't ruler-straight.
void populateAisle(std::mt19937& rng, std::vector<CylinderCollider>& out, float z_start,
                   float z_end, int tree_count_per_side)
{
    std::uniform_real_distribution<float> radius_dist(0.28f, 0.55f);
    std::uniform_real_distribution<float> x_jitter(-2.5f, 2.5f);
    std::uniform_real_distribution<float> z_jitter(-1.5f, 1.5f);
    const float aisle_x_inner = kWalkwayHalfWidth + 1.5f;
    const float aisle_x_outer = kAisleOuterX;
    const float step_z = (z_start - z_end) / static_cast<float>(tree_count_per_side);
    for (int i = 0; i < tree_count_per_side; ++i)
    {
        const float z_base = z_start - static_cast<float>(i) * step_z;
        for (int side = 0; side < 2; ++side)
        {
            const float sign = (side == 0) ? -1.0f : 1.0f;
            std::uniform_real_distribution<float> band(aisle_x_inner, aisle_x_outer);
            const float x = sign * band(rng) + x_jitter(rng);
            const float z = z_base + z_jitter(rng);
            if (inWalkwayCorridor(x, z) || onPlateau(x, z))
                continue;
            const float trunk_r = radius_dist(rng);
            if (tooCloseToExisting(out, x, z, trunk_r))
                continue;
            CylinderCollider c;
            c.center = glm::vec3(x, 0.0f, z);
            c.radius = trunk_r;
            c.half_height = 2.0f;
            out.push_back(c);
        }
    }
}

void populateHubTrees(std::vector<CylinderCollider>& out)
{
    std::mt19937 rng(kHubSeed);
    std::uniform_real_distribution<float> radius_dist(0.28f, 0.55f);

    // Pass 1: dense aisle on the approach (spawn -> colle). The
    // back side of the colle gets NO mirror aisle - the plateau
    // looks out onto a clear view through the kClearViewDepth strip.
    populateAisle(rng, out, kWalkwayStartZ, kWalkwayEndZ, kAisleTreeCount);

    // Pass 2: background scatter through the rest of the wood. Skips
    // anything inside the walkway corridor, on the plateau, or in
    // the clear-view strip behind the plateau.
    {
        std::uniform_real_distribution<float> angle_dist(0.0f, 2.0f * 3.14159265f);
        std::uniform_real_distribution<float> radial_dist(0.0f, 1.0f);
        int placed = 0;
        int attempts = 0;
        while (placed < kBackgroundTreeCount && attempts < 8000)
        {
            ++attempts;
            const float u = radial_dist(rng);
            const float r = std::sqrt(u) * kHubBoundaryRadius;
            const float a = angle_dist(rng);
            // Offset the scatter so it spreads around the colle
            // ridge, not just from world origin. Bias toward the
            // new plateau midpoint (Z=-210) so flank density tracks
            // the colle.
            const float scatter_origin_z = -210.0f;
            const float x = std::cos(a) * r;
            const float z = std::sin(a) * r + scatter_origin_z;
            if (inWalkwayCorridor(x, z) || onPlateau(x, z) || inClearViewStrip(z))
                continue;
            // Keep breathing room around spawn so the wake-zone reads
            // as "found yourself in a wood" without a tree on top of
            // the player.
            if (x * x + z * z < 9.0f)
                continue;
            const float trunk_r = radius_dist(rng);
            if (tooCloseToExisting(out, x, z, trunk_r))
                continue;
            CylinderCollider c;
            c.center = glm::vec3(x, 0.0f, z);
            c.radius = trunk_r;
            c.half_height = 2.0f;
            out.push_back(c);
            ++placed;
        }
    }
}

// Crypt collision. The chapel is a static box of stone with a single
// door gap in the front wall. Authored as five box colliders: front
// wall (split into left and right of the door gap), back wall (with
// apse), and the two long side walls.
//
// y_base is sampled from the terrain at the chapel center so the
// camera raycast sees walls at their actual world Y. The terrain pad
// in gen_terrain_heightmap.py keeps the foundation flat under the
// chapel footprint, so a single sample is accurate for all 5 walls.
void populateCryptColliders(std::vector<BoxCollider>& out)
{
    using namespace crypt_layout;
    constexpr float kHalfThickness = kWallThickness * 0.5f;
    constexpr float kWallHalfHeightY = kWallHeight * 0.5f;
    const float y_base = kChapelGroundY;

    auto pushWall = [&](const char* tag, glm::vec2 center, glm::vec2 half_extents)
    {
        BoxCollider b;
        b.center = center;
        b.half_extents = half_extents;
        b.y_base = y_base;
        b.half_height_y = kWallHalfHeightY;
        b.name = tag;
        out.push_back(b);
    };

    const float kFrontSlabZ = kCryptZ + kHalfLength - kHalfThickness;
    {
        const float left_outer = -kHalfWidth;
        const float left_inner = -kDoorHalfWidth;
        pushWall("wall_front_left",
                 glm::vec2(kCryptX + (left_outer + left_inner) * 0.5f, kFrontSlabZ),
                 glm::vec2((left_inner - left_outer) * 0.5f, kHalfThickness));
    }
    {
        const float right_inner = kDoorHalfWidth;
        const float right_outer = kHalfWidth;
        pushWall("wall_front_right",
                 glm::vec2(kCryptX + (right_inner + right_outer) * 0.5f, kFrontSlabZ),
                 glm::vec2((right_outer - right_inner) * 0.5f, kHalfThickness));
    }
    // Back wall: two slabs flanking the stair tunnel gap.
    {
        const float back_y = kCryptZ - kHalfLength + kHalfThickness;
        const float left_outer = -kHalfWidth;
        const float left_inner = -kSingleFlightHalfWidth;
        pushWall("wall_back_left",
                 glm::vec2(kCryptX + (left_outer + left_inner) * 0.5f, back_y),
                 glm::vec2((left_inner - left_outer) * 0.5f, kHalfThickness));
        const float right_inner = kSingleFlightHalfWidth;
        const float right_outer = kHalfWidth;
        pushWall("wall_back_right",
                 glm::vec2(kCryptX + (right_inner + right_outer) * 0.5f, back_y),
                 glm::vec2((right_outer - right_inner) * 0.5f, kHalfThickness));
    }
    pushWall("wall_side_left",
             glm::vec2(kCryptX - kHalfWidth + kHalfThickness, kCryptZ),
             glm::vec2(kHalfThickness, kHalfLength));
    pushWall("wall_side_right",
             glm::vec2(kCryptX + kHalfWidth - kHalfThickness, kCryptZ),
             glm::vec2(kHalfThickness, kHalfLength));

    {
        BoxCollider header;
        header.center = glm::vec2(kCryptX, kFrontSlabZ);
        header.half_extents = glm::vec2(kDoorHalfWidth, kHalfThickness);
        header.y_base = y_base + kDoorHeight;
        header.half_height_y = (kWallHeight - kDoorHeight) * 0.5f;
        header.camera_only = true;
        header.name = "door_header";
        out.push_back(header);
    }

    {
        BoxCollider roof;
        roof.center = glm::vec2(kCryptX, kCryptZ - kApseRadius * 0.5f);
        roof.half_extents = glm::vec2(kHalfWidth, kHalfLength + kApseRadius * 0.5f);
        roof.y_base = y_base + kWallHeight;
        roof.half_height_y = 0.5f;
        roof.camera_only = true;
        roof.name = "roof";
        out.push_back(roof);
    }
}

// Apse rear-curve: a half-ring of small cylinders wrapping the
// semicircular apse from outside. Spans 180° centered on the rear
// face's apex (-Z direction). Player can't pass through the curve.
//
// Cylinder Y is sampled from the terrain at the chapel center so the
// vertical extent lines up with the visible apse stone (and the camera
// raycast sees them at the right altitude). Without this they sit at
// world Y=0 while the chapel itself stands on the colle plateau, and
// the camera ray passes harmlessly above them.
void populateCryptApseCylinders(std::vector<CylinderCollider>& out)
{
    using namespace crypt_layout;
    constexpr int kArcSegments = 32; // ~5.6° between cylinder centers — dense enough that only the tunnel-X gap is open in the apse-shell colliders
    // Radius chosen so adjacent cylinders overlap: arc spacing at
    // kArcRadius is ~0.32m between centers, so 0.20m radius (0.40m
    // diameter) gives ~0.08m of overlap. Without overlap a thin
    // camera ray can slip between the cylinders and clip into the
    // chapel interior; player push-out had no such issue because
    // the body radius (~0.35m) bridges the gap on its own.
    constexpr float kCylRadius = 0.20f;
    // Centers sit one cyl-radius INWARD of the apse surface so their
    // outer edge aligns with the visible stone curve.
    constexpr float kArcRadius = kApseRadius - kCylRadius;
    const float arc_cx = kCryptX;
    const float arc_cz = kCryptZ - kHalfLength;
    const float arc_cy = kChapelGroundY;
    for (int i = 0; i <= kArcSegments; ++i)
    {
        const float t = static_cast<float>(i) / static_cast<float>(kArcSegments);
        // Half-circle bulging toward -Z. t=0 at +X corner, t=0.5 at
        // the back apex, t=1 at -X corner.
        const float ang = -3.14159265f * t;
        const float x = arc_cx + std::cos(ang) * kArcRadius;
        const float z = arc_cz + std::sin(ang) * kArcRadius;
        // Skip cylinders that fall inside the stair tunnel's X span
        // (|X| <= kSingleFlightHalfWidth). The descent landing extends
        // past the chapel back wall through the apse footprint; an
        // apse cylinder here would push the player off the landing
        // and prevent them from walking the descent path.
        if (std::abs(x - arc_cx) <= kSingleFlightHalfWidth)
            continue;
        CylinderCollider c;
        c.center = glm::vec3(x, arc_cy, z);
        c.radius = kCylRadius;
        c.half_height = kWallHeight * 0.5f;
        c.collision_only = true;
        c.name = "apse_arc";
        out.push_back(c);
    }
}

// Descent stair colliders: two flanking upper-flight ramps + landing +
// 4 corridor ramp segments + 3 corridor landings (with alcove walls) +
// Acheron stub platform. Mirrors gen_crypt_foundation.py's
// build_descent(). Coordinate convention: chapel-local Blender coords
// (X lateral, +Y toward apse, Z up) map to world via:
//   world_x = chapel_x + kCryptX                    (kCryptX = 0)
//   world_z = kCryptZ - chapel_y                    (Blender +Y -> world -Z)
//   world_y = (ground_y - kPlinthHeight + 0.01) + chapel_z
// Walls + ceilings get camera_only=true so they don't block the player
// at XZ (the player's Y already handles vertical clearance via the
// ground sample); only the floor/step boxes get walkable_top=true.
void populateCryptDescent(std::vector<BoxCollider>& out)
{
    using namespace crypt_layout;
    constexpr float kZFightOffset = 0.01f;
    const float ground_y = kChapelGroundY;
    const float chapel_floor_world_y = ground_y - kPlinthHeight + kZFightOffset + kPlinthHeight;

    // Helper: chapel-local (x, y, z) extents -> world BoxCollider.
    // Half-extents are passed as chapel-local (hx, hy, hz). Caller
    // sets walkable_top / camera_only / top_slope as appropriate.
    // `tag` becomes the BoxCollider's debug-overlay name.
    auto pushCL = [&](const char* tag, float cx, float cy, float cz, float hx, float hy,
                      float hz, bool walkable, bool camera_only_flag,
                      const glm::vec2& top_slope = glm::vec2(0.0f))
    {
        BoxCollider b;
        b.center = glm::vec2(kCryptX + cx, kCryptZ - cy);
        b.half_extents = glm::vec2(hx, hy);
        b.y_base = (ground_y - kPlinthHeight + kZFightOffset) + cz - 2.0f * hz;
        b.half_height_y = hz;
        b.walkable_top = walkable;
        b.camera_only = camera_only_flag;
        b.top_slope = top_slope;
        b.name = tag;
        out.push_back(b);
    };

    // ---- Chapel-interior floor (door side of stair hole) ----
    // Without this, the player walks off the chapel plinth top and
    // freefalls onto the interior pit terrain (the depression carved
    // into the heightmap to give the stair shaft clearance) until
    // they reach the ramp box. The plinth's TOP is the architectural
    // walking surface, not the pit floor underneath it. Slab spans
    // the full interior width and covers chapel-local Y from the
    // front interior wall up to the stair hole's near edge (Y=0).
    constexpr float kInteriorHalfWidth = kHalfWidth - kWallThickness;
    constexpr float kInteriorHalfLength = kHalfLength - kWallThickness;
    constexpr float kFloorSlabThickness = 0.10f;
    {
        const float front_strip_cy = -kInteriorHalfLength * 0.5f;
        const float front_strip_half_len_y = kInteriorHalfLength * 0.5f;
        pushCL("chapel_floor_front", 0.0f, front_strip_cy,
               kPlinthHeight,
               kInteriorHalfWidth, front_strip_half_len_y,
               kFloorSlabThickness * 0.5f,
               /*walkable*/ true, /*camera_only*/ false);
    }
    // ---- Chapel-interior floor (side strips flanking the stair hole) ----
    // The hole is narrower than the chapel interior (hole half-width
    // = kSingleFlightHalfWidth < kInteriorHalfWidth), so there are
    // solid plinth strips on each side of the hole along its Y range.
    // Without colliders here, the player walking past the hole's near
    // edge along the side falls into the descent shaft.
    {
        const float side_strip_inner_x = kSingleFlightHalfWidth;
        const float side_strip_outer_x = kInteriorHalfWidth;
        const float side_strip_half_w = (side_strip_outer_x - side_strip_inner_x) * 0.5f;
        const float side_strip_cx_mag = side_strip_inner_x + side_strip_half_w;
        const float side_strip_cy = kDescentForwardSign * (kUpperFlightRun * 0.5f);
        const float side_strip_half_len_y = kUpperFlightRun * 0.5f;
        for (float sign : {-1.0f, 1.0f})
        {
            pushCL("chapel_floor_side", sign * side_strip_cx_mag, side_strip_cy,
                   kPlinthHeight,
                   side_strip_half_w, side_strip_half_len_y,
                   kFloorSlabThickness * 0.5f,
                   /*walkable*/ true, /*camera_only*/ false);
        }
    }
    // ---- Chapel-interior floor (apse-side strips flanking back-wall tunnel) ----
    // 0.25m strip between hole far edge and chapel back wall. Two
    // slabs flanking the back-wall tunnel X span; tunnel opening
    // stays clear so the descending player passes UNDER it on the
    // landing.
    {
        const float apse_strip_cy = kDescentForwardSign *
            (kUpperFlightRun + (kInteriorHalfLength - kUpperFlightRun) * 0.5f);
        const float apse_strip_half_len_y = (kInteriorHalfLength - kUpperFlightRun) * 0.5f;
        const float apse_strip_half_w = (kInteriorHalfWidth - kSingleFlightHalfWidth) * 0.5f;
        const float apse_strip_cx_mag = kSingleFlightHalfWidth + apse_strip_half_w;
        if (apse_strip_half_len_y > 0.01f && apse_strip_half_w > 0.01f)
        {
            for (float sign : {-1.0f, 1.0f})
            {
                pushCL("chapel_floor_apse", sign * apse_strip_cx_mag, apse_strip_cy,
                       kPlinthHeight,
                       apse_strip_half_w, apse_strip_half_len_y,
                       kFloorSlabThickness * 0.5f,
                       /*walkable*/ true, /*camera_only*/ false);
            }
        }
    }

    // ---- Single wide upper flight as ONE sloped ramp ----
    // Visible-step mesh over a continuous ramp collider.
    // Per-step box colliders cause every-step snag/stutter and force
    // the player to drop onto each step under gravity; a ramp gives
    // smooth monotonic descent. The visual mesh still shows discrete
    // steps (the player's eye reads them as stairs), and feet may sit
    // slightly above or below individual step edges (tradeoff: smooth
    // locomotion wins over per-step foot accuracy). If we add foot IK
    // later, it solves to the ramp.
    constexpr float kStairSlabThickness = 0.5f;
    // Ramp top must match the LEADING EDGES of visible steps so the
    // player never sinks below a step top. Leading edge of step i is
    // at chapel_y = i*tread, top Z = kPlinthHeight - (i+1)*kStairRise.
    // Line through these points (treating i as continuous):
    //   ramp_top_cz(chapel_y) = (kPlinthHeight - kStairRise)
    //                           - chapel_y * (kStairRise/kStairTread)
    // At chapel_y = kUpperFlightRun*0.5 (midpoint), top_cz_center =
    //   (kPlinthHeight - kStairRise) - kUpperFlightDrop * 0.5
    const float ramp_top_cz_center =
        (kPlinthHeight - kStairRise) - kUpperFlightDrop * 0.5f;
    const float ramp_cy_center =
        kUpperFlightTopChapelLocalY + kDescentForwardSign * kUpperFlightRun * 0.5f;
    // Slope: walking +world_z = walking -chapel_y = BACKWARD up the
    // ramp (cancels descent). With forward_sign=+1, top_slope.y is
    // +rise/run (Y rises as world_z grows = going backward/up).
    const float ramp_slope_world_z =
        (kStairRise / kStairTread) * kDescentForwardSign;
    pushCL("upper_flight_ramp", 0.0f, ramp_cy_center,
           ramp_top_cz_center,
           kSingleFlightHalfWidth,
           kUpperFlightRun * 0.5f,
           kStairSlabThickness * 0.5f,
           /*walkable*/ true, /*camera_only*/ false,
           glm::vec2(0.0f, ramp_slope_world_z));

    // ---- Landing where flights converge ----
    // Landing starts 0.5 tread BEFORE the ramp's nominal end (overlap
    // with step 8 + step 9). The ramp's top is the leading-edges
    // line, which extrapolates 0.16m BELOW the visible step 8/9 top
    // at chapel_Y >= 8*tread; without the landing overlap, the
    // player walking off the ramp at apse-side would drop and snap
    // up onto the landing (a visible "teleport up onto a floor"
    // step that breaks the descent feel).
    const float landing_top_z = kPlinthHeight - kUpperFlightDrop;
    const float landing_overlap = 0.5f * kStairTread;  // half a step
    const float landing_extended_y_near = kUpperFlightTopChapelLocalY +
        kDescentForwardSign * (kUpperFlightRun - landing_overlap);
    const float landing_extended_y_far = kUpperFlightTopChapelLocalY +
        kDescentForwardSign * (kUpperFlightRun + kLandingDepthY);
    const float landing_cy = (landing_extended_y_near + landing_extended_y_far) * 0.5f;
    const float landing_half_len_y =
        std::abs(landing_extended_y_far - landing_extended_y_near) * 0.5f;
    constexpr float kLandingSlabThickness = 0.4f;
    pushCL("landing_top", 0.0f, landing_cy,
           landing_top_z,
           kLandingWidthX * 0.5f,
           landing_half_len_y,
           kLandingSlabThickness * 0.5f,
           /*walkable*/ true, /*camera_only*/ false);

    // ---- Corridor: ramp segments + in-between landings ----
    float cursor_cy = kUpperFlightTopChapelLocalY +
                      kDescentForwardSign * (kUpperFlightRun + kLandingDepthY);
    float cursor_cz = landing_top_z;
    const float corridor_hw = kCorridorWidth * 0.5f;
    const float corridor_slope =
        (kCorridorSegmentDrop / kCorridorSegmentRun) * kDescentForwardSign;
    for (int seg = 0; seg < kCorridorSegmentCount; ++seg)
    {
        const float ramp_cy = cursor_cy + kDescentForwardSign * kCorridorSegmentRun * 0.5f;
        const float ramp_top_z_center = cursor_cz - kCorridorSegmentDrop * 0.5f;
        pushCL("corridor_ramp", 0.0f, ramp_cy,
               ramp_top_z_center,
               corridor_hw,
               kCorridorSegmentRun * 0.5f,
               kStairSlabThickness * 0.5f,
               /*walkable*/ true, /*camera_only*/ false,
               glm::vec2(0.0f, corridor_slope));
        cursor_cy += kDescentForwardSign * kCorridorSegmentRun;
        cursor_cz -= kCorridorSegmentDrop;

        if (seg < kCorridorSegmentCount - 1)
        {
            const float landing_run = kCorridorLandingLength;
            const float landing_cy_seg = cursor_cy + kDescentForwardSign * landing_run * 0.5f;
            pushCL("corridor_landing", 0.0f, landing_cy_seg,
                   cursor_cz,
                   corridor_hw,
                   landing_run * 0.5f,
                   kStairSlabThickness * 0.5f,
                   /*walkable*/ true, /*camera_only*/ false);
            cursor_cy += kDescentForwardSign * landing_run;
        }
    }

    // ---- Acheron stub platform ----
    const float acheron_top_z = cursor_cz;
    const float acheron_cy = cursor_cy + kDescentForwardSign * kAcheronPlatformHalfExtent;
    pushCL("acheron_platform", 0.0f, acheron_cy,
           acheron_top_z,
           kAcheronPlatformHalfExtent,
           kAcheronPlatformHalfExtent,
           kStairSlabThickness * 0.5f,
           /*walkable*/ true, /*camera_only*/ false);

    (void)chapel_floor_world_y; // (referenced indirectly via pushCL math)
}

void populateCryptInteriorFootprint(std::vector<InteriorFootprint>& out)
{
    using namespace crypt_layout;
    // Chapel interior — door side extends 30cm PAST the door's outer
    // face so the foot landing AT the threshold trips concrete (not
    // grass).
    constexpr float kDoorSideExtension = 0.30f;
    InteriorFootprint chapel;
    chapel.center = glm::vec2(kCryptX, kCryptZ + kDoorSideExtension * 0.5f);
    chapel.half_extents = glm::vec2(kHalfWidth, kHalfLength + kDoorSideExtension * 0.5f);
    out.push_back(chapel);

    // Descent footprint — covers the upper-flight stair, landing,
    // corridor, and Acheron stub so the player remains "indoors" for
    // the entire underground descent. Without this, the moment the
    // player passes the chapel back wall the indoors flag drops and
    // groundHeight's override stops firing — terrain (plateau Y
    // ~31.96) wins over the landing collider top (~30.82) and the
    // player is popped UP to plateau level instead of continuing
    // down. X span matches the stair shaft; Z span runs from the
    // chapel back wall all the way to the Acheron stub far edge.
    const float descent_z_near = kCryptZ - kHalfLength + kWallThickness;
    const float descent_z_far_chapel_local =
        kUpperFlightRun + kLandingDepthY +
        kCorridorSegmentCount * kCorridorSegmentRun +
        (kCorridorSegmentCount - 1) * kCorridorLandingLength +
        2.0f * kAcheronPlatformHalfExtent;
    const float descent_z_far = kCryptZ - descent_z_far_chapel_local;
    InteriorFootprint descent;
    descent.center = glm::vec2(kCryptX, (descent_z_near + descent_z_far) * 0.5f);
    descent.half_extents = glm::vec2(kSingleFlightHalfWidth,
                                     (descent_z_near - descent_z_far) * 0.5f);
    out.push_back(descent);
}

} // namespace

void initHubScene()
{
    sScene.cylinders.clear();
    sScene.boxes.clear();
    sScene.interior_footprints.clear();
    // Boundary disc centered on the colle plateau midpoint (Z=-210)
    // so the playable area covers spawn, the colle, and the
    // clear-view strip + back forest behind it.
    sScene.boundary_center = glm::vec2(0.0f, -210.0f);
    sScene.boundary_radius = kHubBoundaryRadius;
    populateHubTrees(sScene.cylinders);
    populateCryptColliders(sScene.boxes);
    populateCryptApseCylinders(sScene.cylinders);
    populateCryptDescent(sScene.boxes);
    populateCryptInteriorFootprint(sScene.interior_footprints);

    // Authored framing around the crypt's façade. All four are
    // hero `pine_a` (variant 0) — no real cypress in the pack yet.
    // Outer pair flanks the front face at Z=-207; inner pair sits
    // BEHIND the outer pair along the long wall at Z=-217, smaller
    // scale for depth layering.
    constexpr int kPineVariant = 0;
    constexpr float kFrontTreeZ = -207.0f;
    constexpr float kBackTreeZ = -210.0f; // ~3m behind the bigs, canopy gap ~1m

    constexpr float kBigScale = 0.9f;
    constexpr float kBigOffsetX = 5.5f;
    for (float sx : {-1.0f, 1.0f})
    {
        CylinderCollider t;
        t.center = glm::vec3(sx * kBigOffsetX, 0.0f, kFrontTreeZ);
        t.radius = 0.4f;
        t.half_height = 6.0f;
        t.forced_variant_idx = kPineVariant;
        t.forced_scale = kBigScale;
        sScene.cylinders.push_back(t);
    }

    constexpr float kSmallScale = 0.6f;
    constexpr float kSmallOffsetX = 5.5f; // align X with the big pair so smalls sit directly behind
    for (float sx : {-1.0f, 1.0f})
    {
        CylinderCollider t;
        t.center = glm::vec3(sx * kSmallOffsetX, 0.0f, kBackTreeZ);
        t.radius = 0.3f;
        t.half_height = 4.0f;
        t.forced_variant_idx = kPineVariant;
        t.forced_scale = kSmallScale;
        sScene.cylinders.push_back(t);
    }
}

const CollisionScene& currentScene()
{
    return sScene;
}

bool isIndoors(const glm::vec2& body_xz)
{
    for (const auto& f : sScene.interior_footprints)
    {
        const float dx = std::abs(body_xz.x - f.center.x);
        const float dz = std::abs(body_xz.y - f.center.y);
        if (dx <= f.half_extents.x && dz <= f.half_extents.y)
            return true;
    }
    return false;
}

void resolveBodyCollision(glm::vec2& body_xz, float body_radius)
{
    const glm::vec2 entry_pos = body_xz;
    const bool log_on = selva::tuning::current().debug_collision_log;
    if (log_on)
    {
        ++sCollisionFrame;
        collisionLog("[frame %d] enter pos=(%.3f,%.3f) r=%.3f\n", sCollisionFrame,
                     entry_pos.x, entry_pos.y, body_radius);
    }
    // Multi-pass push-out. Single pass can leave the body wedged
    // when it's penetrating two adjacent cylinders/boxes — pushing
    // out of one moves it deeper into the other. Two extra iterations
    // resolve any realistic forest-density / wall-corner case.
    constexpr int kMaxPasses = 3;
    for (int pass = 0; pass < kMaxPasses; ++pass)
    {
        bool any_push = false;
        int cyl_idx = 0;
        for (const auto& c : sScene.cylinders)
        {
            const glm::vec2 cyl_xz(c.center.x, c.center.z);
            const glm::vec2 delta = body_xz - cyl_xz;
            const float dist_sq = glm::dot(delta, delta);
            const float min_dist = c.radius + body_radius;
            if (dist_sq >= min_dist * min_dist || dist_sq <= 1e-8f)
            {
                ++cyl_idx;
                continue;
            }
            const float dist = std::sqrt(dist_sq);
            const float push = min_dist - dist;
            const glm::vec2 push_vec = (delta / dist) * push;
            body_xz += push_vec;
            any_push = true;
            if (log_on)
                collisionLog("  pass=%d CYL[%d] center=(%.3f,%.3f) r=%.2f%s "
                             "push=(%.3f,%.3f) -> pos=(%.3f,%.3f)\n",
                             pass, cyl_idx, c.center.x, c.center.z, c.radius,
                             c.collision_only ? " (collision_only)" : "",
                             push_vec.x, push_vec.y, body_xz.x, body_xz.y);
            ++cyl_idx;
        }
        // Box-vs-circle: find the closest point on the AABB to the
        // body, push out along that vector if the body is inside the
        // box's expanded-by-radius zone. Standard technique for
        // AABB-vs-disc overlap resolution.
        int box_idx = 0;
        for (const auto& b : sScene.boxes)
        {
            if (b.camera_only || b.walkable_top)
            {
                // walkable_top: player walks on TOP of these boxes
                // (stair steps, ramps, platforms). Including them in
                // XZ push-out would shove the player off the stair
                // instead of letting them stand on it.
                ++box_idx;
                continue;
            }
            const glm::vec2 d = body_xz - b.center;
            const glm::vec2 clamped(
                std::max(-b.half_extents.x, std::min(b.half_extents.x, d.x)),
                std::max(-b.half_extents.y, std::min(b.half_extents.y, d.y)));
            const glm::vec2 closest = b.center + clamped;
            const glm::vec2 to_body = body_xz - closest;
            const float dist_sq = glm::dot(to_body, to_body);
            if (dist_sq >= body_radius * body_radius)
            {
                ++box_idx;
                continue;
            }
            if (dist_sq <= 1e-8f)
            {
                // Body center is inside the box. Push along the axis
                // of minimum penetration to the nearest face.
                const float pen_x = b.half_extents.x - std::abs(d.x);
                const float pen_y = b.half_extents.y - std::abs(d.y);
                const glm::vec2 before = body_xz;
                if (pen_x < pen_y)
                    body_xz.x = b.center.x + (d.x >= 0.0f ? 1.0f : -1.0f) *
                                                 (b.half_extents.x + body_radius);
                else
                    body_xz.y = b.center.y + (d.y >= 0.0f ? 1.0f : -1.0f) *
                                                 (b.half_extents.y + body_radius);
                any_push = true;
                if (log_on)
                    collisionLog("  pass=%d BOX[%d] center=(%.3f,%.3f) he=(%.2f,%.2f) "
                                 "INSIDE pen_x=%.3f pen_y=%.3f teleport=(%.3f,%.3f) -> "
                                 "(%.3f,%.3f)\n",
                                 pass, box_idx, b.center.x, b.center.y, b.half_extents.x,
                                 b.half_extents.y, pen_x, pen_y, before.x, before.y, body_xz.x,
                                 body_xz.y);
                ++box_idx;
                continue;
            }
            const float dist = std::sqrt(dist_sq);
            const float push = body_radius - dist;
            const glm::vec2 push_vec = (to_body / dist) * push;
            body_xz += push_vec;
            any_push = true;
            if (log_on)
                collisionLog("  pass=%d BOX[%d] center=(%.3f,%.3f) he=(%.2f,%.2f) edge "
                             "push=(%.3f,%.3f) -> pos=(%.3f,%.3f)\n",
                             pass, box_idx, b.center.x, b.center.y, b.half_extents.x,
                             b.half_extents.y, push_vec.x, push_vec.y, body_xz.x, body_xz.y);
            ++box_idx;
        }
        if (!any_push)
            break;
    }

    // Boundary: keep the body's footprint fully inside the play disc.
    if (sScene.boundary_radius > 0.0f)
    {
        const glm::vec2 delta = body_xz - sScene.boundary_center;
        const float dist_sq = glm::dot(delta, delta);
        const float max_dist = sScene.boundary_radius - body_radius;
        if (max_dist > 0.0f && dist_sq > max_dist * max_dist && dist_sq > 1e-8f)
        {
            const float dist = std::sqrt(dist_sq);
            body_xz = sScene.boundary_center + (delta / dist) * max_dist;
        }
    }
    if (log_on)
    {
        const glm::vec2 net = body_xz - entry_pos;
        if (std::abs(net.x) > 0.001f || std::abs(net.y) > 0.001f)
            collisionLog("[frame %d] exit pos=(%.3f,%.3f) net_push=(%.3f,%.3f)\n",
                         sCollisionFrame, body_xz.x, body_xz.y, net.x, net.y);
    }
}

namespace
{
constexpr float kRayEpsilon = 1e-6f;

// Ray vs axis-aligned Y cylinder. Sphere_radius is ignored at this
// level — the caller (raycastScene) reports the t at which the RAY
// CENTER enters the cylinder, and the camera-side iterative push-out
// (sphereOverlapsScene) handles the buffer around the camera.
//
// Pre-existing overlap (origin already inside the cylinder) is NOT
// reported as a hit: the camera-pull-in caller would interpret
// "ray hit at t=0" as "occluder between player and camera" and
// collapse the camera onto the player. The player standing next to
// a tree must not trigger pull-in unless the trunk extends out into
// the camera's path. Only entries with t_near >= 0 count.
float intersectCylinder(const glm::vec3& origin, const glm::vec3& dir, const CylinderCollider& c,
                        float max_t, float /*sphere_radius*/)
{
    const float r = c.radius;
    const float base_y = c.center.y;
    const float top_y = c.center.y + 2.0f * c.half_height;

    // Side: project ray onto XZ, solve quadratic against the infinite
    // cylinder. Use the NEAR root only (t0 = entry) and require t0 >= 0
    // so origin-overlap doesn't produce a spurious zero-distance hit.
    const glm::vec2 ox(origin.x - c.center.x, origin.z - c.center.z);
    const glm::vec2 dx(dir.x, dir.z);
    const float a = glm::dot(dx, dx);
    float best = -1.0f;
    if (a > kRayEpsilon)
    {
        const float b = 2.0f * glm::dot(ox, dx);
        const float cc = glm::dot(ox, ox) - r * r;
        const float disc = b * b - 4.0f * a * cc;
        if (disc >= 0.0f)
        {
            const float sq = std::sqrt(disc);
            const float t0 = (-b - sq) / (2.0f * a);
            if (t0 >= 0.0f && t0 <= max_t)
            {
                const float y = origin.y + dir.y * t0;
                if (y >= base_y && y <= top_y)
                    best = t0;
            }
        }
    }
    // Caps: solve for plane intersection at y = base_y and y = top_y,
    // then check XZ distance from cylinder axis is within radius. Same
    // t >= 0 requirement.
    if (std::abs(dir.y) > kRayEpsilon)
    {
        for (float cap_y : {base_y, top_y})
        {
            const float t = (cap_y - origin.y) / dir.y;
            if (t < 0.0f || t > max_t)
                continue;
            const float px = origin.x + dir.x * t - c.center.x;
            const float pz = origin.z + dir.z * t - c.center.z;
            if (px * px + pz * pz > r * r)
                continue;
            if (best < 0.0f || t < best)
                best = t;
        }
    }
    return best;
}

// Ray vs axis-aligned 3D box (slab method). Sphere_radius is ignored
// at this level — the caller (raycastScene) reports the t at which
// the RAY CENTER enters the box, and the camera-side iterative
// push-out (sphereOverlapsScene) handles the buffer around the
// camera. This split avoids the "ghost zone" pathology a single
// grown-box sphere-cast suffers: a player hugging a wall has its
// lookAt sphere overlapping the wall, which a grown-box test
// interprets as occlusion (collapsing the camera) when really it's
// just "the player is near the wall, which is fine."
//
// Returns t >= 0 on hit, or -1 on miss. If the origin is inside the
// box itself (camera literally inside a wall — pathological), returns
// 0 so the caller falls back to the player position.
float intersectAabb(const glm::vec3& origin, const glm::vec3& dir, const BoxCollider& b,
                    float max_t, float /*sphere_radius*/)
{
    const glm::vec3 ungrown_min(b.center.x - b.half_extents.x, b.y_base,
                                b.center.y - b.half_extents.y);
    const glm::vec3 ungrown_max(b.center.x + b.half_extents.x,
                                b.y_base + 2.0f * b.half_height_y,
                                b.center.y + b.half_extents.y);

    // Origin inside the box: pathological; collapse to player.
    if (origin.x >= ungrown_min.x && origin.x <= ungrown_max.x &&
        origin.y >= ungrown_min.y && origin.y <= ungrown_max.y &&
        origin.z >= ungrown_min.z && origin.z <= ungrown_max.z)
        return 0.0f;

    float t_near = -std::numeric_limits<float>::infinity();
    float t_far = std::numeric_limits<float>::infinity();
    for (int axis = 0; axis < 3; ++axis)
    {
        const float o = origin[axis];
        const float d = dir[axis];
        const float mn = ungrown_min[axis];
        const float mx = ungrown_max[axis];
        if (std::abs(d) < kRayEpsilon)
        {
            if (o < mn || o > mx)
                return -1.0f;
            continue;
        }
        float t0 = (mn - o) / d;
        float t1 = (mx - o) / d;
        if (t0 > t1)
            std::swap(t0, t1);
        if (t0 > t_near)
            t_near = t0;
        if (t1 < t_far)
            t_far = t1;
        if (t_near > t_far)
            return -1.0f;
    }
    if (t_near < 0.0f || t_near > max_t || t_far < 0.0f)
        return -1.0f;
    return t_near;
}
} // namespace

namespace
{
// Sphere vs vertical cylinder. Closest point in XZ (clamp center to
// cylinder radius), then clamp Y to the cylinder's vertical extent.
// Overlap if distance from sphere center to that closest point is
// less than sphere radius.
bool sphereOverlapsCylinder(const glm::vec3& center, float radius, const CylinderCollider& c)
{
    const glm::vec2 cxz(c.center.x, c.center.z);
    const glm::vec2 pxz(center.x, center.z);
    const glm::vec2 delta = pxz - cxz;
    const float dist_sq_xz = glm::dot(delta, delta);
    const float r_combined = c.radius + radius;
    if (dist_sq_xz > r_combined * r_combined)
        return false;
    const float y_min = c.center.y;
    const float y_max = c.center.y + 2.0f * c.half_height;
    if (center.y + radius < y_min || center.y - radius > y_max)
        return false;
    return true;
}

// Sphere vs AABB. Closest point on the box to the sphere center,
// distance squared check.
bool sphereOverlapsAabb(const glm::vec3& center, float radius, const BoxCollider& b)
{
    const glm::vec3 box_min(b.center.x - b.half_extents.x, b.y_base,
                            b.center.y - b.half_extents.y);
    const glm::vec3 box_max(b.center.x + b.half_extents.x,
                            b.y_base + 2.0f * b.half_height_y,
                            b.center.y + b.half_extents.y);
    const glm::vec3 clamped(std::max(box_min.x, std::min(box_max.x, center.x)),
                            std::max(box_min.y, std::min(box_max.y, center.y)),
                            std::max(box_min.z, std::min(box_max.z, center.z)));
    const glm::vec3 d = center - clamped;
    return glm::dot(d, d) < radius * radius;
}
} // namespace

bool sphereOverlapsScene(const CollisionScene& scene, const glm::vec3& center, float radius)
{
    if (radius <= 0.0f)
        return false;
    for (const auto& c : scene.cylinders)
        if (sphereOverlapsCylinder(center, radius, c))
            return true;
    for (const auto& b : scene.boxes)
        if (sphereOverlapsAabb(center, radius, b))
            return true;
    return false;
}

RaycastHit raycastScene(const CollisionScene& scene, const glm::vec3& origin,
                        const glm::vec3& direction, float max_distance)
{
    RaycastHit result;
    const float dir_len_sq = glm::dot(direction, direction);
    if (dir_len_sq < kRayEpsilon || max_distance <= 0.0f)
        return result;
    const glm::vec3 dir = direction / std::sqrt(dir_len_sq);

    float best = max_distance;
    bool any = false;
    for (const auto& c : scene.cylinders)
    {
        const float t = intersectCylinder(origin, dir, c, best, 0.0f);
        if (t >= 0.0f && t < best)
        {
            best = t;
            any = true;
        }
    }
    for (const auto& b : scene.boxes)
    {
        const float t = intersectAabb(origin, dir, b, best, 0.0f);
        if (t >= 0.0f && t < best)
        {
            best = t;
            any = true;
        }
    }
    result.hit = any;
    result.distance = any ? best : max_distance;
    return result;
}

} // namespace selva::world
