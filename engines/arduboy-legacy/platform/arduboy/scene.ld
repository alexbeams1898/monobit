/* Scene-bank linker script for Arduboy / ATmega32u4.
 *
 * Derived from the avr-ld default avr5 script (`avr-ld -m avr5
 * --verbose`) with two changes to support scene paging:
 *
 * 1) `OVERLAY __scene_bank_start : NOCROSSREFS { ... }` places all
 *    four `.scene.<NAME>` sections at the SAME VMA (the runtime swap
 *    bank base) but DIFFERENT LMAs (sequential in flash). At runtime
 *    only one scene is paged in; its bytes were linked at the bank
 *    base so internal calls within the scene resolve correctly.
 *    NOCROSSREFS is a build-time gate: if any tagged-into-bank-X
 *    function calls a tagged-into-bank-Y function, the link fails.
 *
 * 2) Custom `.data : AT(__scene_lma_end)` — the default places `.data`
 *    using `> data AT> text`, which uses the text-region cursor.
 *    OVERLAY does not advance that cursor, so by default `.data`'s
 *    LMA collides with `.scene.TITLE`. We override the LMA to be past
 *    the end of all four scene LMAs in flash.
 *
 * After link, the build pipeline extracts each `.scene.<NAME>` to a
 * cold-copy binary via `avr-objcopy --dump-section` and concatenates
 * them into FX flash's data.bin. Then it strips three of the four
 * scene sections from the application ELF, leaving only the boot-
 * resident scene (currently TITLE) in the .hex. The swap manager
 * (engine/scene.cpp + platform/arduboy/scene_spm.cpp) loads the other
 * scenes from FX on demand via SPM page-write into the bank.
 *
 * Spec: docs/scene-paging.md.
 */
/* Default avr5 script — DO NOT EDIT below this line, only the OVERLAY
   block and the .data override are project-specific. */
/* Script for -n */
/* Copyright (C) 2014-2023 Free Software Foundation, Inc.
   Copying and distribution of this script, with or without modification,
   are permitted in any medium without royalty provided the copyright
   notice and this notice are preserved.  */
