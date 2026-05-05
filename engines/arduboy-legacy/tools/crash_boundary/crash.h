// Crash-survival breadcrumb. Sized for the AVR's MCU-level reset detection
// path: when the watchdog timer bites because the main loop wedged, the
// chip reboots and MCUSR retains the WDRF bit. The platform layer reads it
// at boot and exposes it through `had_watchdog_reset()`. The game writes a
// short context (state + sub-state + cursor + frame) into a `.noinit`
// region every frame; on a watchdog-driven reboot, that region survives
// uninitialized so the game can paint a tombstone showing exactly which
// state was live when the loop died.
//
// What the watchdog catches: infinite loops, jumps to garbage (bad
// function pointers, smashed return addresses), anything that stops the
// main loop from petting the WDT. What it does NOT catch: clean logic
// bugs where the loop is alive but rendering wrong — pair this with
// on-screen probes for those.
//
// On platforms without a watchdog (PC/SDL), `had_watchdog_reset()` is a
// no-op returning false; `record()` is also a no-op. The game treats the
// boundary as "always healthy" there.
//
// REDEPLOY: see tools/crash_boundary/README.md for the wiring recipe.
// This file lives in tools/ when not in use; copy to engine/crash.h to
// make it part of a build.

#pragma once

#include "types.h"

namespace crash {

struct Context {
  u16 magic;     // 0xDEAD when written by record(); anything else = invalid
  u8 state;      // game::State value at last frame begin
  u8 sub_state;  // sub-view (e.g. shades_view); free-form per state
  u8 cursor;     // active list cursor (shades_cursor / menu_index / ...)
  u8 reserved;   // pad to alignment; reserved for future use
  u16 frame_lo;  // low 16 bits of clock::frame_count
};

// Write the live game context. Cheap (5-byte struct store, no syscalls).
// Call once per frame from the main loop, AFTER input poll but BEFORE
// update(), so a wedge during update is captured with the state we
// believed we were in.
void record(u8 state, u8 sub_state, u8 cursor, u16 frame_lo);

// True iff the previous run died and the watchdog rebooted the chip.
// Stable across calls in a single boot (the platform layer caches MCUSR).
bool had_watchdog_reset();

// Snapshot of the .noinit context at the *previous* run's last frame.
// Only meaningful when had_watchdog_reset() is true AND ctx.magic == 0xDEAD;
// otherwise the contents are uninitialized RAM (read at your own risk).
const Context& last_context();

}  // namespace crash
