#pragma once

#include "log/Log.h"

#include <fmt/core.h>
#include <fmt/format.h> // fmt::ptr for pointer formatting

namespace selva::combat
{

// Master switch for the combat debug overlay + per-fire diagnostics.
// Off by default — the HUD render, pose-match scan, splice-distance
// joint samples, and per-press combatLog spam ALL gate on this. Toggle
// from the F1 panel when iterating on combat feel.
//
// Why this matters: with this enabled, fireAttack runs ~40 ozz
// SamplingJobs (pose-match scan + 5-joint splice samples + chain-link
// velocity diag) AND multiple fflush'd disk writes on a single press
// frame. Tracy showed selvaPerFrame max=77ms (vs ~1ms baseline) when
// these fire. Default off keeps the per-attack frame cost flat.
//
// Resolver dump at startup, sprint-finisher swaps, and the combat-debug
// log file open all also gate on this — the file isn't even created
// when the flag is off.
bool isCombatDebugEnabled();

// Toggle combat debug. Opens combat-debug.log on enable, closes on
// disable. Anim sampler diagnostics flow through the same
// engine::log::Channel ("combat"), so they auto-route alongside.
void setCombatDebugEnabled(bool enabled);

// Returns the shared engine::log::Channel for combat diagnostics.
// Lazily creates the channel on first call. Configured to write to
// combat-debug.log + mirror to stderr; gated on isCombatDebugEnabled().
// All callers (combat, sampler) write through this single channel,
// which means combat-debug.log is the one source of truth — no more
// hand-wired "set sampler log file pointer to combat's file pointer"
// plumbing.
engine::log::Channel& combatChannel();

// Convenience: forwards to combatChannel().debug(...). Compile-time
// format-string checked via fmt::format_string. Use this for the
// previous combatLog(...) pattern at the call site.
template <typename... Args> void combatLog(fmt::format_string<Args...> fmt, Args&&... args)
{
    combatChannel().debug(fmt, std::forward<Args>(args)...);
}

} // namespace selva::combat
