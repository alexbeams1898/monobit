#include "world/Collision.h"

#include <glm/geometric.hpp>

#include <cmath>
#include <random>

namespace selva::world
{

namespace
{
CollisionScene sScene;

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

} // namespace

void initHubScene()
{
    sScene.cylinders.clear();
    // Boundary disc centered on the colle plateau midpoint (Z=-210)
    // so the playable area covers spawn, the colle, and the
    // clear-view strip + back forest behind it.
    sScene.boundary_center = glm::vec2(0.0f, -210.0f);
    sScene.boundary_radius = kHubBoundaryRadius;
    populateHubTrees(sScene.cylinders);
}

const CollisionScene& currentScene()
{
    return sScene;
}

void resolveBodyCollision(glm::vec2& body_xz, float body_radius)
{
    // Multi-pass push-out. Single pass can leave the body wedged
    // when it's penetrating two adjacent cylinders — pushing out of
    // one moves it deeper into the other. Two extra iterations
    // resolve any realistic forest-density corner.
    constexpr int kMaxPasses = 3;
    for (int pass = 0; pass < kMaxPasses; ++pass)
    {
        bool any_push = false;
        for (const auto& c : sScene.cylinders)
        {
            const glm::vec2 cyl_xz(c.center.x, c.center.z);
            const glm::vec2 delta = body_xz - cyl_xz;
            const float dist_sq = glm::dot(delta, delta);
            const float min_dist = c.radius + body_radius;
            if (dist_sq >= min_dist * min_dist || dist_sq <= 1e-8f)
                continue;
            const float dist = std::sqrt(dist_sq);
            const float push = min_dist - dist;
            body_xz += (delta / dist) * push;
            any_push = true;
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
}

} // namespace selva::world
