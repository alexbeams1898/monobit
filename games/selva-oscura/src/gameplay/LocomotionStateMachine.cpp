#include "gameplay/LocomotionStateMachine.h"

namespace selva::gameplay
{

namespace
{
LocomotionStateMachine sLocomotionSM;
} // namespace

LocomotionStateMachine& locomotionSM()
{
    return sLocomotionSM;
}

} // namespace selva::gameplay
