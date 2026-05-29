#include "world/Collision.h"

#include <glm/vec3.hpp>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

using Catch::Approx;
using selva::world::BoxCollider;
using selva::world::CollisionRegion;
using selva::world::CylinderCollider;
using selva::world::RaycastHit;
using selva::world::raycastRegion;
using selva::world::sphereOverlapsRegion;

namespace
{
CylinderCollider makeCyl(glm::vec3 center, float radius, float half_height)
{
    CylinderCollider c;
    c.center = center;
    c.radius = radius;
    c.half_height = half_height;
    return c;
}

BoxCollider makeBox(glm::vec2 center, glm::vec2 half_extents, float y_base, float half_height_y)
{
    BoxCollider b;
    b.center = center;
    b.half_extents = half_extents;
    b.y_base = y_base;
    b.half_height_y = half_height_y;
    return b;
}
} // namespace

TEST_CASE("raycastRegion returns no hit on empty region", "[raycast]")
{
    const CollisionRegion region;
    const RaycastHit r = raycastRegion(region, glm::vec3(0.0f), glm::vec3(1.0f, 0.0f, 0.0f), 10.0f);
    REQUIRE_FALSE(r.hit);
    REQUIRE(r.distance == Approx(10.0f));
}

TEST_CASE("raycastRegion degenerate inputs return no hit", "[raycast]")
{
    CollisionRegion region;
    region.cylinders.push_back(makeCyl(glm::vec3(5.0f, 0.0f, 0.0f), 1.0f, 2.0f));

    SECTION("zero direction")
    {
        const RaycastHit r = raycastRegion(region, glm::vec3(0.0f), glm::vec3(0.0f), 10.0f);
        REQUIRE_FALSE(r.hit);
    }
    SECTION("zero max distance")
    {
        const RaycastHit r =
            raycastRegion(region, glm::vec3(0.0f), glm::vec3(1.0f, 0.0f, 0.0f), 0.0f);
        REQUIRE_FALSE(r.hit);
    }
}

TEST_CASE("raycastRegion hits cylinder side from outside", "[raycast][cylinder]")
{
    CollisionRegion region;
    // Cylinder at x=5, radius 1, spanning Y [0, 4].
    region.cylinders.push_back(makeCyl(glm::vec3(5.0f, 0.0f, 0.0f), 1.0f, 2.0f));

    // Ray from origin pointing +X at Y=2 (mid-height). Should hit the near
    // side of the cylinder at x = 5 - radius = 4 -> t = 4.
    const RaycastHit r =
        raycastRegion(region, glm::vec3(0.0f, 2.0f, 0.0f), glm::vec3(1.0f, 0.0f, 0.0f), 100.0f);
    REQUIRE(r.hit);
    REQUIRE(r.distance == Approx(4.0f));
}

TEST_CASE("raycastRegion misses cylinder when ray passes over the top", "[raycast][cylinder]")
{
    CollisionRegion region;
    // Cylinder at x=5, radius 1, spanning Y [0, 4].
    region.cylinders.push_back(makeCyl(glm::vec3(5.0f, 0.0f, 0.0f), 1.0f, 2.0f));

    // Ray from origin Y=10 pointing +X stays at Y=10 forever; well above
    // the cylinder top of Y=4.
    const RaycastHit r =
        raycastRegion(region, glm::vec3(0.0f, 10.0f, 0.0f), glm::vec3(1.0f, 0.0f, 0.0f), 100.0f);
    REQUIRE_FALSE(r.hit);
}

TEST_CASE("raycastRegion hits cylinder cap from above", "[raycast][cylinder]")
{
    CollisionRegion region;
    // Cylinder at origin, radius 1, spanning Y [0, 4].
    region.cylinders.push_back(makeCyl(glm::vec3(0.0f, 0.0f, 0.0f), 1.0f, 2.0f));

    // Ray straight down through the cylinder's axis from Y=10. Top cap at
    // Y=4 -> t = 6.
    const RaycastHit r =
        raycastRegion(region, glm::vec3(0.0f, 10.0f, 0.0f), glm::vec3(0.0f, -1.0f, 0.0f), 100.0f);
    REQUIRE(r.hit);
    REQUIRE(r.distance == Approx(6.0f));
}

TEST_CASE("raycastRegion misses cylinder when ray passes outside the radius", "[raycast][cylinder]")
{
    CollisionRegion region;
    region.cylinders.push_back(makeCyl(glm::vec3(5.0f, 0.0f, 0.0f), 1.0f, 2.0f));

    // Ray parallel to +X but offset in Z by 5m; cylinder radius is 1, so
    // miss.
    const RaycastHit r =
        raycastRegion(region, glm::vec3(0.0f, 2.0f, 5.0f), glm::vec3(1.0f, 0.0f, 0.0f), 100.0f);
    REQUIRE_FALSE(r.hit);
}

