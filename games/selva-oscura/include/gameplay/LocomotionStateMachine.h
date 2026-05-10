#pragma once

namespace selva::gameplay
{

enum class CombatStance
{
    Peaceful,
    CombatReady,
};

enum class CombatStanceFoot
{
    Default,
    Mirror,
};

// Combat-stance state. Previously held the full Idle/Walk/Run state
// machine + authored transition selector; both were deleted with the
// velocity-driven locomotion + snap+inertialize refactor. Only the
// combat-stance lifecycle survives (F press toggles, attack/block
// inputs auto-engage, decays to Peaceful after combat_idle_grace).
struct LocomotionStateMachine
{
    CombatStance combat_stance = CombatStance::Peaceful;
    CombatStanceFoot stance_foot = CombatStanceFoot::Default;
    float stance_active_until = 0.0f;
};

LocomotionStateMachine& locomotionSM();

} // namespace selva::gameplay
