#include "world/Collision.h"

#include "WallClock.h"
#include "debug/Flags.h"
#include "world/CryptLayout.h"
#include "world/Terrain.h"

#include <glm/geometric.hpp>

#include <cmath>
#include <cstdio>
#include <limits>
#include <random>
#include <utility>

#include <fmt/core.h>

namespace selva::world
{

namespace
{
CollisionRegion sRegion;

FILE* sCollisionLog = nullptr;
int sCollisionFrame = 0;

// Caller MUST gate on selva::debug::flags().collision_log before invoking to keep
// the disabled-state cost at zero (the type-safe template still
// instantiates a formatter per call site if not gated). Routed
// through fmt::format → fwrite rather than the engine::log::Channel
// because this file is a structured per-pass trace (CSV-like rows),
// not a routed log — the channel "[name:LV]" prefix would corrupt
// the format.
template <typename... Args> void collisionLog(fmt::format_string<Args...> fmt, Args&&... args)
{
    if (sCollisionLog == nullptr)
        sCollisionLog = std::fopen("collision-debug.log", "w");
    if (sCollisionLog == nullptr)
        return;
    const std::string line = fmt::format(fmt, std::forward<Args>(args)...);
    std::fwrite(line.data(), 1, line.size(), sCollisionLog);
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
            // Trees scatter ONLY on the selva_inner terrain region.
            // Other regions (Limbo, future Inferno layers) are
            // underground and don't have foliage per the doctrine in
            // [[project_acheron_river_lore]] — Limbo's "fresh green
            // grass" canon is gone with Limbo's dysfunction; deeper
            // circles never had foliage. Skip if this XZ isn't inside
            // selva_inner.
            const auto* region = terrainRegionAt(x, z);
            if (region == nullptr || region->name != "selva_inner")
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

// Chapel collision authoring moved to the chapel_exterior.glb +
// chapel_interior.glb meshes (loaded by JsonRegion from the surface
// region's static_meshes array, registered into Jolt at activation).
// The legacy populateCryptColliders / populateCryptApseCylinders /
// populateCryptDescent C++ collider authoring was removed 2026-05-24
// to end the dual-source-of-truth between mesh and code. See
// docs/design/audits/chapel_source_audit_2026-05-24.md for the audit
// + harmonization decision (option H1: mesh authoritative).
//
// If new architecture needs collision, author it in the chapel .blend
// (regenerate via games/selva-oscura/scripts/blender/gen_crypt_*.py)
// rather than re-introducing C++ collider duplicates here.

} // namespace

void initHubRegion()
{
    sRegion.cylinders.clear();
    sRegion.boxes.clear();
    // Boundary disc centered on the colle plateau midpoint (Z=-210)
    // so the playable area covers spawn, the colle, and the
    // clear-view strip + back forest behind it.
    sRegion.boundary_center = glm::vec2(0.0f, -210.0f);
    sRegion.boundary_radius = kHubBoundaryRadius;
    populateHubTrees(sRegion.cylinders);
    // Chapel collision is owned by the chapel_exterior.glb +
    // chapel_interior.glb meshes (loaded by JsonRegion from the
    // surface region's static_meshes array). The legacy
    // populateCryptColliders / populateCryptApseCylinders /
    // populateCryptDescent C++ collider authoring was removed
    // 2026-05-24 to end the dual-source-of-truth between mesh and
    // code; see docs/design/audits/chapel_source_audit_2026-05-24.md.

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
    for (const float sx : {-1.0f, 1.0f})
    {
        CylinderCollider t;
        t.center = glm::vec3(sx * kBigOffsetX, 0.0f, kFrontTreeZ);
        t.radius = 0.4f;
        t.half_height = 6.0f;
        t.forced_variant_idx = kPineVariant;
        t.forced_scale = kBigScale;
        sRegion.cylinders.push_back(t);
    }

    constexpr float kSmallScale = 0.6f;
    constexpr float kSmallOffsetX = 5.5f; // align X with the big pair so smalls sit directly behind
    for (const float sx : {-1.0f, 1.0f})
    {
        CylinderCollider t;
        t.center = glm::vec3(sx * kSmallOffsetX, 0.0f, kBackTreeZ);
        t.radius = 0.3f;
        t.half_height = 4.0f;
        t.forced_variant_idx = kPineVariant;
        t.forced_scale = kSmallScale;
        sRegion.cylinders.push_back(t);
    }
}

const CollisionRegion& currentRegion()
{
    return sRegion;
}

namespace
{
// One cylinder-vs-body push-out pass; returns true if the body was
// nudged. Skips no-overlap (>= min_dist) and origin-touching (~0)
// cases.
bool pushOutOneCylinder(const CylinderCollider& c, int cyl_idx, int pass, float body_radius,
                        bool log_on, glm::vec2& body_xz)
{
    const glm::vec2 cyl_xz(c.center.x, c.center.z);
    const glm::vec2 delta = body_xz - cyl_xz;
    const float dist_sq = glm::dot(delta, delta);
    const float min_dist = c.radius + body_radius;
    if (dist_sq >= min_dist * min_dist || dist_sq <= 1e-8f)
        return false;
    const float dist = std::sqrt(dist_sq);
    const float push = min_dist - dist;
    const glm::vec2 push_vec = (delta / dist) * push;
    body_xz += push_vec;
    if (log_on)
        collisionLog("  pass={} CYL[{}] center=({:.3f},{:.3f}) r={:.2f}{} "
                     "push=({:.3f},{:.3f}) -> pos=({:.3f},{:.3f})\n",
                     pass, cyl_idx, c.center.x, c.center.z, c.radius,
                     c.collision_only ? " (collision_only)" : "", push_vec.x, push_vec.y, body_xz.x,
                     body_xz.y);
    return true;
}

// One box-vs-circle push-out pass. Skips camera_only + walkable_top
// boxes (walking on top, not against). Body-center-inside is handled
// by axis-of-min-penetration teleport; edge contact uses the closest
// point on the AABB.
bool pushOutOneBox(const BoxCollider& b, int box_idx, int pass, float body_radius, bool log_on,
                   glm::vec2& body_xz)
{
    if (b.camera_only || b.walkable_top)
        return false;
    const glm::vec2 d = body_xz - b.center;
    const glm::vec2 clamped(std::max(-b.half_extents.x, std::min(b.half_extents.x, d.x)),
                            std::max(-b.half_extents.y, std::min(b.half_extents.y, d.y)));
    const glm::vec2 closest = b.center + clamped;
    const glm::vec2 to_body = body_xz - closest;
    const float dist_sq = glm::dot(to_body, to_body);
    if (dist_sq >= body_radius * body_radius)
        return false;
    if (dist_sq <= 1e-8f)
    {
        const float pen_x = b.half_extents.x - std::abs(d.x);
        const float pen_y = b.half_extents.y - std::abs(d.y);
        const glm::vec2 before = body_xz;
        if (pen_x < pen_y)
            body_xz.x =
                b.center.x + (d.x >= 0.0f ? 1.0f : -1.0f) * (b.half_extents.x + body_radius);
        else
            body_xz.y =
                b.center.y + (d.y >= 0.0f ? 1.0f : -1.0f) * (b.half_extents.y + body_radius);
        if (log_on)
            collisionLog("  pass={} BOX[{}] center=({:.3f},{:.3f}) he=({:.2f},{:.2f}) "
                         "INSIDE pen_x={:.3f} pen_y={:.3f} teleport=({:.3f},{:.3f}) -> "
                         "({:.3f},{:.3f})\n",
                         pass, box_idx, b.center.x, b.center.y, b.half_extents.x, b.half_extents.y,
                         pen_x, pen_y, before.x, before.y, body_xz.x, body_xz.y);
        return true;
    }
    const float dist = std::sqrt(dist_sq);
    const float push = body_radius - dist;
    const glm::vec2 push_vec = (to_body / dist) * push;
    body_xz += push_vec;
    if (log_on)
        collisionLog("  pass={} BOX[{}] center=({:.3f},{:.3f}) he=({:.2f},{:.2f}) edge "
                     "push=({:.3f},{:.3f}) -> pos=({:.3f},{:.3f})\n",
                     pass, box_idx, b.center.x, b.center.y, b.half_extents.x, b.half_extents.y,
                     push_vec.x, push_vec.y, body_xz.x, body_xz.y);
    return true;
}

// Clamp body within the play-disc boundary. Pushes back along the
// radial axis so the body's footprint stays fully inside.
void clampToBoundary(glm::vec2& body_xz, float body_radius)
{
    if (sRegion.boundary_radius <= 0.0f)
        return;
    const glm::vec2 delta = body_xz - sRegion.boundary_center;
    const float dist_sq = glm::dot(delta, delta);
    const float max_dist = sRegion.boundary_radius - body_radius;
    if (max_dist > 0.0f && dist_sq > max_dist * max_dist && dist_sq > 1e-8f)
    {
        const float dist = std::sqrt(dist_sq);
        body_xz = sRegion.boundary_center + (delta / dist) * max_dist;
    }
}
} // namespace

void resolveBodyCollision(glm::vec2& body_xz, float body_radius)
{
    const glm::vec2 entry_pos = body_xz;
    const bool log_on = selva::debug::flags().collision_log;
    if (log_on)
    {
        ++sCollisionFrame;
        collisionLog("[frame {}] enter pos=({:.3f},{:.3f}) r={:.3f}\n", sCollisionFrame,
                     entry_pos.x, entry_pos.y, body_radius);
    }
    // Multi-pass push-out. Single pass can leave the body wedged
    // when it's penetrating two adjacent cylinders/boxes — pushing
    // out of one moves it deeper into the other. Three passes resolve
    // any realistic forest-density / wall-corner case.
    constexpr int kMaxPasses = 3;
    for (int pass = 0; pass < kMaxPasses; ++pass)
    {
        bool any_push = false;
        int cyl_idx = 0;
        for (const auto& c : sRegion.cylinders)
        {
            if (pushOutOneCylinder(c, cyl_idx, pass, body_radius, log_on, body_xz))
                any_push = true;
            ++cyl_idx;
        }
        int box_idx = 0;
        for (const auto& b : sRegion.boxes)
        {
            if (pushOutOneBox(b, box_idx, pass, body_radius, log_on, body_xz))
                any_push = true;
            ++box_idx;
        }
        if (!any_push)
            break;
    }
    clampToBoundary(body_xz, body_radius);
    if (log_on)
    {
        const glm::vec2 net = body_xz - entry_pos;
        if (std::abs(net.x) > 0.001f || std::abs(net.y) > 0.001f)
            collisionLog("[frame {}] exit pos=({:.3f},{:.3f}) net_push=({:.3f},{:.3f})\n",
                         sCollisionFrame, body_xz.x, body_xz.y, net.x, net.y);
    }
}

namespace
{
constexpr float kRayEpsilon = 1e-6f;

// Ray vs axis-aligned Y cylinder. Sphere_radius is ignored at this
// level — the caller (raycastRegion) reports the t at which the RAY
// CENTER enters the cylinder, and the camera-side iterative push-out
// (sphereOverlapsRegion) handles the buffer around the camera.
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
        for (const float cap_y : {base_y, top_y})
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

// One axis of the slab test. Returns false on miss; on hit narrows
// t_near/t_far. Parallel-to-slab case (|d|<eps) is a miss if origin
// is outside the slab; otherwise the axis doesn't constrain t.
bool intersectAabbSlab(float o, float d, float mn, float mx, float& t_near, float& t_far)
{
    if (std::abs(d) < kRayEpsilon)
        return !(o < mn || o > mx);
    float t0 = (mn - o) / d;
    float t1 = (mx - o) / d;
    if (t0 > t1)
        std::swap(t0, t1);
    if (t0 > t_near)
        t_near = t0;
    if (t1 < t_far)
        t_far = t1;
    return t_near <= t_far;
}

// Ray vs axis-aligned 3D box (slab method). Sphere_radius is ignored
// at this level — the caller (raycastRegion) reports the t at which
// the RAY CENTER enters the box, and the camera-side iterative
// push-out (sphereOverlapsRegion) handles the buffer around the
// camera. Returns t >= 0 on hit, or -1 on miss. If the origin is
// inside the box (camera literally inside a wall — pathological),
// returns 0 so the caller falls back to the player position.
float intersectAabb(const glm::vec3& origin, const glm::vec3& dir, const BoxCollider& b,
                    float max_t, float /*sphere_radius*/)
{
    const glm::vec3 ungrown_min(b.center.x - b.half_extents.x, b.y_base,
                                b.center.y - b.half_extents.y);
    const glm::vec3 ungrown_max(b.center.x + b.half_extents.x, b.y_base + 2.0f * b.half_height_y,
                                b.center.y + b.half_extents.y);

    if (origin.x >= ungrown_min.x && origin.x <= ungrown_max.x && origin.y >= ungrown_min.y &&
        origin.y <= ungrown_max.y && origin.z >= ungrown_min.z && origin.z <= ungrown_max.z)
        return 0.0f;

    float t_near = -std::numeric_limits<float>::infinity();
    float t_far = std::numeric_limits<float>::infinity();
    for (int axis = 0; axis < 3; ++axis)
    {
        if (!intersectAabbSlab(origin[axis], dir[axis], ungrown_min[axis], ungrown_max[axis],
                               t_near, t_far))
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
    const glm::vec3 box_min(b.center.x - b.half_extents.x, b.y_base, b.center.y - b.half_extents.y);
    const glm::vec3 box_max(b.center.x + b.half_extents.x, b.y_base + 2.0f * b.half_height_y,
                            b.center.y + b.half_extents.y);
    const glm::vec3 clamped(std::max(box_min.x, std::min(box_max.x, center.x)),
                            std::max(box_min.y, std::min(box_max.y, center.y)),
                            std::max(box_min.z, std::min(box_max.z, center.z)));
    const glm::vec3 d = center - clamped;
    return glm::dot(d, d) < radius * radius;
}
} // namespace

bool sphereOverlapsRegion(const CollisionRegion& region, const glm::vec3& center, float radius)
{
    if (radius <= 0.0f)
        return false;
    for (const auto& c : region.cylinders)
        if (sphereOverlapsCylinder(center, radius, c))
            return true;
    for (const auto& b : region.boxes)
        if (sphereOverlapsAabb(center, radius, b))
            return true;
    return false;
}

RaycastHit raycastRegion(const CollisionRegion& region, const glm::vec3& origin,
                         const glm::vec3& direction, float max_distance)
{
    RaycastHit result;
    const float dir_len_sq = glm::dot(direction, direction);
    if (dir_len_sq < kRayEpsilon || max_distance <= 0.0f)
        return result;
    const glm::vec3 dir = direction / std::sqrt(dir_len_sq);

    float best = max_distance;
    bool any = false;
    for (const auto& c : region.cylinders)
    {
        const float t = intersectCylinder(origin, dir, c, best, 0.0f);
        if (t >= 0.0f && t < best)
        {
            best = t;
            any = true;
        }
    }
    for (const auto& b : region.boxes)
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