OUTPUT_FORMAT("elf32-avr","elf32-avr","elf32-avr")
OUTPUT_ARCH(avr:5)
__TEXT_REGION_LENGTH__ = DEFINED(__TEXT_REGION_LENGTH__) ? __TEXT_REGION_LENGTH__ : 128K;
__DATA_REGION_ORIGIN__ = DEFINED(__DATA_REGION_ORIGIN__) ? __DATA_REGION_ORIGIN__ : 0x800060;
__DATA_REGION_LENGTH__ = DEFINED(__DATA_REGION_LENGTH__) ? __DATA_REGION_LENGTH__ : 0xffa0;
__EEPROM_REGION_LENGTH__ = DEFINED(__EEPROM_REGION_LENGTH__) ? __EEPROM_REGION_LENGTH__ : 64K;
__FUSE_REGION_LENGTH__ = DEFINED(__FUSE_REGION_LENGTH__) ? __FUSE_REGION_LENGTH__ : 1K;
__LOCK_REGION_LENGTH__ = DEFINED(__LOCK_REGION_LENGTH__) ? __LOCK_REGION_LENGTH__ : 1K;
__SIGNATURE_REGION_LENGTH__ = DEFINED(__SIGNATURE_REGION_LENGTH__) ? __SIGNATURE_REGION_LENGTH__ : 1K;
__USER_SIGNATURE_REGION_LENGTH__ = DEFINED(__USER_SIGNATURE_REGION_LENGTH__) ? __USER_SIGNATURE_REGION_LENGTH__ : 1K;
MEMORY
{
  text   (rx)   : ORIGIN = 0, LENGTH = __TEXT_REGION_LENGTH__
  data   (rw!x) : ORIGIN = __DATA_REGION_ORIGIN__, LENGTH = __DATA_REGION_LENGTH__
  eeprom (rw!x) : ORIGIN = 0x810000, LENGTH = __EEPROM_REGION_LENGTH__
  fuse      (rw!x) : ORIGIN = 0x820000, LENGTH = __FUSE_REGION_LENGTH__
  lock      (rw!x) : ORIGIN = 0x830000, LENGTH = __LOCK_REGION_LENGTH__
  signature (rw!x) : ORIGIN = 0x840000, LENGTH = __SIGNATURE_REGION_LENGTH__
  user_signatures (rw!x) : ORIGIN = 0x850000, LENGTH = __USER_SIGNATURE_REGION_LENGTH__
}
SECTIONS
{
  /* Read-only sections, merged into text segment: */
  .hash          : { *(.hash)		}
  .dynsym        : { *(.dynsym)		}
  .dynstr        : { *(.dynstr)		}
  .gnu.version   : { *(.gnu.version)	}
  .gnu.version_d   : { *(.gnu.version_d)	}
  .gnu.version_r   : { *(.gnu.version_r)	}
  .rel.init      : { *(.rel.init)		}
  .rela.init     : { *(.rela.init)	}
  .rel.text      :
    {
      *(.rel.text)
      *(.rel.text.*)
      *(.rel.gnu.linkonce.t*)
    }
  .rela.text     :
    {
      *(.rela.text)
      *(.rela.text.*)
      *(.rela.gnu.linkonce.t*)
    }
  .rel.fini      : { *(.rel.fini)		}
  .rela.fini     : { *(.rela.fini)	}
  .rel.rodata    :
    {
      *(.rel.rodata)
      *(.rel.rodata.*)
      *(.rel.gnu.linkonce.r*)
    }
  .rela.rodata   :
    {
      *(.rela.rodata)
      *(.rela.rodata.*)
      *(.rela.gnu.linkonce.r*)
    }
  .rel.data      :
    {
      *(.rel.data)
      *(.rel.data.*)
      *(.rel.gnu.linkonce.d*)
    }
  .rela.data     :
    {
      *(.rela.data)
      *(.rela.data.*)
      *(.rela.gnu.linkonce.d*)
    }
  .rel.ctors     : { *(.rel.ctors)	}
  .rela.ctors    : { *(.rela.ctors)	}
  .rel.dtors     : { *(.rel.dtors)	}
  .rela.dtors    : { *(.rela.dtors)	}
  .rel.got       : { *(.rel.got)		}
  .rela.got      : { *(.rela.got)		}
  .rel.bss       : { *(.rel.bss)		}
  .rela.bss      : { *(.rela.bss)		}
  .rel.plt       : { *(.rel.plt)		}
  .rela.plt      : { *(.rela.plt)		}
  /* Internal text space or external memory.  */
  .text   :
  {
    *(.vectors)
    KEEP(*(.vectors))
    /* For data that needs to reside in the lower 64k of progmem.  */
    *(.progmem.gcc*)
    /* PR 13812: Placing the trampolines here gives a better chance
       that they will be in range of the code that uses them.  */
    . = ALIGN(2);
    __trampolines_start = . ;
    /* The jump trampolines for the 16-bit limited relocs will reside here.  */
    *(.trampolines)
    *(.trampolines*)
    __trampolines_end = . ;
    /* avr-libc expects these data to reside in lower 64K. */
    *libprintf_flt.a:*(.progmem.data)
    *libc.a:*(.progmem.data)
    *(.progmem.*)
    . = ALIGN(2);
    /* For code that needs to reside in the lower 128k progmem.  */
    *(.lowtext)
    *(.lowtext*)
     __ctors_start = . ;
     *(.ctors)
     __ctors_end = . ;
     __dtors_start = . ;
     *(.dtors)
     __dtors_end = . ;
    KEEP(SORT(*)(.ctors))
    KEEP(SORT(*)(.dtors))
    /* From this point on, we do not bother about whether the insns are
       below or above the 16 bits boundary.  */
    *(.init0)  /* Start here after reset.  */
    KEEP (*(.init0))
    *(.init1)
    KEEP (*(.init1))
    *(.init2)  /* Clear __zero_reg__, set up stack pointer.  */
    KEEP (*(.init2))
    *(.init3)
    KEEP (*(.init3))
    *(.init4)  /* Initialize data and BSS.  */
    KEEP (*(.init4))
    *(.init5)
    KEEP (*(.init5))
    *(.init6)  /* C++ constructors.  */
    KEEP (*(.init6))
    *(.init7)
    KEEP (*(.init7))
    *(.init8)
    KEEP (*(.init8))
    *(.init9)  /* Call main().  */
    KEEP (*(.init9))
    *(.text)
    . = ALIGN(2);
    *(.text.*)
    . = ALIGN(2);
    *(.fini9)  /* _exit() starts here.  */
    KEEP (*(.fini9))
    *(.fini8)
    KEEP (*(.fini8))
    *(.fini7)
    KEEP (*(.fini7))
    *(.fini6)  /* C++ destructors.  */
    KEEP (*(.fini6))
    *(.fini5)
    KEEP (*(.fini5))
    *(.fini4)
    KEEP (*(.fini4))
    *(.fini3)
    KEEP (*(.fini3))
    *(.fini2)
    KEEP (*(.fini2))
    *(.fini1)
    KEEP (*(.fini1))
    *(.fini0)  /* Infinite loop after program termination.  */
    KEEP (*(.fini0))
    /* For code that needs not to reside in the lower progmem.  */
    *(.hightext)
    *(.hightext*)
    *(.progmemx.*)
    . = ALIGN(2);
    /* For tablejump instruction arrays.  We do not relax
       JMP / CALL instructions within these sections.  */
    *(.jumptables)
    *(.jumptables*)
    _etext = . ;
  }  > text

  /* Bank base must be page-aligned (128 B) so SPM page-erase / page-write
     ops on the bank don't spill into the preceding .text page. Without
     this, erasing the bank's first page would also wipe the tail of
     .text, corrupting CORE code. ALIGN(128) advances . to the next
     128-byte boundary, leaving a small 0xFF gap. */
  . = ALIGN(128);
  __scene_bank_start = .;
  /* TITLE bank: VMA + LMA both at __scene_bank_start. Its bytes are
     in the .hex (kept by the strip step). Boot reads execute from the
     bank base directly, no SPM needed. */
  .scene.TITLE __scene_bank_start : { *(.scene.TITLE) } > text

  /* MAIN_MENU / GATE / PLAY: all VMA = __scene_bank_start (overlay
     into the same bank), but LMAs parked at 0x10000+ (out of the
     chip's 32 KB physical-flash range entirely). Their bytes get
     extracted by avr-objcopy --dump-section into cold.<NAME>.bin
     and bundled into FX flash's data.bin; the build pipeline strips
     these sections from the .hex (Makefile:236) before flashing.

     Why high LMAs: if these sat in real flash address space, the
     linker's LMA cursor would advance past them and place `.data`
     past the bootloader gate (0x7000), bricking boot. Parking them
     in unused address territory keeps `.data` LMA right after
     TITLE's flash bytes, where it belongs. */
  OVERLAY __scene_bank_start : NOCROSSREFS AT(0x10000)
  {
    .scene.MAIN_MENU { *(.scene.MAIN_MENU) }
    .scene.GATE      { *(.scene.GATE) }
    .scene.PLAY      { *(.scene.PLAY) }
  }
  /* SPM page size on ATmega32u4 is 128 B. The bank's last page is
     erased+rewritten by every scene swap; if .data's LMA lives in
     that page (even past the highest bank byte), SPM corrupts it.
     Round bank size UP to a 128-byte multiple so .data LMA starts
     at a fresh page boundary the SPM loop never touches. Without
     this, Bug 2 returns even after .data LMA is moved past
     __scene_bank_end numerically — see docs/known-bugs/. */
  __scene_bank_size = (MAX(SIZEOF(.scene.TITLE),
                       MAX(SIZEOF(.scene.MAIN_MENU),
                       MAX(SIZEOF(.scene.GATE), SIZEOF(.scene.PLAY)))) + 127) & 0xFFFFFF80;
  __scene_bank_end = __scene_bank_start + __scene_bank_size;
  __scene_lma_end = LOADADDR(.scene.PLAY) + SIZEOF(.scene.PLAY);
  /* CI's older binutils (gcc-avr 7.x) refuses to resolve
     __scene_bank_size / __scene_bank_end across the boundary between
     these symbol assignments and the .data section's VMA / AT()
     expressions below — emits "undefined symbol referenced in
     expression" even though the symbols are clearly defined here.
     Newer binutils (MSYS2 2.x) handles it. To stay compatible,
     spell the page-aligned bank size out fully in every expression
     that needs it (here and in the .data section). The formula
     stays in lockstep with __scene_bank_size's definition above
     (MAX(scene sizes) + 127, masked to 128-byte alignment). */
  . = LOADADDR(.scene.TITLE) +
      ((MAX(SIZEOF(.scene.TITLE),
        MAX(SIZEOF(.scene.MAIN_MENU),
        MAX(SIZEOF(.scene.GATE), SIZEOF(.scene.PLAY)))) + 127) & 0xFFFFFF80);

  /* .data LMA placed AFTER the entire bank region in flash.
     CRITICAL: prior versions placed .data's LMA right after TITLE's
     in-place bytes (at 0x4B42), which lived INSIDE the SPM-rewritable
     bank (bank covers __scene_bank_start..__scene_bank_end). Every
     scene swap rewrote the bank with another scene's bytes, clobbering
     .data's flash source image. On the next reset, __do_copy_data
     read those (now corrupted) bytes into RAM, garbage-ing the size
     table, fx_offset table, vtables, and string literals. Symptom:
     after title→A→reset cycle, scene::size_for(TITLE) returned 0x4915
     instead of 450, page_in wrote zeros, chip executed garbage at
     bank base, white screen / OOB / hang.
     Fix: place .data LMA past __scene_bank_end so SPM-paging never
     touches it. Tight fit between bank-end and bootloader (0x7000)
     forces strings / switch-tables to live in PROGMEM, which they
     should anyway. */
  /* AT() expression spelled out as LOADADDR(.scene.TITLE) +
     __scene_bank_size to avoid a forward-reference to
     __scene_bank_end. Older binutils (Ubuntu's gcc-avr 7.x in CI)
     don't resolve __scene_bank_end across the symbol-assignment /
     section-definition boundary cleanly and emit
     "undefined symbol `__scene_bank_end` referenced in expression"
     even though it's defined three lines up. The expanded form
     resolves the same address (LOADADDR(.scene.TITLE) IS the bank
     LMA, == __scene_bank_start), but uses only symbols the older
     linker has already concretized at this point. */
  .data (__scene_bank_start +
         ((MAX(SIZEOF(.scene.TITLE),
           MAX(SIZEOF(.scene.MAIN_MENU),
           MAX(SIZEOF(.scene.GATE), SIZEOF(.scene.PLAY)))) + 127) & 0xFFFFFF80))
        : AT(LOADADDR(.scene.TITLE) +
             ((MAX(SIZEOF(.scene.TITLE),
               MAX(SIZEOF(.scene.MAIN_MENU),
               MAX(SIZEOF(.scene.GATE), SIZEOF(.scene.PLAY)))) + 127) & 0xFFFFFF80))
  {
    PROVIDE (__data_start = .) ;
    *(.data)
    *(.data*)
    *(.gnu.linkonce.d*)
    *(.rodata)
    *(.rodata*)
    *(.gnu.linkonce.r*)
    . = ALIGN(2);
    _edata = . ;
    PROVIDE (__data_end = .) ;
  } > data
  .bss  ADDR(.data) + SIZEOF (.data)   : AT (ADDR (.bss))
  {
     PROVIDE (__bss_start = .) ;
    *(.bss)
     *(.bss*)
     *(COMMON)
     PROVIDE (__bss_end = .) ;
  }  > data
   __data_load_start = LOADADDR(.data);
   __data_load_end = __data_load_start + SIZEOF(.data);
  /* Global data not cleared after reset.  */
  .noinit  ADDR(.bss) + SIZEOF (.bss)  :  AT (ADDR (.noinit))
  {
     PROVIDE (__noinit_start = .) ;
    *(.noinit .noinit.* .gnu.linkonce.n.*)
     PROVIDE (__noinit_end = .) ;
     _end = . ;
     PROVIDE (__heap_start = .) ;
  }  > data
  .eeprom  :
  {
    /* See .data above...  */
    KEEP(*(.eeprom*))
     __eeprom_end = . ;
  }  > eeprom
  .fuse  :
  {
    KEEP(*(.fuse))
    KEEP(*(.lfuse))
    KEEP(*(.hfuse))
    KEEP(*(.efuse))
  }  > fuse
  .lock  :
  {
    KEEP(*(.lock*))
  }  > lock
  .signature  :
  {
    KEEP(*(.signature*))
  }  > signature
  /* Stabs debugging sections.  */
  .stab          0 : { *(.stab) }
  .stabstr       0 : { *(.stabstr) }
  .stab.excl     0 : { *(.stab.excl) }
  .stab.exclstr  0 : { *(.stab.exclstr) }
  .stab.index    0 : { *(.stab.index) }
  .stab.indexstr 0 : { *(.stab.indexstr) }
  /* `LINKER_VERSION;` was here in newer (binutils 2.36+) avr-ld
     --verbose output; older toolchains (e.g. CI's avr-gcc 7.3.0)
     don't recognise the builtin and fail the link with "cannot find
     LINKER_VERSION". Dropping it costs nothing — `.comment` is
     INFO-only and stripped from the .hex regardless. */
  .comment 0 (INFO) : { *(.comment); }
  .gnu.build.attributes : { *(.gnu.build.attributes .gnu.build.attributes.*) }
  .note.gnu.build-id   : { *(.note.gnu.build-id) }
  /* DWARF debug sections.
     Symbols in the DWARF debugging sections are relative to the beginning
     of the section so we begin them at 0.  */
  /* DWARF 1.  */
  .debug          0 : { *(.debug) }
  .line           0 : { *(.line) }
  /* GNU DWARF 1 extensions.  */
  .debug_srcinfo  0 : { *(.debug_srcinfo) }
  .debug_sfnames  0 : { *(.debug_sfnames) }
  /* DWARF 1.1 and DWARF 2.  */
  .debug_aranges  0 : { *(.debug_aranges) }
  .debug_pubnames 0 : { *(.debug_pubnames) }
  /* DWARF 2.  */
  .debug_info     0 : { *(.debug_info .gnu.linkonce.wi.*) }
  .debug_abbrev   0 : { *(.debug_abbrev) }
  .debug_line     0 : { *(.debug_line .debug_line.* .debug_line_end) }
  .debug_frame    0 : { *(.debug_frame) }
  .debug_str      0 : { *(.debug_str) }
  .debug_loc      0 : { *(.debug_loc) }
  .debug_macinfo  0 : { *(.debug_macinfo) }
  /* SGI/MIPS DWARF 2 extensions.  */
  .debug_weaknames 0 : { *(.debug_weaknames) }
  .debug_funcnames 0 : { *(.debug_funcnames) }
  .debug_typenames 0 : { *(.debug_typenames) }
  .debug_varnames  0 : { *(.debug_varnames) }
  /* DWARF 3.  */
  .debug_pubtypes 0 : { *(.debug_pubtypes) }
  .debug_ranges   0 : { *(.debug_ranges) }
  /* DWARF 5.  */
  .debug_addr     0 : { *(.debug_addr) }
  .debug_line_str 0 : { *(.debug_line_str) }
  .debug_loclists 0 : { *(.debug_loclists) }
  .debug_macro    0 : { *(.debug_macro) }
  .debug_names    0 : { *(.debug_names) }
  .debug_rnglists 0 : { *(.debug_rnglists) }
  .debug_str_offsets 0 : { *(.debug_str_offsets) }
  .debug_sup      0 : { *(.debug_sup) }
}

