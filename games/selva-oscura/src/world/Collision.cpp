#include "world/Collision.h"

#include <glm/geometric.hpp>

#include <cmath>
#include <random>

namespace selva::world
{

namespace
{
CollisionScene sScene;

// Selva-oscura hub layout. Procedural rejection sampling: random
// positions inside a disc, accepted only if outside the central
// clearing and far enough from already-placed trees. Deterministic
// seed so the forest is stable across runs.
constexpr float kHubBoundaryRadius = 30.0f;
constexpr float kHubClearingRadius = 4.0f;
constexpr float kHubMinTreeSpacing = 3.5f;
constexpr int kHubTreeTargetCount = 35;
constexpr int kHubMaxAttempts = 4000;
constexpr unsigned int kHubSeed = 0xDA17EU; // "DANTE" — stable layout

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

void populateHubTrees(std::vector<CylinderCollider>& out)
{
    std::mt19937 rng(kHubSeed);
    std::uniform_real_distribution<float> angle_dist(0.0f, 2.0f * 3.14159265f);
    // Bias toward the edge: sqrt() gives uniform area distribution.
    std::uniform_real_distribution<float> radial_dist(0.0f, 1.0f);
    std::uniform_real_distribution<float> radius_dist(0.28f, 0.55f);

    int attempts = 0;
    while (static_cast<int>(out.size()) < kHubTreeTargetCount && attempts < kHubMaxAttempts)
    {
        ++attempts;
        const float u = radial_dist(rng);
        const float r = std::sqrt(u) * kHubBoundaryRadius;
        if (r < kHubClearingRadius)
            continue;
        const float a = angle_dist(rng);
        const float x = std::cos(a) * r;
        const float z = std::sin(a) * r;
        const float trunk_r = radius_dist(rng);
        // Keep the trunk fully inside the boundary so collision doesn't
        // fight the boundary push-back at the edge.
        if (r + trunk_r > kHubBoundaryRadius - 0.5f)
            continue;
        if (tooCloseToExisting(out, x, z, trunk_r))
            continue;

        CylinderCollider c;
        c.center = glm::vec3(x, 0.0f, z);
        c.radius = trunk_r;
        c.half_height = 2.0f;
        out.push_back(c);
    }
}

} // namespace

void initHubScene()
{
    sScene.cylinders.clear();
    sScene.boundary_center = glm::vec2(0.0f, 0.0f);
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
