// Regression tests for the physics primitives the camera pull-in
// pipeline depends on: raycast + sphereOverlap. These were the load-
// bearing queries during the chapel see-outside-through-wall bug
// (2026-05-26): camera-debug.log probes showed that misreading either
// one would leave the pull-in pipeline confused about where walls are.
//
// What these tests DO cover:
//   - raycast against axis-aligned static box bodies (the basic case)
//   - raycast threading through a gap (the chapel doorway case that
//     made the initial diagnosis tricky)
//   - sphereOverlap firing inside, outside, and brushing a wall
//   - body debug name plumbing (so wall identification in logs works)
//
// What they DON'T cover yet:
//   - The camera pull-in math itself (lives inside buildViewProj with
//     static smoothing state + Tunables singleton + scene queries).
//     Extracting that into a pure function is a bigger refactor; when
//     it lands, a camera_pullin_test.cpp file can be added alongside.
//
// Physics state is global (Jolt single-instance per process). Each
// TEST_CASE brings up + tears down the world so leakage between
// cases is impossible.

#include "physics/PhysicsWorld.h"

#include <glm/vec3.hpp>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

namespace ph = engine::physics;
using Catch::Approx;

namespace
{

struct PhysicsFixture
{
    PhysicsFixture()
    {
        // initPhysics is idempotent on repeated calls within a process,
        // but each test still wants a clean body table. We clear bodies
        // by shutting down and re-initializing.
        ph::shutdownPhysics();
        REQUIRE(ph::initPhysics());
    }
    ~PhysicsFixture()
    {
        ph::shutdownPhysics();
    }
};

// Build a 1m-cube axis-aligned static box centered at `center`.
ph::BodyHandle addCube(const glm::vec3& center, const char* name)
{
    return ph::addStaticBox(center, glm::vec3(0.5f, 0.5f, 0.5f), ph::SurfaceTag::Architecture,
                            name);
}

} // namespace

TEST_CASE("raycast against a static box returns hit + distance + body", "[physics][raycast]")
{
    const PhysicsFixture fx;

    const auto wall = addCube(glm::vec3(5.0f, 0.0f, 0.0f), "wall");
    REQUIRE(wall != ph::kInvalidBody);

    SECTION("ray straight at face hits at face distance")
    {
        const auto hit =
            ph::raycast(glm::vec3(0.0f, 0.0f, 0.0f), glm::vec3(1.0f, 0.0f, 0.0f), 10.0f);
        REQUIRE(hit.hit);
        // Cube extends 4.5..5.5 on X — ray from origin hits at 4.5.
        REQUIRE(hit.distance == Approx(4.5f).margin(1e-3f));
        REQUIRE(hit.body == wall);
    }

    SECTION("ray missing the box returns no hit")
    {
        const auto hit =
            ph::raycast(glm::vec3(0.0f, 5.0f, 0.0f), glm::vec3(1.0f, 0.0f, 0.0f), 10.0f);
        REQUIRE_FALSE(hit.hit);
    }

    SECTION("ray pointing away from box returns no hit")
    {
        const auto hit =
            ph::raycast(glm::vec3(0.0f, 0.0f, 0.0f), glm::vec3(-1.0f, 0.0f, 0.0f), 10.0f);
        REQUIRE_FALSE(hit.hit);
    }

    SECTION("max_distance clamp respected")
    {
        // Box is at 4.5m. Cast for only 3m.
        const auto hit =
            ph::raycast(glm::vec3(0.0f, 0.0f, 0.0f), glm::vec3(1.0f, 0.0f, 0.0f), 3.0f);
        REQUIRE_FALSE(hit.hit);
    }
}

TEST_CASE("raycast threads through a gap between bodies", "[physics][raycast]")
{
    // The chapel doorway diagnosis (camera-debug.log on 2026-05-26)
    // turned on this exact behavior: two wall segments with a gap
    // between them; a ray aimed at the gap returns hit=0 even though
    // the surrounding bodies exist. This used to confuse the diagnosis
    // — testing it explicitly so future me doesn't second-guess.
    const PhysicsFixture fx;

    // Two 1m cubes flanking a 1m gap centered on X=0, both at Z=5.
    // Left cube center X=-1, right cube center X=+1. Gap is at X in
    // [-0.5, +0.5]. Both cubes Z=5.
    const auto left = addCube(glm::vec3(-1.0f, 0.0f, 5.0f), "wall_left");
    const auto right = addCube(glm::vec3(+1.0f, 0.0f, 5.0f), "wall_right");
    REQUIRE(left != ph::kInvalidBody);
    REQUIRE(right != ph::kInvalidBody);

    SECTION("ray through the gap returns no hit")
    {
        const auto hit =
            ph::raycast(glm::vec3(0.0f, 0.0f, 0.0f), glm::vec3(0.0f, 0.0f, 1.0f), 10.0f);
        REQUIRE_FALSE(hit.hit);
    }

    SECTION("ray slightly off-axis hits the appropriate cube")
    {
        // Aim at +X side of gap; should hit the right cube.
        const auto hit =
            ph::raycast(glm::vec3(0.7f, 0.0f, 0.0f), glm::vec3(0.0f, 0.0f, 1.0f), 10.0f);
        REQUIRE(hit.hit);
        REQUIRE(hit.body == right);
    }
}

