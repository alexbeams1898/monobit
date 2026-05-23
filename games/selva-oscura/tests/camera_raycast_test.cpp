#include "world/Collision.h"

#include <glm/vec3.hpp>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

using selva::world::BoxCollider;
using selva::world::CollisionScene;
using selva::world::CylinderCollider;
using selva::world::raycastScene;
using selva::world::RaycastHit;
using selva::world::sphereOverlapsScene;
using Catch::Approx;

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

TEST_CASE("raycastScene returns no hit on empty scene", "[raycast]")
{
    const CollisionScene scene;
    const RaycastHit r = raycastScene(scene, glm::vec3(0.0f), glm::vec3(1.0f, 0.0f, 0.0f), 10.0f);
    REQUIRE_FALSE(r.hit);
    REQUIRE(r.distance == Approx(10.0f));
}

TEST_CASE("raycastScene degenerate inputs return no hit", "[raycast]")
{
    CollisionScene scene;
    scene.cylinders.push_back(makeCyl(glm::vec3(5.0f, 0.0f, 0.0f), 1.0f, 2.0f));

    SECTION("zero direction")
    {
        const RaycastHit r = raycastScene(scene, glm::vec3(0.0f), glm::vec3(0.0f), 10.0f);
        REQUIRE_FALSE(r.hit);
    }
    SECTION("zero max distance")
    {
        const RaycastHit r = raycastScene(scene, glm::vec3(0.0f), glm::vec3(1.0f, 0.0f, 0.0f), 0.0f);
        REQUIRE_FALSE(r.hit);
    }
}

TEST_CASE("raycastScene hits cylinder side from outside", "[raycast][cylinder]")
{
    CollisionScene scene;
    // Cylinder at x=5, radius 1, spanning Y [0, 4].
    scene.cylinders.push_back(makeCyl(glm::vec3(5.0f, 0.0f, 0.0f), 1.0f, 2.0f));

    // Ray from origin pointing +X at Y=2 (mid-height). Should hit the near
    // side of the cylinder at x = 5 - radius = 4 -> t = 4.
    const RaycastHit r = raycastScene(scene, glm::vec3(0.0f, 2.0f, 0.0f),
                                      glm::vec3(1.0f, 0.0f, 0.0f), 100.0f);
    REQUIRE(r.hit);
    REQUIRE(r.distance == Approx(4.0f));
}

TEST_CASE("raycastScene misses cylinder when ray passes over the top", "[raycast][cylinder]")
{
    CollisionScene scene;
    // Cylinder at x=5, radius 1, spanning Y [0, 4].
    scene.cylinders.push_back(makeCyl(glm::vec3(5.0f, 0.0f, 0.0f), 1.0f, 2.0f));

    // Ray from origin Y=10 pointing +X stays at Y=10 forever; well above
    // the cylinder top of Y=4.
    const RaycastHit r = raycastScene(scene, glm::vec3(0.0f, 10.0f, 0.0f),
                                      glm::vec3(1.0f, 0.0f, 0.0f), 100.0f);
    REQUIRE_FALSE(r.hit);
}

TEST_CASE("raycastScene hits cylinder cap from above", "[raycast][cylinder]")
{
    CollisionScene scene;
    // Cylinder at origin, radius 1, spanning Y [0, 4].
    scene.cylinders.push_back(makeCyl(glm::vec3(0.0f, 0.0f, 0.0f), 1.0f, 2.0f));

    // Ray straight down through the cylinder's axis from Y=10. Top cap at
    // Y=4 -> t = 6.
    const RaycastHit r = raycastScene(scene, glm::vec3(0.0f, 10.0f, 0.0f),
                                      glm::vec3(0.0f, -1.0f, 0.0f), 100.0f);
    REQUIRE(r.hit);
    REQUIRE(r.distance == Approx(6.0f));
}

TEST_CASE("raycastScene misses cylinder when ray passes outside the radius", "[raycast][cylinder]")
{
    CollisionScene scene;
    scene.cylinders.push_back(makeCyl(glm::vec3(5.0f, 0.0f, 0.0f), 1.0f, 2.0f));

    // Ray parallel to +X but offset in Z by 5m; cylinder radius is 1, so
    // miss.
    const RaycastHit r = raycastScene(scene, glm::vec3(0.0f, 2.0f, 5.0f),
                                      glm::vec3(1.0f, 0.0f, 0.0f), 100.0f);
    REQUIRE_FALSE(r.hit);
}

TEST_CASE("raycastScene respects max_distance", "[raycast]")
{
    CollisionScene scene;
    scene.cylinders.push_back(makeCyl(glm::vec3(20.0f, 0.0f, 0.0f), 1.0f, 2.0f));

    // Cylinder at x=20, ray would hit at t=19. With max_distance=10, miss.
    const RaycastHit r = raycastScene(scene, glm::vec3(0.0f, 2.0f, 0.0f),
                                      glm::vec3(1.0f, 0.0f, 0.0f), 10.0f);
    REQUIRE_FALSE(r.hit);
    REQUIRE(r.distance == Approx(10.0f));
}

