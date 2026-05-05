// In-application SPM helper — Ardens-only testing path.
//
// On real ATmega32u4 hardware, SPM from the application section is
// silently disabled by silicon (per Microchip / AVR109). The real path
// for in-app self-flashing is the kp_boot_32u4 bootloader's SPM
// trampoline at flash address 0x7FF0.
//
// Ardens does NOT model the silicon restriction (verified in
// src/absim_atmega32u4.hpp:execute_spm — the only target guard is
// "don't write the bootloader", with no PC-source check). For Ardens
// testing we substitute an in-app stub at this symbol's address so
// the swap manager can be exercised end-to-end without a custom
// bootloader being installed in the simulated flash.
//
// ABI matches kp_boot_32u4's call_spm: r10 + r11 = SPMCSR commands,
// r0:r1 = optional data word, Z = target byte address. The kp_boot_spm
// helpers `call` into this address (configured via
// KP_BOOT_SPM_INTERFACE_ADDRESS macro pointing at this symbol).
//
// Build: included only when ARDENS_SPM=1.

#include <avr/io.h>

#include "types.h"

extern "C" {

// `noinline` forces an out-of-line emission; `used` keeps it past
// --gc-sections; the asm body is the same SPMCSR sequence as
// kp_boot_32u4. Not naked: GCC adds prologue/epilogue, but it
// preserves r10/r11 across them per AVR ABI (call-saved). r0:r1 are
// caller-saved but our asm only reads them.
// Disable LTO on this TU to avoid GCC's LTO IR stage mangling the
// inline-asm operand metadata (saw "garbage at end of line" at the
// .s:133 in our LTO build with named-operand syntax). The function
// is small (~16 bytes); losing LTO on it costs nothing and keeps the
// asm exactly as written through link.
// Finer-grained cli/sei: protect only the SPMCSR-write + spm pair, not
// the wait loop or the second SPMCSR-write. Real silicon's SPMEN bit
// clears on a hardware-fixed schedule (~3.5 ms erase, ~4.5 ms write
// per page) regardless of interrupt state — so leaving interrupts
// enabled during the wait loop is safe on hardware.
//
// On Ardens, SPM completion fires through a peripheral-queue scheduler
// that advances on instruction-boundary events. With cli() held across
// the whole sequence, the scheduler's callback doesn't fire and SPMEN
// stays set forever (or the buffer ends up zeros). Re-enabling
// interrupts during the wait loop lets the audio ISR (Timer4 OVF,
// 16 kHz) tick the scheduler, completion fires, SPMEN clears, loop
// exits.
//
// The audio ISR cannot collide with SPM here: it touches only SRAM
// (audio_phase / audio_phase_inc) and one I/O bit (PORTC bit 6 for
// the speaker pin). It does NOT touch SPI or the bank's flash region.
__attribute__((noinline, used, optimize("no-lto"))) void app_spm_trampoline_entry(void) {
  // Save SREG (caller's I-flag) so we can restore it after the SPM
  // sequence. The SPMCSR+spm pair must run interrupts-off; the wait
  // loop runs interrupts-on (so Ardens' SPM scheduler can advance via
  // the audio ISR's clock ticks). Real silicon's SPMEN bit clears on
  // a hardware-fixed schedule independent of interrupt state, so the
  // same code is also correct on hardware.
  //
  // Use r24 for the SREG snapshot — NOT r0. r0:r1 is the data word
  // for PAGE_LOAD (set up by spm_leap_cmd's prologue before the call
  // here); clobbering it corrupts the temp-buffer contents. r24 is
  // call-clobbered scratch per AVR ABI; the noinline / no-lto C++
  // function wrapper takes care of any prologue save/restore needed.
  asm volatile("in r24, 0x3f\n\t"   // r24 = SREG snapshot (caller's I-flag)
               "cli\n\t"            // interrupts off for SPMCSR+spm pair
               "out 0x37, r10\n\t"  // SPMCSR is at I/O addr 0x37 on 32u4
               "spm\n\t"
               "out 0x3f, r24\n\t"  // restore SREG
               "1:\n\t"
               "in r10, 0x37\n\t"
               "sbrc r10, 0\n\t"  // SPMEN bit (bit 0)
               "rjmp 1b\n\t"
               "cli\n\t"  // interrupts off for second SPMCSR+spm
               "out 0x37, r11\n\t"
               "spm\n\t"
               "out 0x3f, r24\n\t"  // restore SREG
               :
               :
               : "r10", "r24", "memory");
}

}  // extern "C"
