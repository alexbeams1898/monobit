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
    if (sChainRight.chain_index > 0 && wall_clock_seconds >= sChainRight.chain_reset_at)
        resetChain(sChainRight);
    if (sChainLeft.chain_index > 0 && wall_clock_seconds >= sChainLeft.chain_reset_at)
        resetChain(sChainLeft);
    // Even after the chain wraps to 0 (mid-flight, after firing the
    // final entry), the window timestamps are still set forward by the
    // last fire. Once the reset grace passes, those need clearing too
    // so the next press is a fresh first-strike, not "past the close
    // of slash_4's window."
    if (sChainRight.chain_index == 0 && sChainRight.cancel_window_open_at > 0.0f &&
        wall_clock_seconds >= sChainRight.chain_reset_at)
        resetChain(sChainRight);
    if (sChainLeft.chain_index == 0 && sChainLeft.cancel_window_open_at > 0.0f &&
        wall_clock_seconds >= sChainLeft.chain_reset_at)
        resetChain(sChainLeft);

    // Expire stale buffered presses.
    if (sBufferedRight.pending &&
        wall_clock_seconds - sBufferedRight.buffered_at > buffer_seconds)
        sBufferedRight.pending = false;
    if (sBufferedLeft.pending &&
        wall_clock_seconds - sBufferedLeft.buffered_at > buffer_seconds)
        sBufferedLeft.pending = false;
}

} // namespace selva::combat
