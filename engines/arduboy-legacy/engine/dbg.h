// dbg::trace — always-linked diagnostic emit, no-op by default.
//
// Two output channels on Arduboy when DBG_TRACE is defined:
//
//   1. EEPROM ring buffer at TRACE_BASE (512). Each trace() advances a
//      head index and stores {tag, value_lo, value_hi} as a 3-byte
//      record. Wraps after RECORDS records. Open Ardens' EEPROM
//      window (Tools → EEPROM) to read the buffer live; the chip's
//      execution can crash arbitrarily and the EEPROM contents
//      survive — that's the whole point. The first 4 bytes at
//      TRACE_BASE are header: magic ('TR'), unused, head_lo, head_hi.
//
//   2. RX_LED on Arduboy (PB0, active-low): every trace() flashes the
//      LED once. Visually confirms code reached the point even when
//      EEPROM hasn't been polled yet. Driven by direct PORT writes
//      (no library deps).
//
// Why EEPROM and not USART1: Ardens v0.24.15 does NOT model USART1
// at all. The "Serial Monitor" window only captures USB-CDC writes
// (UEDATX). UDR1 writes hit dead RAM. EEPROM is fully emulated and
// has a dedicated viewer window. (Researched 2026-04-30, see
// project_vestigia_stack_overflow_2026_04_30 part 2 memory.)
//
// Build with DBG_TRACE=1:
//   make all DBG_TRACE=1                              # Arduboy
//   cmake -B build-sdl -DDBG_TRACE=ON                 # SDL
//
// Tag space:
//   0x00..0xFE valid; 0xFF reserved (means "uninitialized EEPROM cell").
//   0x01..0x7F ad-hoc per-session, recycle freely.
//   0x80..0xFE long-lived; document in tools/dbg_trace/README.md.
//
// EEPROM cost: 64 bytes at addresses 512..575. Free region per
// platform/arduboy/storage.cpp (best-run uses 0..6, meta uses
// 16..43, everything from 44..1023 is unclaimed).
//
// EEPROM write cost: ~3.3 ms per byte (busy-wait on EEPE clear).
// At 3 bytes per trace + 2 bytes for head update = ~16 ms per call.
// Don't trace from the audio ISR (16 kHz fire rate). Per-frame or
// per-event traces are fine.
//
// Cost when disabled: zero. Empty inline stubs; compiler removes
// every call site under -Os/-flto.

#pragma once

#include "types.h"

#ifdef DBG_TRACE
#ifdef __AVR__
#include <avr/eeprom.h>
#include <avr/io.h>
#else
#include <stdio.h>
#endif
#endif

namespace dbg {

#ifdef DBG_TRACE

namespace detail {

#ifdef __AVR__

// EEPROM trace ring layout (addresses are EEPROM, not RAM):
//   512  magic byte 'T' (0x54)
//   513  magic byte 'R' (0x52)
//   514  head index lo (next slot to write, 0..RECORDS-1)
//   515  unused, padding
//   516  record 0 byte 0 (tag)
//   517  record 0 byte 1 (value lo)
//   518  record 0 byte 2 (value hi)
//   519  record 1 byte 0
//   ...
//   573  record 19 byte 2
//   574  unused
//   575  unused
//
// 20 records × 3 bytes = 60 bytes payload + 4 bytes header = 64 bytes.
constexpr u16 TRACE_BASE = 512;
constexpr u8 RECORDS     = 20;
constexpr u8 MAGIC_T     = 0x54;
constexpr u8 MAGIC_R     = 0x52;

__attribute__((always_inline)) inline void ensure_initialized() {
  // Re-initialize the buffer header on the first trace() of a session.
  // We can't reliably do this from a static constructor (run order
  // vs. main is fragile under -flto). Cheap compare-and-store: read
  // the magic bytes, write them only if they don't already match.
  // After the first trace of a boot, both magics are correct and this
  // is a 2-byte read with no write.
  static bool primed = false;
  if (primed) return;
  primed = true;
  // Always reset head to 0 on the first trace of a session, even if
  // the magic already matches a prior session — gives a clean stream
  // per power-up. EEPROM cell wear: 1 write per boot, fine.
  eeprom_update_byte((u8*)(TRACE_BASE + 0), MAGIC_T);
  eeprom_update_byte((u8*)(TRACE_BASE + 1), MAGIC_R);
  eeprom_update_byte((u8*)(TRACE_BASE + 2), 0);  // head = 0
  eeprom_update_byte((u8*)(TRACE_BASE + 3), 0);  // padding
}

__attribute__((always_inline)) inline void emit_record(u8 tag, u16 value) {
  ensure_initialized();
  u8 head = eeprom_read_byte((const u8*)(TRACE_BASE + 2));
  if (head >= RECORDS) head = 0;
  const u16 slot = TRACE_BASE + 4 + (u16)head * 3;
  eeprom_update_byte((u8*)(slot + 0), tag);
  eeprom_update_byte((u8*)(slot + 1), (u8)(value & 0xFF));
  eeprom_update_byte((u8*)(slot + 2), (u8)((value >> 8) & 0xFF));
  ++head;
  if (head >= RECORDS) head = 0;
  eeprom_update_byte((u8*)(TRACE_BASE + 2), head);
}

// One LED flash on PB0 (RX_LED, active-low). Visual confirmation
// channel — PORT writes only, no library deps. Direct register
// access avoids any -mcall-prologues / shared-prologue surprises.
__attribute__((always_inline)) inline void led_pulse() {
  DDRB |= (1 << 0);    // PB0 as output (idempotent)
  PORTB &= ~(1 << 0);  // LED on (active-low)
  // Tiny busy-loop to make the flash visible (~50 µs at 16 MHz).
  for (volatile u8 i = 0; i < 200; ++i) {
    asm volatile("nop");
  }
  PORTB |= (1 << 0);  // LED off
}

#endif  // __AVR__

}  // namespace detail

__attribute__((always_inline)) inline void trace(u8 tag) {
#ifdef __AVR__
  detail::led_pulse();
  detail::emit_record(tag, 0);
#else
  printf("T%02X\n", (unsigned)tag);
#endif
}

__attribute__((always_inline)) inline void trace(u8 tag, u16 value) {
#ifdef __AVR__
  detail::led_pulse();
  detail::emit_record(tag, value);
#else
  printf("T%02X=%04X\n", (unsigned)tag, (unsigned)value);
#endif
}

#else  // DBG_TRACE not defined

// No-op stubs — compiler eliminates the call entirely under -Os/-flto.
__attribute__((always_inline)) inline void trace(u8 tag) {
  (void)tag;
}
__attribute__((always_inline)) inline void trace(u8 tag, u16 value) {
  (void)tag;
  (void)value;
}

#endif

}  // namespace dbg
