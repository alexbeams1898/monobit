#include "combat/AttackChain.h"

namespace selva::combat
{

namespace
{
BufferedPress sBufferedRight;
BufferedPress sBufferedLeft;
PendingFirstAction sPendingFirstAction;
} // namespace

BufferedPress& buffer(HandSide hand)
{
    return (hand == HandSide::Right) ? sBufferedRight : sBufferedLeft;
}

PendingFirstAction& pendingFirstAction()
{
    return sPendingFirstAction;
}

void tickChainExpiry(float wall_clock_seconds, float buffer_seconds)
{
    if (sBufferedRight.pending && wall_clock_seconds - sBufferedRight.buffered_at > buffer_seconds)
        sBufferedRight.pending = false;
    if (sBufferedLeft.pending && wall_clock_seconds - sBufferedLeft.buffered_at > buffer_seconds)
        sBufferedLeft.pending = false;
}

} // namespace selva::combat
