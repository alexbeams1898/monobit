// AVR implementation of the stack-peak tripwire. See engine/stack_probe.h
// for the rationale and contract.
//
// Linker symbols used:
//   __bss_end — end of .bss (linker-provided). Stack grows downward
//                from RAMEND; the region [__bss_end .. SP) is unused
//                stack space we want to paint.
//   SP        — current stack pointer (special function register).
//
// Painting strategy: walk from __bss_end up to (SP - SAFETY) and write
// SENTINEL. We leave a small gap below SP untouched so a poorly-timed
// interrupt push doesn't get clobbered by our paint loop. (paint() is
// non-reentrant; we pause interrupts inside it.)

#if defined(STACK_PROBE) && defined(__AVR__)

#include "stack_probe.h"

#include <avr/eeprom.h>
#include <avr/interrupt.h>
#include <avr/io.h>

extern "C" char __bss_end;  // linker-provided; address of first byte
                            // past .bss. Stack grows from RAMEND down
                            // toward this.

namespace stack_probe {

// Don't paint the very top of the unused region (just below SP) to
// avoid racing with an interrupt that's mid-push. 8 bytes is plenty.
static constexpr u8 PAINT_GAP_BELOW_SP = 8;

void paint() {
  // Use uintptr_t-equivalent arithmetic on AVR (ptrs are 16-bit).
  u16 sp_now;
  asm volatile("in %A0, __SP_L__\n\t"
               "in %B0, __SP_H__\n\t"
               : "=r"(sp_now));
  const u16 bss_end_addr = (u16)&__bss_end;
  if (sp_now <= bss_end_addr + PAINT_GAP_BELOW_SP) {
    // Stack has already grown into bss-adjacent range; refuse to paint
    // (we'd corrupt live frames).
    return;
  }
  const u16 paint_top = (u16)(sp_now - PAINT_GAP_BELOW_SP);
  const u8 prev_sreg  = SREG;
  cli();
  for (u16 a = bss_end_addr; a < paint_top; ++a) {
    *(volatile u8*)a = SENTINEL;
  }
  SREG = prev_sreg;
}

u16 peak_bytes() {
  const u16 bss_end_addr = (u16)&__bss_end;
  // Find the first byte from bss_end upward that is NOT SENTINEL.
  // RAMEND on ATmega32u4 is 0x0AFF. Cap the scan there to avoid
  // walking off SRAM.
  constexpr u16 RAMEND_ADDR = 0x0AFF;
  u16 a                     = bss_end_addr;
  while (a <= RAMEND_ADDR) {
    if (*(volatile u8*)a != SENTINEL) {
      return (u16)(a - bss_end_addr);
    }
    ++a;
  }
  return 0;  // Stack never grew past bss_end — impossible in practice.
}

// Fixed EEPROM address for the logged peak. 510..511 (u16 little-endian).
// Disjoint from dbg::trace's 512+ ring and from storage's 0..43 region.
constexpr u16 PEAK_EEPROM_ADDR = 510;

void log_peak_to_eeprom() {
  const u16 p = peak_bytes();
  eeprom_update_word((u16*)PEAK_EEPROM_ADDR, p);
}

u16 read_logged_peak() {
  return eeprom_read_word((const u16*)PEAK_EEPROM_ADDR);
}

constexpr u16 BREADCRUMB_EEPROM_ADDR = 509;
__attribute__((section(".hightext"))) void breadcrumb(u8 tag) {
  eeprom_update_byte((u8*)BREADCRUMB_EEPROM_ADDR, tag);
}
u8 read_breadcrumb() {
  return eeprom_read_byte((const u8*)BREADCRUMB_EEPROM_ADDR);
}

constexpr u16 LOG_U16_ADDR = 507;
__attribute__((section(".hightext"))) void log_u16(u16 value) {
  eeprom_update_word((u16*)LOG_U16_ADDR, value);
}
u16 read_logged_u16() {
  return eeprom_read_word((const u16*)LOG_U16_ADDR);
}

// One-shot persistent log slot. Writes to EEPROM 505-506 (u16 LE) and
// 504 (u8 tag). Use for boot-time snapshots that must survive the
// for-loop's running breadcrumb churn at 0x1FD / 0x1FB.
constexpr u16 ONESHOT_VALUE_ADDR = 505;
constexpr u16 ONESHOT_TAG_ADDR   = 504;
__attribute__((section(".hightext"))) void log_oneshot(u8 tag, u16 value) {
  eeprom_update_word((u16*)ONESHOT_VALUE_ADDR, value);
  eeprom_update_byte((u8*)ONESHOT_TAG_ADDR, tag);
}

// Second one-shot slot. EEPROM 502-503 (value LE) + 501 (tag).
constexpr u16 ONESHOT2_VALUE_ADDR = 502;
constexpr u16 ONESHOT2_TAG_ADDR   = 501;
__attribute__((section(".hightext"))) void log_oneshot2(u8 tag, u16 value) {
  eeprom_update_word((u16*)ONESHOT2_VALUE_ADDR, value);
  eeprom_update_byte((u8*)ONESHOT2_TAG_ADDR, tag);
}

// Third one-shot slot. EEPROM 499-500 (value LE) + 498 (tag).
constexpr u16 ONESHOT3_VALUE_ADDR = 499;
constexpr u16 ONESHOT3_TAG_ADDR   = 498;
__attribute__((section(".hightext"))) void log_oneshot3(u8 tag, u16 value) {
  eeprom_update_word((u16*)ONESHOT3_VALUE_ADDR, value);
  eeprom_update_byte((u8*)ONESHOT3_TAG_ADDR, tag);
}

}  // namespace stack_probe

#endif  // STACK_PROBE && __AVR__
