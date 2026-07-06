// Tests for selva::gameplay::directionalLocoClip: the locked-on
// directional clip picker used when the player is lock-targeted on
// an enemy. Variant branching (armed vs unarmed) moved out of the
// picker into the active AnimSet pointer the caller supplies.

#include "anim/AnimSet.h"
#include "anim/AnimSetRegistry.h"
#include "gameplay/Actor.h"

#include <cstdio>
#include <cstring>
#include <filesystem>

#include <catch2/catch_test_macros.hpp>

using selva::anim::AnimSet;
using selva::anim::AnimSetRegistry;
using selva::gameplay::directionalLocoClip;
using selva::gameplay::LocoTier;

namespace
{

const glm::vec3 kFwd(0.0f, 0.0f, 1.0f);
const glm::vec3 kRight(1.0f, 0.0f, 0.0f);

const glm::vec3 kForward(0.0f, 0.0f, 1.0f);
const glm::vec3 kBackward(0.0f, 0.0f, -1.0f);
const glm::vec3 kStrafeRight(1.0f, 0.0f, 0.0f);
const glm::vec3 kStrafeLeft(-1.0f, 0.0f, 0.0f);

bool isClip(const char* a, const char* b)
{
    return a != nullptr && std::strcmp(a, b) == 0;
}

// Resolve the project's config/anim dir relative to the test binary
// or the source tree. Tests can be invoked from either; both layouts
// are valid. Returns empty path if neither is found.
std::filesystem::path findConfigDir()
{
    namespace fs = std::filesystem;
    for (const auto& candidate : {fs::path{"config/anim"}, fs::path{"../config/anim"},
                                  fs::path{"games/selva-oscura/config/anim"}})
    {
        if (fs::is_directory(candidate))
            return candidate;
    }
    return {};
}

const AnimSet* loadHumanoidUnarmed()
{
    static const AnimSet* sCached = nullptr;
    if (sCached)
        return sCached;
    const auto dir = findConfigDir();
    if (dir.empty())
        return nullptr;
    AnimSetRegistry::instance().loadFromDirectory(dir.string());
    sCached = AnimSetRegistry::instance().get("humanoid_unarmed");
    return sCached;
}

const AnimSet* loadHumanoidArmed()
{
    loadHumanoidUnarmed(); // populates registry
    return AnimSetRegistry::instance().get("humanoid_sword_and_shield");
}

} // namespace

TEST_CASE("directionalLocoClip returns nullptr for zero intent", "[loco][directional]")
{
    const AnimSet* set = loadHumanoidUnarmed();
    REQUIRE(set != nullptr);
    REQUIRE(directionalLocoClip(kFwd, kRight, glm::vec3(0.0f), LocoTier::Jog, set) == nullptr);
}

TEST_CASE("directionalLocoClip returns nullptr when AnimSet is null", "[loco][directional]")
{
    REQUIRE(directionalLocoClip(kFwd, kRight, kForward, LocoTier::Jog, nullptr) == nullptr);
}

TEST_CASE("unarmed locked-on directional clips", "[loco][directional][unarmed]")
{
    const AnimSet* set = loadHumanoidUnarmed();
    REQUIRE(set != nullptr);

    REQUIRE(isClip(directionalLocoClip(kFwd, kRight, kForward, LocoTier::Walk, set), "walking"));
    REQUIRE(isClip(directionalLocoClip(kFwd, kRight, kBackward, LocoTier::Walk, set),
                   "walking_backward"));
    REQUIRE(isClip(directionalLocoClip(kFwd, kRight, kForward, LocoTier::Jog, set), "jogging"));
    REQUIRE(isClip(directionalLocoClip(kFwd, kRight, kBackward, LocoTier::Jog, set),
                   "jogging_backward"));
    REQUIRE(isClip(directionalLocoClip(kFwd, kRight, kStrafeRight, LocoTier::Walk, set),
                   "strafe_walking_right"));
    REQUIRE(isClip(directionalLocoClip(kFwd, kRight, kStrafeLeft, LocoTier::Walk, set),
                   "strafe_walking_left"));
    REQUIRE(isClip(directionalLocoClip(kFwd, kRight, kStrafeRight, LocoTier::Jog, set),
                   "strafe_jogging_right"));
    REQUIRE(isClip(directionalLocoClip(kFwd, kRight, kStrafeLeft, LocoTier::Jog, set),
                   "strafe_jogging_left"));
    // Sprint forward picks the LocoSprint clip; sprint back/strafe demote to jog.
    REQUIRE(
        isClip(directionalLocoClip(kFwd, kRight, kForward, LocoTier::Sprint, set), "sprinting"));
    REQUIRE(isClip(directionalLocoClip(kFwd, kRight, kBackward, LocoTier::Sprint, set),
                   "jogging_backward"));
    REQUIRE(isClip(directionalLocoClip(kFwd, kRight, kStrafeRight, LocoTier::Sprint, set),
                   "strafe_jogging_right"));
}

TEST_CASE("armed locked-on directional clips use sword variants", "[loco][directional][armed]")
{
    const AnimSet* set = loadHumanoidArmed();
    REQUIRE(set != nullptr);

    REQUIRE(isClip(directionalLocoClip(kFwd, kRight, kForward, LocoTier::Walk, set),
                   "sword_and_shield_walk"));
    REQUIRE(isClip(directionalLocoClip(kFwd, kRight, kBackward, LocoTier::Walk, set),
                   "sword_and_shield_walk"));
    REQUIRE(isClip(directionalLocoClip(kFwd, kRight, kForward, LocoTier::Jog, set),
                   "sword_and_shield_run"));
    REQUIRE(isClip(directionalLocoClip(kFwd, kRight, kBackward, LocoTier::Jog, set),
                   "sword_and_shield_run"));
    REQUIRE(isClip(directionalLocoClip(kFwd, kRight, kStrafeRight, LocoTier::Walk, set),
                   "sword_and_shield_strafe"));
    REQUIRE(isClip(directionalLocoClip(kFwd, kRight, kStrafeLeft, LocoTier::Walk, set),
                   "sword_and_shield_strafe"));
    REQUIRE(
        isClip(directionalLocoClip(kFwd, kRight, kForward, LocoTier::Sprint, set), "sprinting"));
}
