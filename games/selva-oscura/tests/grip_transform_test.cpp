// Tests for buildGripMatrix: the per-weapon grip transform applied
// after the right-hand bone's world matrix. Verifies composition
// order T(offset) * R_z * R_y * R_x * S(scale) by checking the
// resulting matrix's effect on canonical points.

#include "ecs/Items.h"
#include "render/EquippedWeapon.h"

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

using Catch::Approx;

namespace
{

bool vec3Approx(const glm::vec3& a, const glm::vec3& b, float eps = 1e-4f)
{
    return glm::all(glm::lessThan(glm::abs(a - b), glm::vec3(eps)));
}

} // namespace

TEST_CASE("buildGripMatrix returns identity for default ItemDef", "[grip][matrix]")
{
    // Default ItemDef: all grip offsets/rotations zero, grip_scale = 1.0.
    // Identity transform maps (1,2,3) to (1,2,3).
    engine::ecs::ItemDef def;
    REQUIRE(def.grip_scale == Approx(1.0f));
    const glm::mat4 m = selva::render::buildGripMatrix(def);
    const glm::vec3 p = glm::vec3(m * glm::vec4(1.0f, 2.0f, 3.0f, 1.0f));
    REQUIRE(vec3Approx(p, glm::vec3(1.0f, 2.0f, 3.0f)));
}

TEST_CASE("buildGripMatrix applies translation only", "[grip][matrix][translate]")
{
    engine::ecs::ItemDef def;
    def.grip_offset_x = 0.1f;
    def.grip_offset_y = 0.2f;
    def.grip_offset_z = 0.3f;
    const glm::mat4 m = selva::render::buildGripMatrix(def);
    // Origin in object-space lands at the offset in joint-space.
    const glm::vec3 origin = glm::vec3(m * glm::vec4(0.0f, 0.0f, 0.0f, 1.0f));
    REQUIRE(vec3Approx(origin, glm::vec3(0.1f, 0.2f, 0.3f)));
    // Other points get the same offset.
    const glm::vec3 p = glm::vec3(m * glm::vec4(1.0f, 0.0f, 0.0f, 1.0f));
    REQUIRE(vec3Approx(p, glm::vec3(1.1f, 0.2f, 0.3f)));
}

TEST_CASE("buildGripMatrix applies uniform scale", "[grip][matrix][scale]")
{
    engine::ecs::ItemDef def;
    def.grip_scale = 2.0f;
    const glm::mat4 m = selva::render::buildGripMatrix(def);
    const glm::vec3 p = glm::vec3(m * glm::vec4(1.0f, 2.0f, 3.0f, 1.0f));
    REQUIRE(vec3Approx(p, glm::vec3(2.0f, 4.0f, 6.0f)));
}

TEST_CASE("buildGripMatrix rotates 90 deg about Z axis", "[grip][matrix][rotate]")
{
    // Rotating (1,0,0) by +90 around Z should produce (0,1,0).
    engine::ecs::ItemDef def;
    def.grip_rot_deg_z = 90.0f;
    const glm::mat4 m = selva::render::buildGripMatrix(def);
    const glm::vec3 p = glm::vec3(m * glm::vec4(1.0f, 0.0f, 0.0f, 1.0f));
    REQUIRE(vec3Approx(p, glm::vec3(0.0f, 1.0f, 0.0f)));
}

TEST_CASE("buildGripMatrix rotates 90 deg about Y axis", "[grip][matrix][rotate]")
{
    // Rotating (1,0,0) by +90 around Y should produce (0,0,-1).
    engine::ecs::ItemDef def;
    def.grip_rot_deg_y = 90.0f;
    const glm::mat4 m = selva::render::buildGripMatrix(def);
    const glm::vec3 p = glm::vec3(m * glm::vec4(1.0f, 0.0f, 0.0f, 1.0f));
    REQUIRE(vec3Approx(p, glm::vec3(0.0f, 0.0f, -1.0f)));
}

TEST_CASE("buildGripMatrix rotates 90 deg about X axis", "[grip][matrix][rotate]")
{
    // Rotating (0,1,0) by +90 around X should produce (0,0,1).
    engine::ecs::ItemDef def;
    def.grip_rot_deg_x = 90.0f;
    const glm::mat4 m = selva::render::buildGripMatrix(def);
    const glm::vec3 p = glm::vec3(m * glm::vec4(0.0f, 1.0f, 0.0f, 1.0f));
    REQUIRE(vec3Approx(p, glm::vec3(0.0f, 0.0f, 1.0f)));
}

TEST_CASE("buildGripMatrix translate-after-scale composition: scale applies inside translation",
          "[grip][matrix][compose]")
{
    // Composition is T(offset) * R * S(scale).
    // Object-space point (1,0,0) gets: scaled to (2,0,0), then translated.
    engine::ecs::ItemDef def;
    def.grip_offset_x = 0.5f;
    def.grip_scale = 2.0f;
    const glm::mat4 m = selva::render::buildGripMatrix(def);
    const glm::vec3 p = glm::vec3(m * glm::vec4(1.0f, 0.0f, 0.0f, 1.0f));
    REQUIRE(vec3Approx(p, glm::vec3(2.5f, 0.0f, 0.0f)));
}

TEST_CASE("buildGripMatrix lead-mace-like grip transform produces sensible result",
          "[grip][matrix][realistic]")
{
    // Lead mace's locked values from lead_mace.json. Sanity-check that
    // the composed transform doesn't explode and origin lands at the
    // authored offset.
    engine::ecs::ItemDef def;
    def.grip_offset_x = -0.065f;
    def.grip_offset_y = 0.030f;
    def.grip_offset_z = 0.015f;
    def.grip_rot_deg_x = 12.5f;
    def.grip_rot_deg_y = -6.2f;
    def.grip_rot_deg_z = -67.4f;
    def.grip_scale = 1.0f;
    const glm::mat4 m = selva::render::buildGripMatrix(def);
    // Origin should land at the offset (translation is the last column).
    const glm::vec3 origin = glm::vec3(m * glm::vec4(0.0f, 0.0f, 0.0f, 1.0f));
    REQUIRE(vec3Approx(origin, glm::vec3(-0.065f, 0.030f, 0.015f)));
    // The matrix should preserve length for a point at the origin's
    // distance (rotation + uniform-scale-1 is rigid). Length of the
    // x-axis basis vector should be 1.
    const glm::vec3 x_axis = glm::vec3(m[0]);
    REQUIRE(glm::length(x_axis) == Approx(1.0f));
}
