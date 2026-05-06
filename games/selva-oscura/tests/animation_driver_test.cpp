#include "Tunables.h"
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
constexpr float kEps = 1e-3f;

// Reset Tunables to a known set of values for tests that assert specific
// numeric outcomes. Called from each numeric test so they don't couple to
// whatever's in the (mutable, file-loaded) global at test time. Lean is
// disabled by default here (angle=0) so tumble-only tests aren't polluted
// by the lean's pitch contribution; tests that need the lean enable it
// explicitly.
void resetTunablesForTests()
{
    auto& tun = selva::tuning::current();
    tun.roll_distance = 3.5f;
    tun.roll_hop_height = 0.45f;
    tun.roll_hop_start = 0.0f;
    tun.roll_hop_end = 0.55f;
    tun.roll_tumble_revs = 1.0f;
    tun.roll_tumble_ease = 2.0f;
    tun.roll_tumble_start = 0.15f;
    tun.roll_tumble_end = 0.85f;
    tun.roll_translation_start = 0.0f;
    tun.roll_translation_end = 0.85f;
    tun.roll_lean_angle = 0.0f;
    tun.roll_lean_start = 0.0f;
    tun.roll_lean_end = 0.55f;
    tun.roll_lean_peak_phase = 0.45f;
    tun.backstep_distance = 2.0f;
}
} // namespace

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
    resetTunablesForTests();
    AnimDriverInput in;
    in.state = AnimState::DodgeRoll;
    in.phase = 0.0f;
    in.params.dodge_dir = glm::vec3(0.0f, 0.0f, -1.0f);
    const AnimDriverOutput out = evaluate(in);

    REQUIRE(out.translation.x == Catch::Approx(0.0f).margin(kEps));
    REQUIRE(out.translation.y == Catch::Approx(0.0f).margin(kEps));
    REQUIRE(out.translation.z == Catch::Approx(0.0f).margin(kEps));
    REQUIRE(out.pitch_offset == Catch::Approx(0.0f).margin(kEps));
}

TEST_CASE("AnimationDriver: DodgeRoll lands at full distance at phase 1", "[anim][driver][dodge]")
{
    resetTunablesForTests();
    AnimDriverInput in;
    in.state = AnimState::DodgeRoll;
    in.phase = 1.0f;
    in.params.dodge_dir = glm::vec3(0.0f, 0.0f, -1.0f);
    const AnimDriverOutput out = evaluate(in);

    // Eased ease-in-out cumulative is 1.0 at t=1 regardless of ease power.
    REQUIRE(out.translation.z == Catch::Approx(-3.5f).margin(kEps));
    REQUIRE(out.translation.x == Catch::Approx(0.0f).margin(kEps));
    // Hop arc returns to 0 at t=1 (character lands).
    REQUIRE(out.translation.y == Catch::Approx(0.0f).margin(kEps));
    // Eased tumble is also 1.0 at t=1, so total rotation is one full
    // revolution backward (negative pitch = forward tip).
    REQUIRE(out.pitch_offset == Catch::Approx(-glm::two_pi<float>()).margin(kEps));
}

TEST_CASE("AnimationDriver: DodgeRoll hop peaks at the window midpoint (gravity arc)",
          "[anim][driver][dodge]")
{
    resetTunablesForTests();
    // Hop uses a gravity arc — peak is fixed at the temporal midpoint of
    // the hop window. With the default [0.0, 0.55] window, midpoint is
    // global phase 0.275.
    AnimDriverInput in;
    in.state = AnimState::DodgeRoll;
    in.phase = 0.275f;
    const AnimDriverOutput out = evaluate(in);
    REQUIRE(out.translation.y == Catch::Approx(0.45f).margin(kEps));
}

TEST_CASE("AnimationDriver: DodgeRoll hop is symmetric in time around its midpoint",
          "[anim][driver][dodge]")
{
    resetTunablesForTests();
    // Sample two equidistant points around the midpoint — gravity arc is
    // symmetric, so they should produce identical Y.
    AnimDriverInput in;
    in.state = AnimState::DodgeRoll;
    // Hop window [0.0, 0.55], midpoint 0.275. Sample 0.275 - 0.10 and 0.275 + 0.10.
    in.phase = 0.175f;
    const float y_pre = evaluate(in).translation.y;
    in.phase = 0.375f;
    const float y_post = evaluate(in).translation.y;
    REQUIRE(y_pre == Catch::Approx(y_post).margin(kEps));
    REQUIRE(y_pre > 0.0f);
}

