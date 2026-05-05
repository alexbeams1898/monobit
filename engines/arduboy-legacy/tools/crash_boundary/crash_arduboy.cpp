// AVR implementation of the crash-breadcrumb / watchdog-tombstone path.
//
// Two pieces of MCU plumbing make this work:
//
//   1. MCUSR (MCU Status Register) latches the cause of every reset. Bit 3
//      (WDRF) is set if the watchdog timer fired. We capture and clear it
//      *very* early — before anything else (including the bootloader's
//      tail or .bss zeroing) has a chance to touch it — because some
//      bootloaders are known to clobber it on the way out.
//
//   2. The `.noinit` ELF section is RAM that the C runtime does NOT zero
//      at boot. Variables placed there hold their previous values across a
//      warm reset. We use a 6-byte `Context` struct as the breadcrumb.
//
// The early-MCUSR capture is done via avr-libc's `.init3` mechanism: a
// function tagged `naked, used, section(".init3")` runs before `main()`,
// before the .data/.bss copy/zero phase. We stash the latched MCUSR in
// another `.noinit` byte so the game-layer API can ask about it later.
//
// Watchdog policy: enabled with a 1-second timeout in init(). The main
// loop pets it (wdt_reset()) once per frame. At 60Hz that's plenty of
// margin — frames usually complete in <16ms, so the WDT only ever fires
// when the loop genuinely stops.
//
// REDEPLOY: see tools/crash_boundary/README.md. This file lives in tools/
// when not in use; copy to platform/arduboy/crash.cpp to make it part of
// a build.

#include "crash.h"

#include <avr/interrupt.h>
#include <avr/io.h>
#include <avr/wdt.h>

namespace crash {

// .noinit storage. Survives reset, NOT zeroed by C runtime startup.
// `__attribute__((used))` keeps LTO from deleting them since the loads
// from the early-init function are invisible to the optimizer.
__attribute__((used, section(".noinit"))) Context g_ctx;
__attribute__((used, section(".noinit"))) static u8 g_mcusr_at_boot;

// Run before main(), before .data/.bss init. Two reasons for `naked`:
// avoid the prologue (no stack frame needed for two MMIO ops) and avoid
// any compiler-inserted setup that might depend on .data being live yet.
__attribute__((naked, used, section(".init3"))) void capture_mcusr_and_disable_wdt() {
  // Snapshot the reset-cause bits the chip latched.
  g_mcusr_at_boot = MCUSR;
  // Clear MCUSR so the *next* boot starts clean. Per the datasheet the
  // bits are cleared by writing 1 to them.
  MCUSR = 0;
  // Disable the watchdog. If we got here because the WDT fired, it's
  // still configured and would re-fire during init. wdt_disable() does
  // the timed sequence the datasheet requires.
  wdt_disable();
}

void record(u8 state, u8 sub_state, u8 cursor, u16 frame_lo) {
  g_ctx.magic     = 0xDEAD;
  g_ctx.state     = state;
  g_ctx.sub_state = sub_state;
  g_ctx.cursor    = cursor;
  g_ctx.reserved  = 0;
  g_ctx.frame_lo  = frame_lo;
}

bool had_watchdog_reset() {
  return (g_mcusr_at_boot & (1 << WDRF)) != 0;
}

const Context& last_context() {
  return g_ctx;
}

// Public entry the platform main() calls once at boot to arm the watchdog.
// Separate from the .init3 capture so the game-layer code can be running
// (and the framebuffer initialized) by the time the WDT goes live.
void arm_watchdog_1s() {
  wdt_enable(WDTO_1S);
}

// Pet the watchdog. Called every frame from main loop.
void pet_watchdog() {
  wdt_reset();
}

}  // namespace crash
