#pragma once

#include <cstdarg>
#include <cstdio>

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
// disable. Wires the open file into the PoseSampler diagnostic log.
void setCombatDebugEnabled(bool enabled);

// Open the combat-debug.log file directly (used at startup if the
// debug flag was on by default in source). Returns the FILE* so the
// caller can wire it into PoseSampler diag. Pairs with closeCombatLog().
FILE* openCombatLog();
void closeCombatLog();

// Debug-gated log: no-op when isCombatDebugEnabled() is false.
// Routes to stderr + the combat log file when enabled. Use for
// high-frequency per-fire / per-frame diagnostics. fflush per line
// keeps the log readable even after a crash.
void combatLog(const char* fmt, ...);

} // namespace selva::combat
