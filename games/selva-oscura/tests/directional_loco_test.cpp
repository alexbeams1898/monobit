// Tests for selva::gameplay::directionalLocoClip: the locked-on
// directional clip picker used when the player is lock-targeted on
// an enemy. Covers the unarmed-vs-armed branching across forward /
// back / strafe / sprint inputs.

#include "gameplay/Actor.h"

#include <cstring>

#include <catch2/catch_test_macros.hpp>

using selva::gameplay::directionalLocoClip;
using selva::gameplay::LocoTier;

namespace
{

// Player-facing forward axis (+Z) and right axis (+X) for these
// tests. The function treats `fwd` as "toward the locked target" and
// `right` as perpendicular; intent then dots against them.
const glm::vec3 kFwd(0.0f, 0.0f, 1.0f);
const glm::vec3 kRight(1.0f, 0.0f, 0.0f);

// Intent vectors aligned to those axes for the four cardinal lock-on
// directions.
const glm::vec3 kForward(0.0f, 0.0f, 1.0f);
const glm::vec3 kBackward(0.0f, 0.0f, -1.0f);
const glm::vec3 kStrafeRight(1.0f, 0.0f, 0.0f);
const glm::vec3 kStrafeLeft(-1.0f, 0.0f, 0.0f);

bool isClip(const char* a, const char* b)
{
    return a != nullptr && std::strcmp(a, b) == 0;
}

} // namespace

TEST_CASE("directionalLocoClip returns nullptr for zero intent", "[loco][directional]")
{
    REQUIRE(directionalLocoClip(kFwd, kRight, glm::vec3(0.0f), LocoTier::Jog, false) == nullptr);
}

TEST_CASE("unarmed locked-on directional clips", "[loco][directional][unarmed]")
{
    REQUIRE(isClip(directionalLocoClip(kFwd, kRight, kForward, LocoTier::Walk, false), "walking"));
    REQUIRE(isClip(directionalLocoClip(kFwd, kRight, kBackward, LocoTier::Walk, false),
                   "walking_backward"));
    REQUIRE(isClip(directionalLocoClip(kFwd, kRight, kForward, LocoTier::Jog, false), "jogging"));
    REQUIRE(isClip(directionalLocoClip(kFwd, kRight, kBackward, LocoTier::Jog, false),
                   "jogging_backward"));
    REQUIRE(isClip(directionalLocoClip(kFwd, kRight, kStrafeRight, LocoTier::Walk, false),
                   "strafe_walking_right"));
    REQUIRE(isClip(directionalLocoClip(kFwd, kRight, kStrafeLeft, LocoTier::Walk, false),
                   "strafe_walking_left"));
    REQUIRE(isClip(directionalLocoClip(kFwd, kRight, kStrafeRight, LocoTier::Jog, false),
                   "strafe_jogging_right"));
    REQUIRE(isClip(directionalLocoClip(kFwd, kRight, kStrafeLeft, LocoTier::Jog, false),
                   "strafe_jogging_left"));
    // Sprint forward picks sprinting; sprint back/strafe demote to jog.
    REQUIRE(
        isClip(directionalLocoClip(kFwd, kRight, kForward, LocoTier::Sprint, false), "sprinting"));
    REQUIRE(isClip(directionalLocoClip(kFwd, kRight, kBackward, LocoTier::Sprint, false),
                   "jogging_backward"));
    REQUIRE(isClip(directionalLocoClip(kFwd, kRight, kStrafeRight, LocoTier::Sprint, false),
                   "strafe_jogging_right"));
}

TEST_CASE("armed locked-on directional clips use _grip variants", "[loco][directional][armed]")
{
    REQUIRE(isClip(directionalLocoClip(kFwd, kRight, kForward, LocoTier::Walk, true),
                   "sword_and_shield_walk_grip"));
    REQUIRE(isClip(directionalLocoClip(kFwd, kRight, kBackward, LocoTier::Walk, true),
                   "sword_and_shield_walk_2_grip"));
    REQUIRE(isClip(directionalLocoClip(kFwd, kRight, kForward, LocoTier::Jog, true),
                   "sword_and_shield_run_grip"));
    // Armed back at jog tier has no run-back clip; demotes to the
    // armed walk-back per the in-source comment.
    REQUIRE(isClip(directionalLocoClip(kFwd, kRight, kBackward, LocoTier::Jog, true),
                   "sword_and_shield_walk_2_grip"));
    REQUIRE(isClip(directionalLocoClip(kFwd, kRight, kStrafeRight, LocoTier::Walk, true),
                   "sword_and_shield_strafe_grip"));
    REQUIRE(isClip(directionalLocoClip(kFwd, kRight, kStrafeLeft, LocoTier::Walk, true),
                   "sword_and_shield_strafe_2_grip"));
    // Armed sprint forward shares the run-grip clip (per the user-
    // chosen "reuse run for sprint at faster playback" doctrine).
    REQUIRE(isClip(directionalLocoClip(kFwd, kRight, kForward, LocoTier::Sprint, true),
                   "sword_and_shield_run_grip"));
}
