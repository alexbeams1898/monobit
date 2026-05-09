#include "gameplay/LocomotionStateMachine.h"

namespace selva::gameplay
{

namespace
{
LocomotionStateMachine sLocomotionSM;
} // namespace

LocomotionState LocomotionStateMachine::desiredFromIntent(bool is_moving, bool is_sprinting)
{
    if (!is_moving)
        return LocomotionState::Idle;
    if (is_sprinting)
        return LocomotionState::Run;
    return LocomotionState::Walk;
}

const char* selectTransitionClip(LocomotionState /*from*/, LocomotionState /*to*/, bool* out_loops)
{
    *out_loops = false;
    return nullptr;
}

const char* loopClipForState(LocomotionState s, CombatStance stance, bool is_armed)
{
    switch (s)
    {
    case LocomotionState::Idle:
        if (stance == CombatStance::CombatReady)
            return is_armed ? "sword_and_shield_idle_4" : "unarmed_combat_idle";
        return "standard_idle";
    case LocomotionState::Walk:
        return "walking";
    case LocomotionState::Run:
        return "running";
    case LocomotionState::Transitioning:
        return "standard_idle";
    }
    return "standard_idle";
}

LocomotionFrameOutput tickLocomotionStateMachine(LocomotionStateMachine& sm, bool is_moving,
                                                 bool is_sprinting,
                                                 bool clip_finished_this_frame,
                                                 bool combat_input_this_frame, float /*dt*/,
                                                 float combat_grace_seconds, bool is_armed,
                                                 float wall_clock_seconds)
{
    if (combat_input_this_frame)
    {
        sm.combat_stance = CombatStance::CombatReady;
        const float new_until = wall_clock_seconds + combat_grace_seconds;
        if (new_until > sm.stance_active_until)
            sm.stance_active_until = new_until;
    }
    if (sm.combat_stance == CombatStance::CombatReady &&
        wall_clock_seconds >= sm.stance_active_until)
    {
        sm.combat_stance = CombatStance::Peaceful;
    }

    LocomotionFrameOutput out;
    const LocomotionState desired =
        LocomotionStateMachine::desiredFromIntent(is_moving, is_sprinting);

    if (sm.current == LocomotionState::Transitioning)
    {
        if (clip_finished_this_frame)
        {
            sm.current = sm.target;
            sm.active_transition_clip = nullptr;
        }
        else
        {
            out.clip_name = sm.active_transition_clip
                                ? sm.active_transition_clip
                                : loopClipForState(sm.target, sm.combat_stance, is_armed);
            out.loops = sm.active_transition_loops;
            out.blend_seconds = 0.10f;
            return out;
        }
    }

    if (desired == sm.current)
    {
        out.clip_name = loopClipForState(sm.current, sm.combat_stance, is_armed);
        out.loops = true;
        return out;
    }

    bool tloops = false;
    const char* transition = selectTransitionClip(sm.current, desired, &tloops);
    if (transition != nullptr)
    {
        sm.active_transition_clip = transition;
        sm.active_transition_loops = tloops;
        sm.target = desired;
        sm.current = LocomotionState::Transitioning;
        out.clip_name = transition;
        out.loops = tloops;
        out.blend_seconds = 0.10f;
        return out;
    }
    sm.current = desired;
    out.clip_name = loopClipForState(desired, sm.combat_stance, is_armed);
    out.loops = true;
    out.blend_seconds = 0.20f;
    return out;
}

LocomotionStateMachine& locomotionSM()
{
    return sLocomotionSM;
}

} // namespace selva::gameplay
