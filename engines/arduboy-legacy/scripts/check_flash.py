#!/usr/bin/env python3
"""Fail the build when program flash crosses the *usable* ceiling.

Run as:
    python scripts/check_flash.py <elf> <pct>

The ATmega32u4 has 32,768 B of flash total. The Arduboy ships with the
Caterina USB bootloader occupying the top 4,096 B (addresses 28672..32767),
so the *usable* program area is 28,672 B — not 32,768.

avr-gcc / avr-size both report the percentage against 32 KB, which makes
the binary look like it has more headroom than it does. This gate uses the
real 28,672 B ceiling.

Why this matters: if the binary spills past 28,672 B it overwrites the
bootloader region. On real hardware that bricks USB-flashing; on Ardens
it tends to manifest as the device booting to a black screen because the
reset vector + IRQ table get clobbered.
"""

from __future__ import annotations
import os
import subprocess
import sys
from pathlib import Path

# ATmega32u4 has 32,768 B of flash; the bootloader reserves the top of it.
# Default = 4096 B for Caterina (the stock Arduino Leonardo / original
# Arduboy bootloader). Some community Arduboys ship Cathy3K (3072 B), and
# programmer-only builds skip the bootloader entirely (0 B).
# Override at the make line: BOOTLOADER_BYTES=3072 make
FLASH_TOTAL_PHYSICAL = 32768
BOOTLOADER_BYTES     = int(os.environ.get("BOOTLOADER_BYTES", "4096"))
FLASH_USABLE         = FLASH_TOTAL_PHYSICAL - BOOTLOADER_BYTES


def parse_program_bytes(elf: Path) -> int:
    """Return the total flash byte count.

    Sums every CONTENTS+ALLOC+LOAD section (these are the bytes that
    physically ship in the .hex). This includes `.text`, `.data`, AND
    the project-specific `.scene.*` swap-bank sections introduced by
    scene-paging. `avr-size`'s `Program:` line ONLY counts
    `.text + .data + .bootloader` and silently undercounts the binary
    once `.scene.*` sections exist — using it here would let the binary
    spill past the bootloader boundary undetected.
    """
    out = subprocess.check_output(
        ["avr-objdump", "-h", str(elf)],
        text=True,
    )
    total = 0
    for line in out.splitlines():
        # objdump section table rows look like:
        #   "  2 .scene.GATE   00000844  00006350  00006350  000063e4  2**0"
        parts = line.split()
        if len(parts) < 6 or not parts[0].isdigit():
            continue
        # Filter to flash-resident sections: name starts with `.` and
        # the next line will have the flags. Easier: just skip well-known
        # non-flash sections by name. .data IS flash-resident (the
        # initialized-data PROGMEM image is in flash; runtime copies it
        # into RAM at startup), so include it.
        name = parts[1]
        size = int(parts[2], 16)
        if name in (".bss", ".comment", ".note.gnu.avr.deviceinfo"):
            continue
        if name.startswith(".debug") or name.startswith(".stab"):
            continue
        if size == 0:
            continue
        total += size
    if total == 0:
        raise RuntimeError(f"could not parse any sections from avr-objdump:\n{out}")
    return total


def check_data_lma_in_flash(elf: Path) -> tuple[int, int] | None:
    """Verify .data's LMA falls below the bootloader boundary.

    Returns (lma, lma_end) when the LMA crosses into bootloader range,
    None when fine. The OVERLAY linker pattern places .data after the
    last scene bank's LMA — if cold-copy LMAs are inside real flash
    space, .data ends up near the top of physical flash even though
    the cold-copy bytes are stripped before flashing. See
    docs/bootloader.md "The layout trap" for the full story.

    A binary that passes the size gate but fails this check will boot
    to the ARDUBOY-FX-LOADER selection screen instead of the game on
    Ardens, and will brick USB-flashing on real hardware.
    """
    out = subprocess.check_output(
        ["avr-objdump", "-h", str(elf)],
        text=True,
    )
    # objdump rows: idx name size vma lma file_off align
    for line in out.splitlines():
        parts = line.split()
        if len(parts) < 6 or not parts[0].isdigit():
            continue
        if parts[1] != ".data":
            continue
        size = int(parts[2], 16)
        lma  = int(parts[4], 16)
        end  = lma + size
        if end > FLASH_USABLE:
            return (lma, end)
        return None
    return None


def main() -> int:
    if len(sys.argv) != 3:
        print("usage: check_flash.py <elf> <ceiling-pct>", file=sys.stderr)
        return 2
    elf = Path(sys.argv[1])
    pct = int(sys.argv[2])
    ceiling = FLASH_USABLE * pct // 100
    used = parse_program_bytes(elf)
    used_pct = 100.0 * used / FLASH_USABLE

    # Layout-trap check: even when total bytes fit, .data's LMA can
    # land in the bootloader region under the OVERLAY scene-paging
    # pattern. See docs/bootloader.md "The layout trap".
    #
    # SKIP_LAYOUT_GATE=1 disables this check for analysis-only variants
    # (release-nolto, used by check_stack.py). Those binaries are
    # measurement artifacts, never flashed; their .data LMA is allowed
    # to land anywhere because no flash tool ever sees them.
    skip_layout = os.environ.get("SKIP_LAYOUT_GATE", "0") == "1"
    layout_trap = None if skip_layout else check_data_lma_in_flash(elf)
    if layout_trap is not None:
        lma, end = layout_trap
        print("", file=sys.stderr)
        print("=" * 60, file=sys.stderr)
        print(f"  FLASH GATE FAILED: .data LMA crosses bootloader.", file=sys.stderr)
        print(f"  .data is loaded at 0x{lma:04X}..0x{end:04X};", file=sys.stderr)
        print(f"  ceiling is 0x{FLASH_USABLE:04X} ({FLASH_USABLE} B).", file=sys.stderr)
        print(f"  Cold-copy LMAs in scene.ld may have shifted past", file=sys.stderr)
        print(f"  the chip's 32 KB flash range. See docs/bootloader.md", file=sys.stderr)
        print(f"  'The layout trap' for the fix.", file=sys.stderr)
        print("=" * 60, file=sys.stderr)
        return 1

    if used > ceiling:
        print("", file=sys.stderr)
        print("=" * 60, file=sys.stderr)
        print(f"  FLASH GATE FAILED: {used} / {FLASH_USABLE} B usable ({used_pct:.1f}%)", file=sys.stderr)
        print(f"  Ceiling is {pct}% ({ceiling} B). Over by {used - ceiling} B.", file=sys.stderr)
        print(f"  (Physical flash is {FLASH_TOTAL_PHYSICAL} B; Caterina bootloader", file=sys.stderr)
        print(f"  reserves the top {BOOTLOADER_BYTES} B and is off-limits.)", file=sys.stderr)
        print("", file=sys.stderr)
        print("  Top flash consumers (run to audit):", file=sys.stderr)
        print(f"    avr-nm --size-sort --radix=d {elf} | grep ' [Tt] ' | tail -20", file=sys.stderr)
        print(f"    avr-objdump -h {elf}", file=sys.stderr)
        print("", file=sys.stderr)
        print("  To intentionally ship over ceiling (won't help if past 100%):", file=sys.stderr)
        print(f"    make FLASH_CEILING_PCT=110", file=sys.stderr)
        print("=" * 60, file=sys.stderr)
        return 1
    print(f"FLASH gate OK: {used} B / {FLASH_USABLE} B usable ({used_pct:.1f}%, ceiling {pct}%)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
