// SPM trampoline interface for kp_boot_32u4 bootloader.
//
// ATmega32u4 hardware-disables the SPM instruction when executed from the
// application section (RWW). The application can only do in-flash
// self-programming by calling into a bootloader-resident trampoline.
//
// kp_boot_32u4 (https://github.com/ahtn/kp_boot_32u4, MIT) exposes a 16-byte
// SPM call gate at flash address (FLASHEND+1 - 16) = 0x7FF0 on ATmega32u4.
// The trampoline takes its arguments in the AVR ABI:
//   r10           — first SPMCSR command bits (the operation: erase/fill/write)
//   r11           — second SPMCSR command bits (post-op cleanup, usually RWWSRE)
//   r0:r1         — optional data word (for fill-temp-buffer)
//   Z (r30:r31)   — target byte address (for erase/write) or word offset (for fill)
//
// The trampoline executes SPM from .boot_extra in the bootloader (NRWW),
// waits for SPMEN to clear, optionally executes a second SPM with the
// cleanup bits, then returns. The application sees this as a regular C
// function call (with custom register conventions enforced via inline asm).
//
// This file is the interface from kp_boot_32u4/interface/kp_boot_32u4.[ch],
// adapted to our project (engine/types.h types, integration with our build).
// See LICENSE / kp_boot_32u4 for upstream attribution.

#pragma once

#include <avr/io.h>

#include "types.h"

// The SPM trampoline lives at the top of flash, 16 bytes below FLASHEND.
// On ATmega32u4 (FLASHEND = 0x7FFF), this is 0x7FF0.
//
// ARDENS_SPM build flag: Ardens doesn't model the SPM-from-app silicon
// restriction (verified by reading src/absim_atmega32u4.hpp:execute_spm
// — no PC-source check). We use this to test the swap manager in
// Ardens without needing a custom bootloader installed: instead of
// calling 0x7FF0 (which holds Cathy3K bytes in Ardens, not an SPM
// helper), we call an in-app stub `app_spm_trampoline_entry` defined
// in platform/arduboy/spm_app_stub.cpp. Real hardware build leaves
// this address as 0x7FF0 and uses the real kp_boot_32u4 bootloader.
#define KP_BOOT_SPM_INTERFACE_SIZE 16
#ifdef ARDENS_SPM
extern "C" void app_spm_trampoline_entry(void);
#define KP_BOOT_SPM_INTERFACE_ADDRESS ((u16)(uintptr_t) & app_spm_trampoline_entry)
#else
#define KP_BOOT_SPM_INTERFACE_ADDRESS ((u32)FLASHEND + 1 - KP_BOOT_SPM_INTERFACE_SIZE)
#endif

namespace kp_boot {

// Erase the flash page containing `addr`. addr is a byte address; it
// will be aligned down to the page boundary (128 B) by the SPM hardware.
// CPU halts during the erase phase (~3.5 ms per page) on RWW-targeting-RWW;
// audio ISR will not run. Plan transitions accordingly.
void spm_erase_page(u16 addr);

// Load one 16-bit word into the SPM temporary page buffer at the given
// word offset (0..63 for 128 B / 2 B-per-word pages). Call 64 times to
// fill a full page, then `spm_write_page()` to commit.
void spm_load_temporary_buffer(u8 word_offset, u16 data_word);

// Write the temporary page buffer to the flash page containing `addr`.
// Must be preceded by a `spm_erase_page(addr)` (the page must be 0xFF).
// CPU halts during the write phase (~4.5 ms per page).
void spm_write_page(u16 addr);

}  // namespace kp_boot
