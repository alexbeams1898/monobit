// kp_boot_32u4 SPM trampoline interface — implementation.
//
// Calls the bootloader's SPM helper at KP_BOOT_SPM_INTERFACE_ADDRESS.
// The trampoline expects: r10/r11 = SPMCSR command bytes, r0:r1 =
// optional data word, Z (r30:r31) = target byte address.
//
// All register setup + the call live in a SINGLE asm-volatile block.
// r10/r11 are call-saved per the AVR ABI; if we set them in one block
// and called the trampoline from another, the C call sequence's
// callee-prologue could clobber them before the trampoline read them.
// Splitting the asm-volatile blocks introduces a window where the
// compiler can emit code that uses our registers as scratch.
//
// Adapted from kp_boot_32u4/interface/kp_boot_32u4.c (MIT, jem@seethis.link).

#include "kp_boot_spm.h"

namespace kp_boot {

static void spm_leap_cmd(u16 addr, u8 spm_cmd, u8 spm_cmd2, u16 opt_value) {
  asm volatile("push r10\n\t"
               "push r11\n\t"
               "push r0\n\t"
               "push r1\n\t"
               "push r30\n\t"
               "push r31\n\t"

               "movw r0, %[opt_value]\n\t"  // r0:r1 = data word
               "mov r10, %[spm_cmd]\n\t"    // r10 = first SPMCSR command
               "mov r11, %[spm_cmd2]\n\t"   // r11 = second SPMCSR command (cleanup)
               "mov r30, %A[addr]\n\t"      // Z lo = SPM target byte address
               "mov r31, %B[addr]\n\t"

               "call %x[SPM_ADDR]\n\t"  // %x suppresses gas's gs() decoration

               "pop r31\n\t"
               "pop r30\n\t"
               "pop r1\n\t"
               "pop r0\n\t"
               "pop r11\n\t"
               "pop r10\n\t"
               :
               : [spm_cmd] "r"(spm_cmd), [spm_cmd2] "r"(spm_cmd2), [opt_value] "r"(opt_value),
                 [addr] "r"(addr), [SPM_ADDR] "i"(KP_BOOT_SPM_INTERFACE_ADDRESS)
               : "memory");
}

void spm_erase_page(u16 addr) {
  spm_leap_cmd(addr, (1 << SPMEN) | (1 << PGERS), (1 << SPMEN) | (1 << RWWSRE), 0);
}

void spm_load_temporary_buffer(u8 word_offset, u16 data_word) {
  spm_leap_cmd((u16)word_offset, (1 << SPMEN), 0, data_word);
}

void spm_write_page(u16 addr) {
  spm_leap_cmd(addr, (1 << SPMEN) | (1 << PGWRT), (1 << SPMEN) | (1 << RWWSRE), 0);
}

}  // namespace kp_boot
