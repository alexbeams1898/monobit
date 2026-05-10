#include "combat/AttackChain.h"

namespace selva::combat
{

namespace
{
AttackChainState sChainRight;
AttackChainState sChainLeft;
BufferedPress sBufferedRight;
BufferedPress sBufferedLeft;
PendingFirstAction sPendingFirstAction;
} // namespace

AttackChainState& chain(HandSide hand)
{
    return (hand == HandSide::Right) ? sChainRight : sChainLeft;
}

BufferedPress& buffer(HandSide hand)
{
    return (hand == HandSide::Right) ? sBufferedRight : sBufferedLeft;
}

PendingFirstAction& pendingFirstAction()
{
    return sPendingFirstAction;
}

void resetChain(AttackChainState& c)
{
    c.chain_index = 0;
    c.technique_index = -1;
    c.cancel_window_open_at = 0.0f;
    c.cancel_window_close_at = 0.0f;
    c.chain_reset_at = 0.0f;
    c.is_finisher = false;
    c.last_press_accuracy = 0.0f;
    c.last_press_was_perfect = false;
}

void tickChainExpiry(float wall_clock_seconds, float buffer_seconds)
{
    // Cancel-window expiry is implicit: clipForButton's inside-window
    // check fails once wall_clock_seconds > cancel_window_close_at, so
    // the rebuilt press path doesn't need explicit window clearing.
    // The HUD renders cancel_window_*_at directly — clearing them
    // here erased the visual band each frame.
    if (sBufferedRight.pending &&
        wall_clock_seconds - sBufferedRight.buffered_at > buffer_seconds)
        sBufferedRight.pending = false;
    if (sBufferedLeft.pending &&
        wall_clock_seconds - sBufferedLeft.buffered_at > buffer_seconds)
        sBufferedLeft.pending = false;
}

} // namespace selva::combat
