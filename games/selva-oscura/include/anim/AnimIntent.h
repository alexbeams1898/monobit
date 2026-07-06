#pragma once

#include <string_view>

namespace selva::anim
{

// Gameplay-facing animation vocabulary. Game code requests an intent;
// the AnimSet on each actor resolves which clip plays. Equipment-state
// branching ("armed vs unarmed") lives in the SET (swap the active set
// when equipment changes), never as `is_armed ? X : Y` in gameplay code.
//
// Adding an intent: append to the enum + add it to the kAnimIntentNames
// table + map it in every shipping AnimSet JSON. The AnimSet loader
// asserts every intent is mapped, so a missing entry fails loud at
// load time, not silently in a combat encounter.
enum class AnimIntent
{
    // --- Locomotion (sustained, looping) ---------------------------
    LocoIdle,            // peaceful standing
    LocoIdleCombat,      // combat stance idle
    LocoIdleBlock,       // holding block stance still
    LocoIdleCrouch,      // crouched and still
    LocoIdleCrouchBlock, // crouched + blocking

    LocoWalk,
    LocoWalkBack,
    LocoStrafeLeft,
    LocoStrafeRight,

    LocoJog,
    LocoJogBack,
    LocoJogStrafeLeft,
    LocoJogStrafeRight,

    LocoSprint,

    LocoCrouch, // crouched movement

    // --- One-shot actions (fire-and-finish) ------------------------
    ActionJumpStanding,
    ActionJumpRunning,
    ActionDodgeBackward,
    ActionDodgeRoll,
    ActionGetUp,

    ActionBlockRaise,
    ActionBlockLower,

    ActionDrawWeapon,
    ActionSheathWeapon,

    ActionAttackLight1,
    ActionAttackLight2,
    ActionAttackHeavy,
    ActionAttackKick,
    ActionAttackCasting,
    ActionAttackPowerUp,

    ActionImpact,
    ActionDeath,
    ActionDying,

    // Keep last; sized lookup tables index against this.
    COUNT
};

// String name for an intent. Used by AnimSet JSON loader to match keys
// and by debug/log output. Same order + count as the enum.
std::string_view animIntentName(AnimIntent intent);

// Reverse lookup; returns AnimIntent::COUNT on unknown name.
AnimIntent animIntentFromName(std::string_view name);

} // namespace selva::anim