TEST_CASE("raycastScene normalizes non-unit direction", "[raycast]")
{
    CollisionScene scene;
    scene.cylinders.push_back(makeCyl(glm::vec3(5.0f, 0.0f, 0.0f), 1.0f, 2.0f));

    // Same geometry as the side-hit case, but direction vector scaled by 3.
    // Distance is reported in world units, so the answer should be the same
    // 4m regardless of input magnitude.
    const RaycastHit r = raycastScene(scene, glm::vec3(0.0f, 2.0f, 0.0f),
                                      glm::vec3(3.0f, 0.0f, 0.0f), 100.0f);
    REQUIRE(r.hit);
    REQUIRE(r.distance == Approx(4.0f));
}

TEST_CASE("raycastScene hits AABB front face", "[raycast][box]")
{
    CollisionScene scene;
    // Box centered at (5, 0) XZ, 1m half-width in X and Z, spanning Y [0, 4].
    scene.boxes.push_back(makeBox(glm::vec2(5.0f, 0.0f), glm::vec2(1.0f, 1.0f), 0.0f, 2.0f));

    // Ray from origin +X at mid-height. Hits near face at x = 4 -> t = 4.
    const RaycastHit r = raycastScene(scene, glm::vec3(0.0f, 2.0f, 0.0f),
                                      glm::vec3(1.0f, 0.0f, 0.0f), 100.0f);
    REQUIRE(r.hit);
    REQUIRE(r.distance == Approx(4.0f));
}

TEST_CASE("raycastScene misses AABB when ray clears the top", "[raycast][box]")
{
    CollisionScene scene;
    scene.boxes.push_back(makeBox(glm::vec2(5.0f, 0.0f), glm::vec2(1.0f, 1.0f), 0.0f, 2.0f));

    // Ray from Y=10 pointing +X stays above box top (Y=4) the whole way.
    const RaycastHit r = raycastScene(scene, glm::vec3(0.0f, 10.0f, 0.0f),
                                      glm::vec3(1.0f, 0.0f, 0.0f), 100.0f);
    REQUIRE_FALSE(r.hit);
}

TEST_CASE("raycastScene misses AABB when ray parallel to slab and outside", "[raycast][box]")
{
    CollisionScene scene;
    scene.boxes.push_back(makeBox(glm::vec2(5.0f, 0.0f), glm::vec2(1.0f, 1.0f), 0.0f, 2.0f));

    // Ray parallel to +X but Z=5 (outside the Z-slab [-1, 1] of the box).
    const RaycastHit r = raycastScene(scene, glm::vec3(0.0f, 2.0f, 5.0f),
                                      glm::vec3(1.0f, 0.0f, 0.0f), 100.0f);
    REQUIRE_FALSE(r.hit);
}

TEST_CASE("raycastScene picks nearest among multiple colliders", "[raycast]")
{
    CollisionScene scene;
    // Cylinder far at x=20, box near at x=5. Ray hits box first at t=4.
    scene.cylinders.push_back(makeCyl(glm::vec3(20.0f, 0.0f, 0.0f), 1.0f, 2.0f));
    scene.boxes.push_back(makeBox(glm::vec2(5.0f, 0.0f), glm::vec2(1.0f, 1.0f), 0.0f, 2.0f));

    const RaycastHit r = raycastScene(scene, glm::vec3(0.0f, 2.0f, 0.0f),
                                      glm::vec3(1.0f, 0.0f, 0.0f), 100.0f);
    REQUIRE(r.hit);
    REQUIRE(r.distance == Approx(4.0f));
}

TEST_CASE("raycastScene diagonal ray hits cylinder at expected distance", "[raycast]")
{
    CollisionScene scene;
    // Cylinder centered at (5, 5) XZ, radius 1, tall.
    scene.cylinders.push_back(makeCyl(glm::vec3(5.0f, 0.0f, 5.0f), 1.0f, 2.0f));

    // Diagonal ray at 45deg in XZ. Cylinder center is at distance
    // sqrt(50) ~= 7.071. Near surface at radius 1 inside -> t ~= 6.071.
    const RaycastHit r = raycastScene(scene, glm::vec3(0.0f, 2.0f, 0.0f),
                                      glm::vec3(1.0f, 0.0f, 1.0f), 100.0f);
    REQUIRE(r.hit);
    REQUIRE(r.distance == Approx(6.0710678f).margin(0.001f));
}

