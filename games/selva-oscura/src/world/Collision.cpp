#include "world/Collision.h"

#include "Tunables.h"
#include "WallClock.h"
#include "world/CryptLayout.h"
#include "world/Terrain.h"

#include <glm/geometric.hpp>

#include <cmath>
#include <cstdarg>
#include <cstdio>
#include <random>

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
void populateCryptColliders(std::vector<BoxCollider>& out)
{
    using namespace crypt_layout;
    constexpr float kHalfThickness = kWallThickness * 0.5f;

    // Front wall (faces +Z, toward spawn). Door in the middle.
    // Front face's outer edge is at Z = kCryptZ + kHalfLength = -206.
    // Slab center sits at outer_edge - half_thickness.
    const float kFrontSlabZ = kCryptZ + kHalfLength - kHalfThickness;
    // Left half-segment: X from -kHalfWidth to -kDoorHalfWidth.
    {
        BoxCollider b;
        const float left_outer = -kHalfWidth;
        const float left_inner = -kDoorHalfWidth;
        b.center = glm::vec2(kCryptX + (left_outer + left_inner) * 0.5f, kFrontSlabZ);
        b.half_extents = glm::vec2((left_inner - left_outer) * 0.5f, kHalfThickness);
        out.push_back(b);
    }
    // Right half-segment: X from +kDoorHalfWidth to +kHalfWidth.
    {
        BoxCollider b;
        const float right_inner = kDoorHalfWidth;
        const float right_outer = kHalfWidth;
        b.center = glm::vec2(kCryptX + (right_inner + right_outer) * 0.5f, kFrontSlabZ);
        b.half_extents = glm::vec2((right_outer - right_inner) * 0.5f, kHalfThickness);
        out.push_back(b);
    }
    // Back wall (faces -Z, apse side). Single contiguous slab.
    {
        BoxCollider b;
        b.center = glm::vec2(kCryptX, kCryptZ - kHalfLength + kHalfThickness);
        b.half_extents = glm::vec2(kHalfWidth, kHalfThickness);
        out.push_back(b);
    }
    // Left long wall (faces -X).
    {
        BoxCollider b;
        b.center = glm::vec2(kCryptX - kHalfWidth + kHalfThickness, kCryptZ);
        b.half_extents = glm::vec2(kHalfThickness, kHalfLength);
        out.push_back(b);
    }
    // Right long wall (faces +X).
    {
        BoxCollider b;
        b.center = glm::vec2(kCryptX + kHalfWidth - kHalfThickness, kCryptZ);
        b.half_extents = glm::vec2(kHalfThickness, kHalfLength);
        out.push_back(b);
    }
}

// Apse rear-curve: a half-ring of small cylinders wrapping the
// semicircular apse from outside. Spans 180° centered on the rear
// face's apex (-Z direction). Player can't pass through the curve.
void populateCryptApseCylinders(std::vector<CylinderCollider>& out)
{
    using namespace crypt_layout;
    constexpr int kArcSegments = 16; // ~11° between cylinder centers
    constexpr float kCylRadius = 0.15f;
    // Centers sit one cyl-radius INWARD of the apse surface so their
    // outer edge aligns with the visible stone curve.
    constexpr float kArcRadius = kApseRadius - kCylRadius;
    const float arc_cx = kCryptX;
    const float arc_cz = kCryptZ - kHalfLength;
    for (int i = 0; i <= kArcSegments; ++i)
    {
        const float t = static_cast<float>(i) / static_cast<float>(kArcSegments);
        // Half-circle bulging toward -Z. t=0 at +X corner, t=0.5 at
        // the back apex, t=1 at -X corner.
        const float ang = -3.14159265f * t;
        const float x = arc_cx + std::cos(ang) * kArcRadius;
        const float z = arc_cz + std::sin(ang) * kArcRadius;
        CylinderCollider c;
        c.center = glm::vec3(x, 0.0f, z);
        c.radius = kCylRadius;
        c.half_height = 4.0f;
        c.collision_only = true;
        out.push_back(c);
    }
}

void populateCryptInteriorFootprint(std::vector<InteriorFootprint>& out)
{
    using namespace crypt_layout;
    // Indoor footprint extends 30cm PAST the door's outer face so
    // that walking up to the threshold trips concrete on the foot
    // that lands at the door, not the one that lands after stepping
    // through it. Without the extension, the trailing foot during a
    // threshold cross plants outside the chapel (heel-strike behind
    // the body) and fires grass even though the player visually
    // sees the leading foot already inside — a perceived "wrong
    // sample" caused by walking biomechanics, not bug logic.
    // Asymmetric: shifted toward the door (+Z), keeping the back/
    // sides aligned with the chapel's outer face.
    constexpr float kDoorSideExtension = 0.30f;
    InteriorFootprint f;
    f.center = glm::vec2(kCryptX, kCryptZ + kDoorSideExtension * 0.5f);
    f.half_extents = glm::vec2(kHalfWidth, kHalfLength + kDoorSideExtension * 0.5f);
    out.push_back(f);
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

} // namespace selva::world
