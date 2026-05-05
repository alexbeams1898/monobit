#!/usr/bin/env python3
"""Static linker-layout check for the scene-paging build.

Catches known structural bugs in the linker output that would cause
runtime failures impossible to spot in source review:

  * Bug 2 (`.data` LMA inside the SPM-rewritable bank): the linker
    parks `.data`'s flash source image at LOADADDR(.scene.TITLE) +
    SIZEOF(.scene.TITLE), which historically landed inside the bank's
    flash region. Any scene swap (SPM page-write into the bank) then
    overwrites `.data`'s source. On the next reset, `__do_copy_data`
    copies the corrupted bytes into RAM, garbage-ing the size table,
    fx_offset table, vtables, and string literals. Symptom: chip
    boots into garbage, OOB-deref, white screen.

    This check fires if `__data_load_start` lies in [bank_start,
    bank_end). The fix landed in `platform/arduboy/scene.ld:246`.

  * Bank-end overlap with the bootloader: SPM page-erase / page-write
    into bytes >= bootloader_address (0x7000 on Caterina) is silently
    ignored by the chip's hardware lockout, so a bank that grows past
    0x7000 would paginate only partially. Catches a hypothetical
    future bug if scene size ever pushes the bank end past the
    bootloader gate.

Run: `python3 scripts/check_paging_layout.py path/to/rpg.elf`
Exit nonzero on failure; emits a one-line summary to stderr.

Wired into the build: add to `make audit-all` (TODO) and the CI
build job (TODO).
"""

from __future__ import annotations

import argparse
import subprocess
import sys
from dataclasses import dataclass


# Caterina bootloader on Arduboy (ATmega32u4, 32 KB flash).
# SPM into [BOOTLOADER_ADDR, 32K) is locked at the silicon level.
BOOTLOADER_ADDR = 0x7000


@dataclass
class Layout:
    bank_start: int
    bank_size: int
    data_load_start: int
    data_load_end: int

    @property
    def bank_end(self) -> int:
        return self.bank_start + self.bank_size


def parse_nm(elf_path: str) -> Layout:
    """Pull the linker symbols we need from `avr-nm -n`."""
    out = subprocess.check_output(
        ["avr-nm", "-n", elf_path], text=True, stderr=subprocess.STDOUT
    )
    syms: dict[str, int] = {}
    for line in out.splitlines():
        parts = line.split()
        if len(parts) < 3:
            continue
        try:
            addr = int(parts[0], 16)
        except ValueError:
            continue
        name = parts[2]
        syms[name] = addr

    required = [
        "__scene_bank_start",
        "__scene_bank_size",
        "__data_load_start",
        "__data_load_end",
    ]
    missing = [n for n in required if n not in syms]
    if missing:
        sys.stderr.write(
            f"check_paging_layout: missing linker symbol(s): {missing}\n"
            f"  is this a non-paging build? (SCENE_PAGING_ENABLED off)\n"
        )
        sys.exit(2)

    return Layout(
        bank_start=syms["__scene_bank_start"],
        bank_size=syms["__scene_bank_size"],
        data_load_start=syms["__data_load_start"],
        data_load_end=syms["__data_load_end"],
    )


def check_data_lma_outside_bank(L: Layout) -> list[str]:
    errs: list[str] = []
    if L.bank_start <= L.data_load_start < L.bank_end:
        errs.append(
            f".data LMA 0x{L.data_load_start:04X} sits INSIDE the SPM-rewritable "
            f"bank [0x{L.bank_start:04X}, 0x{L.bank_end:04X}). Scene swaps will "
            f"clobber .data's flash source -> next reset reads garbage. "
            f"Fix: scene.ld must place .data via AT(__scene_bank_end) or higher. "
            f"See docs/known-bugs/scene-paging-boot-recovery.md (Bug 2)."
        )
    if L.bank_start <= L.data_load_end <= L.bank_end:
        errs.append(
            f".data LMA end 0x{L.data_load_end:04X} crosses into the bank region "
            f"[0x{L.bank_start:04X}, 0x{L.bank_end:04X}). Same Bug 2 class."
        )
    # SPM page-erase / page-write operate on 128-byte pages. If the
    # bank's last page straddles into .data LMA bytes, every scene
    # swap rewrites that page (erases all 128 B, writes only the
    # cold-copy bytes that land in it) and corrupts .data's flash
    # source. The linker fix that pushes .data past __scene_bank_end
    # numerically isn't enough on its own — bank size must be rounded
    # up to a 128-byte multiple in scene.ld so .data starts on a
    # fresh page.
    PAGE = 128
    last_bank_page_start = ((L.bank_end - 1) // PAGE) * PAGE if L.bank_end > 0 else 0
    next_page_start = last_bank_page_start + PAGE
    if L.bank_end < next_page_start and L.data_load_start < next_page_start:
        errs.append(
            f".data LMA 0x{L.data_load_start:04X} lives in the same SPM page as "
            f"the bank's tail (page [0x{last_bank_page_start:04X}, 0x{next_page_start:04X})). "
            f"SPM page-write on the bank rewrites the entire 128-B page including "
            f".data bytes. Fix: in scene.ld, round __scene_bank_size up to a "
            f"128-byte multiple before computing __scene_bank_end."
        )
    return errs


def check_bank_below_bootloader(L: Layout) -> list[str]:
    errs: list[str] = []
    if L.bank_end > BOOTLOADER_ADDR:
        errs.append(
            f"bank end 0x{L.bank_end:04X} exceeds bootloader at 0x{BOOTLOADER_ADDR:04X}. "
            f"SPM page-write into >= bootloader is silently locked out by silicon -> "
            f"partial scene loads. Reduce scene-bank max size or move bootloader."
        )
    return errs


def check_data_below_bootloader(L: Layout) -> list[str]:
    errs: list[str] = []
    if L.data_load_end > BOOTLOADER_ADDR:
        errs.append(
            f".data LMA end 0x{L.data_load_end:04X} exceeds bootloader at "
            f"0x{BOOTLOADER_ADDR:04X}. .data's source overlaps the bootloader region; "
            f"`avr-objcopy` will likely strip those bytes from the .hex and "
            f"__do_copy_data will read 0xFF. Shrink .data (PROGMEM-ize strings) "
            f"or move scenes."
        )
    return errs


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("elf", help="path to the linked ELF (rpg.elf)")
    args = ap.parse_args()

    L = parse_nm(args.elf)
    errs: list[str] = []
    errs += check_data_lma_outside_bank(L)
    errs += check_bank_below_bootloader(L)
    errs += check_data_below_bootloader(L)

    if errs:
        sys.stderr.write("check_paging_layout: FAIL\n")
        for e in errs:
            sys.stderr.write(f"  - {e}\n")
        return 1

    sys.stdout.write(
        f"check_paging_layout: OK\n"
        f"  bank: [0x{L.bank_start:04X}, 0x{L.bank_end:04X}) size 0x{L.bank_size:X}\n"
        f"  .data LMA: [0x{L.data_load_start:04X}, 0x{L.data_load_end:04X}) "
        f"size 0x{L.data_load_end - L.data_load_start:X}\n"
        f"  bootloader gap: 0x{BOOTLOADER_ADDR - L.data_load_end:X} bytes free "
        f"between .data end and bootloader\n"
    )
    return 0


if __name__ == "__main__":
    sys.exit(main())