TEST_CASE("AnimationDriver: DodgeRoll hop is zero outside its window", "[anim][driver][dodge]")
{
    resetTunablesForTests();
    // Hop window default is [0.0, 0.55]. At phase 0.7 we're well past the end.
    AnimDriverInput in;
    in.state = AnimState::DodgeRoll;
    in.phase = 0.7f;
    const AnimDriverOutput out = evaluate(in);
    REQUIRE(out.translation.y == Catch::Approx(0.0f).margin(kEps));
}

TEST_CASE("AnimationDriver: DodgeRoll tumble is zero before its window", "[anim][driver][dodge]")
{
    resetTunablesForTests();
    // Tumble window default is [0.15, 0.85]. At phase 0.10 we haven't started.
    AnimDriverInput in;
    in.state = AnimState::DodgeRoll;
    in.phase = 0.10f;
    const AnimDriverOutput out = evaluate(in);
    REQUIRE(out.pitch_offset == Catch::Approx(0.0f).margin(kEps));
}

TEST_CASE("AnimationDriver: DodgeRoll tumble holds at full revolutions after window end",
          "[anim][driver][dodge]")
{
    resetTunablesForTests();
    // Tumble window default ends at 0.85. At phase 0.95 the tumble has settled.
    AnimDriverInput in;
    in.state = AnimState::DodgeRoll;
    in.phase = 0.95f;
    const AnimDriverOutput out = evaluate(in);
    // Full revolution backward (negative pitch = forward tip).
    REQUIRE(out.pitch_offset == Catch::Approx(-glm::two_pi<float>()).margin(kEps));
}

TEST_CASE("AnimationDriver: DodgeRoll lean is zero outside its window",
          "[anim][driver][dodge][lean]")
{
    auto& tun = selva::tuning::current();
    resetTunablesForTests();
    tun.roll_lean_angle = 0.4f; // enable lean
    tun.roll_lean_start = 0.20f;
    tun.roll_lean_end = 0.60f;
    tun.roll_tumble_revs = 0.0f; // isolate the lean

    AnimDriverInput in;
    in.state = AnimState::DodgeRoll;
    in.phase = 0.10f;
    REQUIRE(evaluate(in).pitch_offset == Catch::Approx(0.0f).margin(kEps));
    in.phase = 0.80f;
    REQUIRE(evaluate(in).pitch_offset == Catch::Approx(0.0f).margin(kEps));
}

TEST_CASE("AnimationDriver: DodgeRoll lean peaks at configured peak phase",
          "[anim][driver][dodge][lean]")
{
    auto& tun = selva::tuning::current();
    resetTunablesForTests();
    tun.roll_lean_angle = 0.5f;
    tun.roll_lean_start = 0.0f;
    tun.roll_lean_end = 0.40f;
    tun.roll_lean_peak_phase = 0.5f; // peak at midpoint → global 0.20
    tun.roll_tumble_revs = 0.0f;

    AnimDriverInput in;
    in.state = AnimState::DodgeRoll;
    in.phase = 0.20f;
    REQUIRE(evaluate(in).pitch_offset == Catch::Approx(-0.5f).margin(kEps));
}

TEST_CASE("AnimationDriver: DodgeRoll lean composes additively with tumble",
          "[anim][driver][dodge][lean]")
{
    auto& tun = selva::tuning::current();
    resetTunablesForTests();
    tun.roll_lean_angle = 0.3f;
    tun.roll_lean_start = 0.0f;
    tun.roll_lean_end = 1.0f;
    tun.roll_lean_peak_phase = 0.5f;
    tun.roll_tumble_revs = 1.0f;
    tun.roll_tumble_start = 0.0f;
    tun.roll_tumble_end = 1.0f;

    AnimDriverInput in;
    in.state = AnimState::DodgeRoll;
    in.phase = 0.5f;
    const float combined = evaluate(in).pitch_offset;

    tun.roll_lean_angle = 0.0f;
    const float tumble_only = evaluate(in).pitch_offset;
    tun.roll_lean_angle = 0.3f;
    tun.roll_tumble_revs = 0.0f;
    const float lean_only = evaluate(in).pitch_offset;

    REQUIRE(combined == Catch::Approx(tumble_only + lean_only).margin(kEps));
}

