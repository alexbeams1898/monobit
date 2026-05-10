#pragma once

namespace selva::gameplay
{

enum class LocomotionState
{
    Idle,
    Walk,
    Run,
    Transitioning,
};

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

struct LocomotionStateMachine
{
    LocomotionState current = LocomotionState::Idle;
    LocomotionState target = LocomotionState::Idle;
    const char* active_transition_clip = nullptr;
    bool active_transition_loops = false;

    CombatStance combat_stance = CombatStance::Peaceful;
    CombatStanceFoot stance_foot = CombatStanceFoot::Default;
    float stance_active_until = 0.0f;

    static LocomotionState desiredFromIntent(bool is_moving, bool is_sprinting);
};

struct LocomotionFrameOutput
{
    const char* clip_name = "standard_idle";
    bool loops = true;
    float blend_seconds = 0.20f;
};

const char* selectTransitionClip(LocomotionState from, LocomotionState to, bool* out_loops);
const char* loopClipForState(LocomotionState s, CombatStance stance, bool is_armed);

struct LocomotionTickInput
{
    bool is_moving = false;
    bool is_sprinting = false;
    bool clip_finished_this_frame = false;
    bool combat_input_this_frame = false;
    bool is_armed = false;
    float dt = 0.0f;
    float combat_grace_seconds = 0.0f;
    float wall_clock_seconds = 0.0f;
};

LocomotionFrameOutput tickLocomotionStateMachine(LocomotionStateMachine& sm,
                                                 const LocomotionTickInput& in);

LocomotionStateMachine& locomotionSM();

} // namespace selva::gameplay
