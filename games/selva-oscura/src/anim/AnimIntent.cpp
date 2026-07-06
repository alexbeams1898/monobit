#include "anim/AnimIntent.h"

#include <array>

namespace selva::anim
{

namespace
{
constexpr std::array<std::string_view, static_cast<size_t>(AnimIntent::COUNT)> kNames = {
    "LocoIdle",
    "LocoIdleCombat",
    "LocoIdleBlock",
    "LocoIdleCrouch",
    "LocoIdleCrouchBlock",

    "LocoWalk",
    "LocoWalkBack",
    "LocoStrafeLeft",
    "LocoStrafeRight",

    "LocoJog",
    "LocoJogBack",
    "LocoJogStrafeLeft",
    "LocoJogStrafeRight",

    "LocoSprint",

    "LocoCrouch",

    "ActionJumpStanding",
    "ActionJumpRunning",
    "ActionDodgeBackward",
    "ActionDodgeRoll",
    "ActionGetUp",

    "ActionBlockRaise",
    "ActionBlockLower",

    "ActionDrawWeapon",
    "ActionSheathWeapon",

    "ActionAttackLight1",
    "ActionAttackLight2",
    "ActionAttackHeavy",
    "ActionAttackKick",
    "ActionAttackCasting",
    "ActionAttackPowerUp",

    "ActionImpact",
    "ActionDeath",
    "ActionDying",
};
}

std::string_view animIntentName(AnimIntent intent)
{
    const auto i = static_cast<size_t>(intent);
    return i < kNames.size() ? kNames[i] : std::string_view{"<invalid>"};
}

AnimIntent animIntentFromName(std::string_view name)
{
    for (size_t i = 0; i < kNames.size(); ++i)
        if (kNames[i] == name)
            return static_cast<AnimIntent>(i);
    return AnimIntent::COUNT;
}

} // namespace selva::anim