TEST_CASE("sphereOverlap fires at the right boundary", "[physics][sphere]")
{
    const PhysicsFixture fx;

    addCube(glm::vec3(5.0f, 0.0f, 0.0f), "wall");

    SECTION("sphere clearly outside doesn't overlap")
    {
        REQUIRE_FALSE(ph::sphereOverlap(glm::vec3(0.0f, 0.0f, 0.0f), 0.5f));
    }

    SECTION("sphere clearly inside overlaps")
    {
        // Sphere center IS inside the cube.
        REQUIRE(ph::sphereOverlap(glm::vec3(5.0f, 0.0f, 0.0f), 0.5f));
    }

    SECTION("sphere brushing the face overlaps")
    {
        // Sphere center at 4m, radius 0.6m, reaches X=4.6 → overlaps
        // the cube's left face at X=4.5.
        REQUIRE(ph::sphereOverlap(glm::vec3(4.0f, 0.0f, 0.0f), 0.6f));
    }

    SECTION("sphere just shy of the face does not overlap")
    {
        // Sphere center at 4m, radius 0.4m, reaches X=4.4 — gap of 0.1m
        // remains to cube face at X=4.5.
        REQUIRE_FALSE(ph::sphereOverlap(glm::vec3(4.0f, 0.0f, 0.0f), 0.4f));
    }

    SECTION("zero / negative radius never overlaps")
    {
        REQUIRE_FALSE(ph::sphereOverlap(glm::vec3(5.0f, 0.0f, 0.0f), 0.0f));
        REQUIRE_FALSE(ph::sphereOverlap(glm::vec3(5.0f, 0.0f, 0.0f), -1.0f));
    }
}

TEST_CASE("body debug name plumbing round-trips", "[physics][body-name]")
{
    // The camera-debug.log uses bodyDebugName to identify which wall
    // a raycast hit. If the name plumbing breaks, the log becomes
    // useless for diagnosing camera bugs.
    const PhysicsFixture fx;

    const auto a = addCube(glm::vec3(5.0f, 0.0f, 0.0f), "chapel:wall_a");
    const auto b = addCube(glm::vec3(0.0f, 5.0f, 0.0f), "chapel:wall_b");
    const auto unnamed = addCube(glm::vec3(0.0f, 0.0f, 5.0f), nullptr);

    REQUIRE(std::string(ph::bodyDebugName(a)) == "chapel:wall_a");
    REQUIRE(std::string(ph::bodyDebugName(b)) == "chapel:wall_b");
    // nullptr -> ""
    REQUIRE(std::string(ph::bodyDebugName(unnamed)).empty());

    // Hit body carries the name through raycast.
    const auto hit = ph::raycast(glm::vec3(0.0f, 0.0f, 0.0f), glm::vec3(1.0f, 0.0f, 0.0f), 10.0f);
    REQUIRE(hit.hit);
    REQUIRE(std::string(ph::bodyDebugName(hit.body)) == "chapel:wall_a");
}

TEST_CASE("raycast prefers nearest body when multiple are along the ray", "[physics][raycast]")
{
    const PhysicsFixture fx;

    addCube(glm::vec3(5.0f, 0.0f, 0.0f), "near_wall");
    addCube(glm::vec3(10.0f, 0.0f, 0.0f), "far_wall");

    const auto hit = ph::raycast(glm::vec3(0.0f, 0.0f, 0.0f), glm::vec3(1.0f, 0.0f, 0.0f), 20.0f);
    REQUIRE(hit.hit);
    REQUIRE(hit.distance == Approx(4.5f).margin(1e-3f));
    REQUIRE(std::string(ph::bodyDebugName(hit.body)) == "near_wall");
}
