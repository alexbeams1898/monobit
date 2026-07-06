// Locks the hip-delta + body_scale two-sides contract per
// feedback_hip_delta_two_sides.md. The visible mesh renders at
// body_scale of bind-pose size (buildActorModelMatrix scales the
// model matrix uniformly). If the clip-authored hip delta isn't
// scaled by the same factor when applied to world velocity, the
// world moves faster than the visible feet and the actor slides.
// This is the burned-twice-already regression class; pin it
// numerically.

#include "gameplay/Actor.h"
#include "gameplay/Appearance.h"

#include <glm/vec3.hpp>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

using Catch::Approx;
using selva::gameplay::hipDeltaVelocityContribution;

TEST_CASE("hip-delta velocity scales linearly with body_scale",
          "[appearance][hip-delta][two-sides]")
{
    // Forward axis = -Z in actor-local space (the codebase's bind
    // convention). With yaw=0 the world-Z velocity should be POSITIVE
    // for a forward step. The exact sign matters less than the linear
    // scaling with body_scale.
    const glm::vec3 hip(0.0f, 0.0f, -0.1f);
    const float yaw = 0.0f;
    const float dt = 1.0f / 60.0f;

    const auto v_unit = hipDeltaVelocityContribution(hip, yaw, dt, 1.0f, 1.0f);
    const auto v_half = hipDeltaVelocityContribution(hip, yaw, dt, 1.0f, 0.5f);
    const auto v_big = hipDeltaVelocityContribution(hip, yaw, dt, 1.0f, 1.5f);

    REQUIRE(v_half.x == Approx(v_unit.x * 0.5f));
    REQUIRE(v_half.y == Approx(v_unit.y * 0.5f));
    REQUIRE(v_big.x == Approx(v_unit.x * 1.5f));
    REQUIRE(v_big.y == Approx(v_unit.y * 1.5f));
}

TEST_CASE("hip-delta scaling composes multiplicatively with hip_delta_scale",
          "[appearance][hip-delta]")
{
    // The two scalars (hip_delta_scale for jump-style one-shots,
    // body_scale for body size) MUST multiply, not stack additively
    // or override. Future-me: if you "simplify" this by picking one,
    // the other surface breaks silently.
    const glm::vec3 hip(0.0f, 0.0f, -0.1f);
    const float yaw = 0.0f;
    const float dt = 1.0f / 60.0f;

    const auto v_baseline = hipDeltaVelocityContribution(hip, yaw, dt, 1.0f, 1.0f);
    const auto v_scaled = hipDeltaVelocityContribution(hip, yaw, dt, 2.0f, 0.5f);

    // 2.0 * 0.5 = 1.0, so the composed scaling should equal the
    // baseline.
    REQUIRE(v_scaled.x == Approx(v_baseline.x));
    REQUIRE(v_scaled.y == Approx(v_baseline.y));
}

TEST_CASE("hip-delta is zero when timestep is zero or near-zero", "[appearance][hip-delta]")
{
    const glm::vec3 hip(0.0f, 0.0f, -0.1f);
    const auto v = hipDeltaVelocityContribution(hip, 0.0f, 0.0f, 1.0f, 1.0f);
    REQUIRE(v.x == Approx(0.0f));
    REQUIRE(v.y == Approx(0.0f));
}

TEST_CASE("hip-delta is zero when no motion authored on the clip", "[appearance][hip-delta]")
{
    const glm::vec3 hip(0.0f, 0.0f, 0.0f);
    const auto v = hipDeltaVelocityContribution(hip, 0.5f, 1.0f / 60.0f, 1.0f, 1.0f);
    REQUIRE(v.x == Approx(0.0f));
    REQUIRE(v.y == Approx(0.0f));
}

TEST_CASE("default-constructed Appearance has body_scale 1.0", "[appearance][defaults]")
{
    selva::gameplay::Appearance a;
    REQUIRE(a.body_scale == Approx(1.0f));
}

TEST_CASE("default-constructed Appearance has neutral white color", "[appearance][defaults]")
{
    selva::gameplay::Appearance a;
    REQUIRE(a.color.x == Approx(1.0f));
    REQUIRE(a.color.y == Approx(1.0f));
    REQUIRE(a.color.z == Approx(1.0f));
}

TEST_CASE("default-constructed Appearance has head_scale 1.0", "[appearance][defaults]")
{
    selva::gameplay::Appearance a;
    REQUIRE(a.head_scale == Approx(1.0f));
}

TEST_CASE("default-constructed Appearance has arm_scale + leg_scale 1.0", "[appearance][defaults]")
{
    selva::gameplay::Appearance a;
    REQUIRE(a.arm_scale == Approx(1.0f));
    REQUIRE(a.leg_scale == Approx(1.0f));
}

TEST_CASE("default-constructed Appearance has torso_scale 1.0", "[appearance][defaults]")
{
    selva::gameplay::Appearance a;
    REQUIRE(a.torso_scale == Approx(1.0f));
}

TEST_CASE("empty appearance_path returns default Appearance", "[appearance][loader]")
{
    const auto a = selva::gameplay::loadAppearance(std::string{});
    REQUIRE(a.body_scale == Approx(1.0f));
}

TEST_CASE("missing appearance file falls back to default + logs", "[appearance][loader]")
{
    const auto a =
        selva::gameplay::loadAppearance("config/characters/__definitely_does_not_exist__.json");
    REQUIRE(a.body_scale == Approx(1.0f));
}
