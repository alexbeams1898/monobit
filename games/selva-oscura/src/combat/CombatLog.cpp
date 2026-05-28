#include "combat/CombatLog.h"

namespace selva::combat
{

namespace
{
// Default ON during active combat-feel iteration. The logging+disk-flush
// cost is real (Tracy showed selvaPerFrame max 77ms vs ~1ms baseline
// with debug active) — flip to false when not iterating on combat.
bool sEnabled = false;
} // namespace

bool isCombatDebugEnabled()
{
    return sEnabled;
}

engine::log::Channel& combatChannel()
{
    // First call wins: registers the "combat" channel writing to
    // combat-debug.log with stderr mirror. Subsequent calls hit the
    // cached lookup. The PoseSampler (selva/anim/PoseSampler.cpp)
    // uses Channel::find("combat") to write into the same channel
    // without owning the file. One source of truth.
    static engine::log::Channel& ch = engine::log::Channel::get(
        "combat", engine::log::Sink::File, "combat-debug.log", engine::log::Level::Trace,
        /*mirror_to_stderr=*/true);
    // Start disabled until setCombatDebugEnabled(true) flips it on.
    // Idempotent — each call just re-asserts the current state.
    ch.set_enabled(sEnabled);
    return ch;
}

void setCombatDebugEnabled(bool enabled)
{
    if (sEnabled == enabled)
        return;
    sEnabled = enabled;
    combatChannel().set_enabled(sEnabled);
}

} // namespace selva::combat