TEST_CASE("raycastRegion respects max_distance", "[raycast]")
{
    CollisionRegion region;
    region.cylinders.push_back(makeCyl(glm::vec3(20.0f, 0.0f, 0.0f), 1.0f, 2.0f));

    // Cylinder at x=20, ray would hit at t=19. With max_distance=10, miss.
    const RaycastHit r =
        raycastRegion(region, glm::vec3(0.0f, 2.0f, 0.0f), glm::vec3(1.0f, 0.0f, 0.0f), 10.0f);
    REQUIRE_FALSE(r.hit);
    REQUIRE(r.distance == Approx(10.0f));
}

TEST_CASE("raycastRegion normalizes non-unit direction", "[raycast]")
{
    CollisionRegion region;
    region.cylinders.push_back(makeCyl(glm::vec3(5.0f, 0.0f, 0.0f), 1.0f, 2.0f));

    // Same geometry as the side-hit case, but direction vector scaled by 3.
    // Distance is reported in world units, so the answer should be the same
    // 4m regardless of input magnitude.
    const RaycastHit r =
        raycastRegion(region, glm::vec3(0.0f, 2.0f, 0.0f), glm::vec3(3.0f, 0.0f, 0.0f), 100.0f);
    REQUIRE(r.hit);
    REQUIRE(r.distance == Approx(4.0f));
}

TEST_CASE("raycastRegion hits AABB front face", "[raycast][box]")
{
    CollisionRegion region;
    // Box centered at (5, 0) XZ, 1m half-width in X and Z, spanning Y [0, 4].
    region.boxes.push_back(makeBox(glm::vec2(5.0f, 0.0f), glm::vec2(1.0f, 1.0f), 0.0f, 2.0f));

    // Ray from origin +X at mid-height. Hits near face at x = 4 -> t = 4.
    const RaycastHit r =
        raycastRegion(region, glm::vec3(0.0f, 2.0f, 0.0f), glm::vec3(1.0f, 0.0f, 0.0f), 100.0f);
    REQUIRE(r.hit);
    REQUIRE(r.distance == Approx(4.0f));
}

TEST_CASE("raycastRegion misses AABB when ray clears the top", "[raycast][box]")
{
    CollisionRegion region;
    region.boxes.push_back(makeBox(glm::vec2(5.0f, 0.0f), glm::vec2(1.0f, 1.0f), 0.0f, 2.0f));

    // Ray from Y=10 pointing +X stays above box top (Y=4) the whole way.
    const RaycastHit r =
        raycastRegion(region, glm::vec3(0.0f, 10.0f, 0.0f), glm::vec3(1.0f, 0.0f, 0.0f), 100.0f);
    REQUIRE_FALSE(r.hit);
}

TEST_CASE("raycastRegion misses AABB when ray parallel to slab and outside", "[raycast][box]")
{
    CollisionRegion region;
    region.boxes.push_back(makeBox(glm::vec2(5.0f, 0.0f), glm::vec2(1.0f, 1.0f), 0.0f, 2.0f));

    // Ray parallel to +X but Z=5 (outside the Z-slab [-1, 1] of the box).
    const RaycastHit r =
        raycastRegion(region, glm::vec3(0.0f, 2.0f, 5.0f), glm::vec3(1.0f, 0.0f, 0.0f), 100.0f);
    REQUIRE_FALSE(r.hit);
}

TEST_CASE("raycastRegion picks nearest among multiple colliders", "[raycast]")
{
    CollisionRegion region;
    // Cylinder far at x=20, box near at x=5. Ray hits box first at t=4.
    region.cylinders.push_back(makeCyl(glm::vec3(20.0f, 0.0f, 0.0f), 1.0f, 2.0f));
    region.boxes.push_back(makeBox(glm::vec2(5.0f, 0.0f), glm::vec2(1.0f, 1.0f), 0.0f, 2.0f));

    const RaycastHit r =
        raycastRegion(region, glm::vec3(0.0f, 2.0f, 0.0f), glm::vec3(1.0f, 0.0f, 0.0f), 100.0f);
    REQUIRE(r.hit);
    REQUIRE(r.distance == Approx(4.0f));
}

TEST_CASE("raycastRegion diagonal ray hits cylinder at expected distance", "[raycast]")
{
    CollisionRegion region;
    // Cylinder centered at (5, 5) XZ, radius 1, tall.
    region.cylinders.push_back(makeCyl(glm::vec3(5.0f, 0.0f, 5.0f), 1.0f, 2.0f));

    // Diagonal ray at 45deg in XZ. Cylinder center is at distance
    // sqrt(50) ~= 7.071. Near surface at radius 1 inside -> t ~= 6.071.
    const RaycastHit r =
        raycastRegion(region, glm::vec3(0.0f, 2.0f, 0.0f), glm::vec3(1.0f, 0.0f, 1.0f), 100.0f);
    REQUIRE(r.hit);
    REQUIRE(r.distance == Approx(6.0710678f).margin(0.001f));
}

