#include "anim/AnimationDriver.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <glm/gtc/constants.hpp>

using selva::anim::AnimDriverInput;
using selva::anim::AnimDriverOutput;
using selva::anim::AnimState;
using selva::anim::evaluate;

namespace
{
constexpr float kEps = 1e-4f;
}

// ---------------------------------------------------------------------------
// Identity states — driver returns identity offsets.
// ---------------------------------------------------------------------------

TEST_CASE("AnimationDriver: None state returns identity", "[anim][driver]")
{
    AnimDriverInput in;
    in.state = AnimState::None;
    in.phase = 0.5f;
    const AnimDriverOutput out = evaluate(in);

    REQUIRE(out.translation.x == Catch::Approx(0.0f).margin(kEps));
    REQUIRE(out.translation.y == Catch::Approx(0.0f).margin(kEps));
    REQUIRE(out.translation.z == Catch::Approx(0.0f).margin(kEps));
    REQUIRE(out.pitch_offset == Catch::Approx(0.0f).margin(kEps));
    REQUIRE(out.yaw_offset == Catch::Approx(0.0f).margin(kEps));
    REQUIRE(out.roll_offset == Catch::Approx(0.0f).margin(kEps));
}

TEST_CASE("AnimationDriver: Locomotion state returns identity (today)", "[anim][driver]")
{
    AnimDriverInput in;
    in.state = AnimState::Locomotion;
    in.phase = 0.7f;
    const AnimDriverOutput out = evaluate(in);
    REQUIRE(out.translation == glm::vec3(0.0f));
    REQUIRE(out.pitch_offset == Catch::Approx(0.0f).margin(kEps));
}

TEST_CASE("AnimationDriver: DodgeRecover state returns identity", "[anim][driver]")
{
    AnimDriverInput in;
    in.state = AnimState::DodgeRecover;
    in.phase = 0.3f;
    const AnimDriverOutput out = evaluate(in);
    REQUIRE(out.translation == glm::vec3(0.0f));
    REQUIRE(out.pitch_offset == Catch::Approx(0.0f).margin(kEps));
}

// ---------------------------------------------------------------------------
// DodgeRoll — translation curve, hop arc, tumble.
// ---------------------------------------------------------------------------

TEST_CASE("AnimationDriver: DodgeRoll translation is zero at phase 0", "[anim][driver][dodge]")
{
    AnimDriverInput in;
    in.state = AnimState::DodgeRoll;
    in.phase = 0.0f;
    in.params.dodge_dir = glm::vec3(0.0f, 0.0f, -1.0f);
    const AnimDriverOutput out = evaluate(in);

    REQUIRE(out.translation.x == Catch::Approx(0.0f).margin(kEps));
    // Y also 0 — hop arc is 4*0*1 = 0 at phase 0.
    REQUIRE(out.translation.y == Catch::Approx(0.0f).margin(kEps));
    REQUIRE(out.translation.z == Catch::Approx(0.0f).margin(kEps));
    REQUIRE(out.pitch_offset == Catch::Approx(0.0f).margin(kEps));
}

TEST_CASE("AnimationDriver: DodgeRoll lands at full distance at phase 1", "[anim][driver][dodge]")
{
    AnimDriverInput in;
    in.state = AnimState::DodgeRoll;
    in.phase = 1.0f;
    in.params.dodge_dir = glm::vec3(0.0f, 0.0f, -1.0f);
    const AnimDriverOutput out = evaluate(in);

    // Roll distance is documented as 3.5; ease-out cumulative is 1.0 at t=1.
    REQUIRE(out.translation.z == Catch::Approx(-3.5f).margin(kEps));
    REQUIRE(out.translation.x == Catch::Approx(0.0f).margin(kEps));
    // Hop arc is 4*1*0 = 0 at phase 1 — character lands.
    REQUIRE(out.translation.y == Catch::Approx(0.0f).margin(kEps));
    // Tumble at phase 1 = -2pi * revs = full revolution backward.
    REQUIRE(out.pitch_offset == Catch::Approx(-glm::two_pi<float>()).margin(kEps));
}

TEST_CASE("AnimationDriver: DodgeRoll hop peaks at phase 0.5", "[anim][driver][dodge]")
{
    AnimDriverInput in;
    in.state = AnimState::DodgeRoll;
    in.phase = 0.5f;
    const AnimDriverOutput out = evaluate(in);
    // Hop peak documented as 0.45.
    REQUIRE(out.translation.y == Catch::Approx(0.45f).margin(kEps));
}