TEST_CASE("raycastScene ignores collider when ray points away from it", "[raycast]")
{
    CollisionScene scene;
    // Wall just to the +X of the origin: AABB at X=[0.5, 1.5].
    scene.boxes.push_back(makeBox(glm::vec2(1.0f, 0.0f), glm::vec2(0.5f, 0.5f), 0.0f, 2.0f));

    // Origin in open ground at X=0 (not inside the wall). Ray fires
    // in -X, AWAY from the wall. No hit.
    const RaycastHit r = raycastScene(scene, glm::vec3(0.0f, 1.0f, 0.0f),
                                      glm::vec3(-1.0f, 0.0f, 0.0f), 6.0f);
    REQUIRE_FALSE(r.hit);
}

TEST_CASE("raycastScene returns t=0 when origin is inside a box", "[raycast]")
{
    CollisionScene scene;
    // Wall at X=[0.5, 1.5].
    scene.boxes.push_back(makeBox(glm::vec2(1.0f, 0.0f), glm::vec2(0.5f, 0.5f), 0.0f, 2.0f));

    // Origin INSIDE the wall: pathological camera-inside-wall case.
    // Reports hit at t=0 so the caller collapses the camera onto the
    // player rather than rendering from inside the wall.
    const RaycastHit r = raycastScene(scene, glm::vec3(1.0f, 1.0f, 0.0f),
                                      glm::vec3(1.0f, 0.0f, 0.0f), 6.0f);
    REQUIRE(r.hit);
    REQUIRE(r.distance == Approx(0.0f));
}

TEST_CASE("sphereOverlapsScene returns false on empty scene", "[sphere-overlap]")
{
    const CollisionScene scene;
    REQUIRE_FALSE(sphereOverlapsScene(scene, glm::vec3(0.0f), 1.0f));
}

TEST_CASE("sphereOverlapsScene returns false for zero radius", "[sphere-overlap]")
{
    CollisionScene scene;
    scene.boxes.push_back(makeBox(glm::vec2(0.0f, 0.0f), glm::vec2(1.0f, 1.0f), 0.0f, 2.0f));
    // Origin inside the box, but zero radius -> degenerate, no overlap.
    REQUIRE_FALSE(sphereOverlapsScene(scene, glm::vec3(0.0f, 1.0f, 0.0f), 0.0f));
}

TEST_CASE("sphereOverlapsScene detects sphere inside a box", "[sphere-overlap]")
{
    CollisionScene scene;
    // Box at origin, half-extent 1m in XZ, Y [0, 4].
    scene.boxes.push_back(makeBox(glm::vec2(0.0f, 0.0f), glm::vec2(1.0f, 1.0f), 0.0f, 2.0f));
    REQUIRE(sphereOverlapsScene(scene, glm::vec3(0.0f, 2.0f, 0.0f), 0.3f));
}

TEST_CASE("sphereOverlapsScene detects sphere brushing a box edge", "[sphere-overlap]")
{
    CollisionScene scene;
    scene.boxes.push_back(makeBox(glm::vec2(0.0f, 0.0f), glm::vec2(1.0f, 1.0f), 0.0f, 2.0f));
    // Sphere center at X=1.3, radius 0.4 -> overlaps the +X face at X=1.0.
    REQUIRE(sphereOverlapsScene(scene, glm::vec3(1.3f, 2.0f, 0.0f), 0.4f));
}

TEST_CASE("sphereOverlapsScene rejects sphere clearly outside a box", "[sphere-overlap]")
{
    CollisionScene scene;
    scene.boxes.push_back(makeBox(glm::vec2(0.0f, 0.0f), glm::vec2(1.0f, 1.0f), 0.0f, 2.0f));
    REQUIRE_FALSE(sphereOverlapsScene(scene, glm::vec3(5.0f, 2.0f, 0.0f), 0.4f));
}

TEST_CASE("sphereOverlapsScene detects sphere overlapping a cylinder", "[sphere-overlap]")
{
    CollisionScene scene;
    // Cylinder at origin, radius 1, Y [0, 4].
    scene.cylinders.push_back(makeCyl(glm::vec3(0.0f), 1.0f, 2.0f));
    // Sphere center at X=1.3, radius 0.5 -> distance 1.3 between centers,
    // combined radius 1.5 -> overlap.
    REQUIRE(sphereOverlapsScene(scene, glm::vec3(1.3f, 2.0f, 0.0f), 0.5f));
}

TEST_CASE("sphereOverlapsScene rejects sphere above a cylinder cap", "[sphere-overlap]")
{
    CollisionScene scene;
    // Cylinder Y [0, 4].
    scene.cylinders.push_back(makeCyl(glm::vec3(0.0f), 1.0f, 2.0f));
    // Sphere centered at Y=10, radius 0.3 -> well above the cap.
    REQUIRE_FALSE(sphereOverlapsScene(scene, glm::vec3(0.0f, 10.0f, 0.0f), 0.3f));
}