TEST_CASE("raycastRegion ignores collider when ray points away from it", "[raycast]")
{
    CollisionRegion region;
    // Wall just to the +X of the origin: AABB at X=[0.5, 1.5].
    region.boxes.push_back(makeBox(glm::vec2(1.0f, 0.0f), glm::vec2(0.5f, 0.5f), 0.0f, 2.0f));

    // Origin in open ground at X=0 (not inside the wall). Ray fires
    // in -X, AWAY from the wall. No hit.
    const RaycastHit r =
        raycastRegion(region, glm::vec3(0.0f, 1.0f, 0.0f), glm::vec3(-1.0f, 0.0f, 0.0f), 6.0f);
    REQUIRE_FALSE(r.hit);
}

TEST_CASE("raycastRegion returns t=0 when origin is inside a box", "[raycast]")
{
    CollisionRegion region;
    // Wall at X=[0.5, 1.5].
    region.boxes.push_back(makeBox(glm::vec2(1.0f, 0.0f), glm::vec2(0.5f, 0.5f), 0.0f, 2.0f));

    // Origin INSIDE the wall: pathological camera-inside-wall case.
    // Reports hit at t=0 so the caller collapses the camera onto the
    // player rather than rendering from inside the wall.
    const RaycastHit r =
        raycastRegion(region, glm::vec3(1.0f, 1.0f, 0.0f), glm::vec3(1.0f, 0.0f, 0.0f), 6.0f);
    REQUIRE(r.hit);
    REQUIRE(r.distance == Approx(0.0f));
}

TEST_CASE("sphereOverlapsRegion returns false on empty region", "[sphere-overlap]")
{
    const CollisionRegion region;
    REQUIRE_FALSE(sphereOverlapsRegion(region, glm::vec3(0.0f), 1.0f));
}

TEST_CASE("sphereOverlapsRegion returns false for zero radius", "[sphere-overlap]")
{
    CollisionRegion region;
    region.boxes.push_back(makeBox(glm::vec2(0.0f, 0.0f), glm::vec2(1.0f, 1.0f), 0.0f, 2.0f));
    // Origin inside the box, but zero radius -> degenerate, no overlap.
    REQUIRE_FALSE(sphereOverlapsRegion(region, glm::vec3(0.0f, 1.0f, 0.0f), 0.0f));
}

TEST_CASE("sphereOverlapsRegion detects sphere inside a box", "[sphere-overlap]")
{
    CollisionRegion region;
    // Box at origin, half-extent 1m in XZ, Y [0, 4].
    region.boxes.push_back(makeBox(glm::vec2(0.0f, 0.0f), glm::vec2(1.0f, 1.0f), 0.0f, 2.0f));
    REQUIRE(sphereOverlapsRegion(region, glm::vec3(0.0f, 2.0f, 0.0f), 0.3f));
}

TEST_CASE("sphereOverlapsRegion detects sphere brushing a box edge", "[sphere-overlap]")
{
    CollisionRegion region;
    region.boxes.push_back(makeBox(glm::vec2(0.0f, 0.0f), glm::vec2(1.0f, 1.0f), 0.0f, 2.0f));
    // Sphere center at X=1.3, radius 0.4 -> overlaps the +X face at X=1.0.
    REQUIRE(sphereOverlapsRegion(region, glm::vec3(1.3f, 2.0f, 0.0f), 0.4f));
}

TEST_CASE("sphereOverlapsRegion rejects sphere clearly outside a box", "[sphere-overlap]")
{
    CollisionRegion region;
    region.boxes.push_back(makeBox(glm::vec2(0.0f, 0.0f), glm::vec2(1.0f, 1.0f), 0.0f, 2.0f));
    REQUIRE_FALSE(sphereOverlapsRegion(region, glm::vec3(5.0f, 2.0f, 0.0f), 0.4f));
}

TEST_CASE("sphereOverlapsRegion detects sphere overlapping a cylinder", "[sphere-overlap]")
{
    CollisionRegion region;
    // Cylinder at origin, radius 1, Y [0, 4].
    region.cylinders.push_back(makeCyl(glm::vec3(0.0f), 1.0f, 2.0f));
    // Sphere center at X=1.3, radius 0.5 -> distance 1.3 between centers,
    // combined radius 1.5 -> overlap.
    REQUIRE(sphereOverlapsRegion(region, glm::vec3(1.3f, 2.0f, 0.0f), 0.5f));
}

TEST_CASE("sphereOverlapsRegion rejects sphere above a cylinder cap", "[sphere-overlap]")
{
    CollisionRegion region;
    // Cylinder Y [0, 4].
    region.cylinders.push_back(makeCyl(glm::vec3(0.0f), 1.0f, 2.0f));
    // Sphere centered at Y=10, radius 0.3 -> well above the cap.
    REQUIRE_FALSE(sphereOverlapsRegion(region, glm::vec3(0.0f, 10.0f, 0.0f), 0.3f));
}