TEST_CASE("AnimationDriver: DodgeRoll tumble is forward (negative pitch)", "[anim][driver][dodge]")
{
    AnimDriverInput in;
    in.state = AnimState::DodgeRoll;
    in.phase = 0.25f;
    const AnimDriverOutput out = evaluate(in);
    // Negative around local +X = top tilts toward direction of motion.
    // Critical regression guard: this was the bug ("rolls backward").
    REQUIRE(out.pitch_offset < 0.0f);
}

TEST_CASE("AnimationDriver: DodgeRoll ease-out — past halfway by phase 0.5",
          "[anim][driver][dodge]")
{
    AnimDriverInput in;
    in.state = AnimState::DodgeRoll;
    in.phase = 0.5f;
    in.params.dodge_dir = glm::vec3(0.0f, 0.0f, -1.0f);
    const AnimDriverOutput out = evaluate(in);
    // Ease-out (1 - (1-t)^2) at t=0.5 is 0.75 — we've covered 75% of the
    // distance halfway through. Linear would be 50%. This is the
    // signature of ease-out (push-off + decel), not linear translation.
    const float distance_covered = -out.translation.z; // -Z means we moved -Z
    REQUIRE(distance_covered == Catch::Approx(3.5f * 0.75f).margin(kEps));
}

TEST_CASE("AnimationDriver: DodgeRoll respects dodge_dir parameter", "[anim][driver][dodge]")
{
    AnimDriverInput in;
    in.state = AnimState::DodgeRoll;
    in.phase = 1.0f;
    in.params.dodge_dir = glm::vec3(1.0f, 0.0f, 0.0f); // strafe right
    const AnimDriverOutput out = evaluate(in);
    REQUIRE(out.translation.x == Catch::Approx(3.5f).margin(kEps));
    REQUIRE(out.translation.z == Catch::Approx(0.0f).margin(kEps));
}

// ---------------------------------------------------------------------------
// DodgeBackstep — like roll, but no hop, no tumble, shorter distance.
// ---------------------------------------------------------------------------

TEST_CASE("AnimationDriver: DodgeBackstep travels backstep distance, no hop",
          "[anim][driver][dodge]")
{
    AnimDriverInput in;
    in.state = AnimState::DodgeBackstep;
    in.phase = 1.0f;
    in.params.dodge_dir = glm::vec3(0.0f, 0.0f, 1.0f); // backward (away from -Z facing)
    const AnimDriverOutput out = evaluate(in);

    REQUIRE(out.translation.z == Catch::Approx(2.0f).margin(kEps));
    // No hop: Y is 0 throughout.
    REQUIRE(out.translation.y == Catch::Approx(0.0f).margin(kEps));
    // No tumble.
    REQUIRE(out.pitch_offset == Catch::Approx(0.0f).margin(kEps));
}

TEST_CASE("AnimationDriver: DodgeBackstep starts at zero", "[anim][driver][dodge]")
{
    AnimDriverInput in;
    in.state = AnimState::DodgeBackstep;
    in.phase = 0.0f;
    const AnimDriverOutput out = evaluate(in);
    REQUIRE(out.translation == glm::vec3(0.0f));
}

// ---------------------------------------------------------------------------
// Phase clamping — out-of-range inputs don't NaN through.
// ---------------------------------------------------------------------------

TEST_CASE("AnimationDriver: phase below 0 clamps to 0", "[anim][driver][robustness]")
{
    AnimDriverInput in;
    in.state = AnimState::DodgeRoll;
    in.phase = -0.5f;
    const AnimDriverOutput out = evaluate(in);
    REQUIRE(out.translation == glm::vec3(0.0f));
    REQUIRE(out.pitch_offset == Catch::Approx(0.0f).margin(kEps));
}

TEST_CASE("AnimationDriver: phase above 1 clamps to 1", "[anim][driver][robustness]")
{
    AnimDriverInput in;
    in.state = AnimState::DodgeRoll;
    in.phase = 1.5f;
    in.params.dodge_dir = glm::vec3(0.0f, 0.0f, -1.0f);
    const AnimDriverOutput out = evaluate(in);
    // Output should match phase=1, not run past the end.
    REQUIRE(out.translation.z == Catch::Approx(-3.5f).margin(kEps));
}
