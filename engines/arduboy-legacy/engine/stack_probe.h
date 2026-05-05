// Runtime stack-peak tripwire — paint the unused stack region with a
// known sentinel at boot, scan it later to find the empirical
// high-water mark.
//
// Why this exists in addition to the static analyzer:
// scripts/check_stack.py computes the worst-case stack peak by
// summing -fstack-usage frames along the call graph. That tool only
// catches what the disassembly walker can see — `-mcall-prologues`
// shared stubs and indirect calls (vtables, fnptr tables) hide many
// edges. The runtime probe measures *empirical* peak across whatever
// scenario the dev exercises. Together: static catches what's
// statically reachable, runtime catches the rest.
//
// Cost when STACK_PROBE undefined: zero. All functions become empty
// inline stubs the compiler removes.
//
// How to use:
//   make all STACK_PROBE=1
//   // ... in code, after the deepest operation you want to measure:
//   u16 peak = stack_probe::peak_bytes();
//   dbg::trace(0x90, peak);
//
// Then run the scenario (vestigia save, scene swap, full-combat
// frame), open Ardens' EEPROM viewer, find the trace record. peak
// is bytes from __bss_end to the deepest SP the run reached.
//
// Sentinel byte 0xC5 chosen to be:
//   - Not 0xFF (matches uninitialized flash/EEPROM, would false-clean)
//   - Not 0x00 (matches `.bss`, would false-clean)
//   - Not a common AVR opcode byte to reduce coincidence
//
// Limitations:
//   - Paint MUST happen before any deep call grows the stack — in
//     practice, very early in main(). The function only paints
//     (current_sp .. __bss_end_high), so anything pushed before paint
//     will read as "untouched" forever even if rewritten later.
//   - If a single call frame both grows AND shrinks across the same
//     bytes within one scenario, the peak measurement still correctly
//     reflects the deepest SP — the sentinel got overwritten then,
//     and the peeled-back call frame doesn't restore it.
//   - SDL platform: SDL has no fixed stack region the same way; we
//     stub these out with no-ops since stack overflow on PC is
//     either OS-caught or never the failure mode that bites.

#pragma once

#include "types.h"

namespace stack_probe {

#if defined(STACK_PROBE) && defined(__AVR__)

// Sentinel byte used to paint unused stack.
constexpr u8 SENTINEL = 0xC5;

// Paint the region between the current stack top (SP) and the bottom
// of stack space (__bss_end) with SENTINEL. Call once in main() before
// any deep operation. Idempotent: re-painting just resets the probe.
//
// The implementation uses inline asm because we need to read SP and
// the linker symbol __bss_end at runtime; doing it in C++ would risk
// the compiler optimizing the loop or reordering with respect to SP.
void paint();

// Return the empirical stack high-water mark since the last paint(),
// in bytes. 0 means SP never grew past __bss_end (impossible in
// practice — the call frame for peak_bytes itself counts).
//
// Walks from __bss_end upward, returning the offset of the first byte
// that is NOT SENTINEL. That's the deepest SP point a call frame ever
// occupied (because the prologue push/store sequence wrote real
// register data on top of the sentinel).
u16 peak_bytes();

// Read the latest peak AND emit it to a fixed EEPROM word (addr 510).
// Cheaper than wrapping with dbg::trace: a single eeprom_update_word
// call, no LED pulse, no ring-buffer header bookkeeping. Survives
// power-loss; readable from Ardens' EEPROM viewer at addr 510 (LSB
// at 510, MSB at 511) at any time.
//
// Why a different EEPROM region from dbg::trace's ring (which uses
// 512+): keep them disjoint so neither tool clobbers the other's
// state. 510..511 is otherwise unused (storage's best-run uses 0..6,
// meta uses 16..43; everything 44..511 is free).
void log_peak_to_eeprom();

// Read the most recent peak that log_peak_to_eeprom() wrote. Useful
// for code that wants to display the value, log it elsewhere, or
// gate a behavior on it.
u16 read_logged_peak();

// Tiny diagnostic-breadcrumb helper. Writes a single u8 tag to EEPROM
// address 509. Plant breadcrumb(N) calls at boot decision points;
// after reset the EEPROM at addr 509 holds the LAST tag the running
// code reached. Survives reset, white-screen, OOB-deref — anything
// short of a full chip wipe. ~6 B of flash per call site.
//
// Tag space: any u8 you like. Reserve 0xFF as "uninitialized."
void breadcrumb(u8 tag);
u8 read_breadcrumb();

// Log a u16 value to EEPROM 507-508 (LE). For inspecting a live
// pointer / counter / state value at the moment a breadcrumb fires.
void log_u16(u16 value);
u16 read_logged_u16();

// One-shot persistent log: writes (tag, u16) to EEPROM 504-506. Use
// for boot-time captures that must survive the running for-loop's
// breadcrumb churn at 0x1FD / 0x1FB.
void log_oneshot(u8 tag, u16 value);

// Second one-shot slot at EEPROM 501-503.
void log_oneshot2(u8 tag, u16 value);

// Third one-shot slot at EEPROM 498-500.
void log_oneshot3(u8 tag, u16 value);

#else  // STACK_PROBE undefined or non-AVR

// No-op stubs — compiler eliminates the call entirely.
inline void paint() {}
inline u16 peak_bytes() {
  return 0;
}
inline void log_peak_to_eeprom() {}
inline u16 read_logged_peak() {
  return 0;
}
inline void breadcrumb(u8) {}
inline u8 read_breadcrumb() {
  return 0;
}
inline void log_u16(u16) {}
inline u16 read_logged_u16() {
  return 0;
}
inline void log_oneshot(u8, u16) {}
inline void log_oneshot2(u8, u16) {}
inline void log_oneshot3(u8, u16) {}

#endif

}  // namespace stack_probe