TEST_CASE("AnimationDriver: DodgeRoll windowed sub-curves are independent", "[anim][driver][dodge]")
{
    auto& tun = selva::tuning::current();
    resetTunablesForTests();
    // Configure a deliberate "hop fully completes BEFORE tumble starts"
    // sequence — the structural property the windows enable.
    tun.roll_hop_start = 0.0f;
    tun.roll_hop_end = 0.40f;
    tun.roll_tumble_start = 0.40f;
    tun.roll_tumble_end = 1.0f;

    // Mid-hop, before any tumble: hop > 0, pitch == 0.
    AnimDriverInput in;
    in.state = AnimState::DodgeRoll;
    in.phase = 0.20f;
    const AnimDriverOutput out_a = evaluate(in);
    REQUIRE(out_a.translation.y > 0.0f);
    REQUIRE(out_a.pitch_offset == Catch::Approx(0.0f).margin(kEps));

    // Past the hop, mid-tumble: hop == 0, pitch != 0.
    in.phase = 0.70f;
    const AnimDriverOutput out_b = evaluate(in);
    REQUIRE(out_b.translation.y == Catch::Approx(0.0f).margin(kEps));
    REQUIRE(out_b.pitch_offset < 0.0f);
}

TEST_CASE("AnimationDriver: DodgeRoll tumble is forward (negative pitch)", "[anim][driver][dodge]")
{
    resetTunablesForTests();
    AnimDriverInput in;
    in.state = AnimState::DodgeRoll;
    in.phase = 0.5f;
    const AnimDriverOutput out = evaluate(in);
    // Negative around local +X = top tilts toward direction of motion.
    // Critical regression guard: this was the bug ("rolls backward").
    REQUIRE(out.pitch_offset < 0.0f);
}

TEST_CASE("AnimationDriver: DodgeRoll ease-in-out — half distance at translation midpoint",
          "[anim][driver][dodge]")
{
    resetTunablesForTests();
    // Translation window is [0.0, 0.85]; its midpoint is global phase 0.425.
    // easeInOutPow at the local midpoint is exactly 0.5 by construction
    // (symmetric around the midpoint regardless of power). Signature of
    // ease-in-out — slow start, fast middle, slow end.
    AnimDriverInput in;
    in.state = AnimState::DodgeRoll;
    in.phase = 0.425f;
    in.params.dodge_dir = glm::vec3(0.0f, 0.0f, -1.0f);
    const AnimDriverOutput out = evaluate(in);
    const float distance_covered = -out.translation.z;
    REQUIRE(distance_covered == Catch::Approx(3.5f * 0.5f).margin(kEps));
}

TEST_CASE("AnimationDriver: DodgeRoll respects dodge_dir parameter", "[anim][driver][dodge]")
{
    resetTunablesForTests();
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
    resetTunablesForTests();
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
    resetTunablesForTests();
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
    resetTunablesForTests();
    AnimDriverInput in;
    in.state = AnimState::DodgeRoll;
    in.phase = -0.5f;
    const AnimDriverOutput out = evaluate(in);
    REQUIRE(out.translation == glm::vec3(0.0f));
    REQUIRE(out.pitch_offset == Catch::Approx(0.0f).margin(kEps));
}

TEST_CASE("AnimationDriver: phase above 1 clamps to 1", "[anim][driver][robustness]")
{
    resetTunablesForTests();
    AnimDriverInput in;
    in.state = AnimState::DodgeRoll;
    in.phase = 1.5f;
    in.params.dodge_dir = glm::vec3(0.0f, 0.0f, -1.0f);
    const AnimDriverOutput out = evaluate(in);
    // Output should match phase=1, not run past the end.
    REQUIRE(out.translation.z == Catch::Approx(-3.5f).margin(kEps));
}
