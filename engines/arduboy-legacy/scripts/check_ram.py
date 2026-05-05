#!/usr/bin/env python3
"""Fail the build when static RAM usage crosses a configurable ceiling.

Run as:
    python scripts/check_ram.py <elf> <pct>

Reads .data + .bss + .noinit from `avr-size --format=avr` output and
exits non-zero with a loud banner if the sum exceeds `pct`% of the
ATmega32u4's 2560-byte SRAM.

Why this matters: on AVR there's no MMU, so globals and the stack share
one address range. When .bss+.data crowds the stack's growth direction,
deep call chains silently corrupt the top globals. See docs for the
stack-smashing incident. Default ceiling is 85%, leaving 384 B formal
stack headroom; cross-checked against `make stack-report` (callgraph-
measured peak + 64 B safety pad). The static analyzer is fragile —
small code changes can reveal newly-matched .su frames and swing the
reported peak by ~100 B, so treat the number as a lower bound and keep
the formal headroom comfortably above the worst case seen.
"""

from __future__ import annotations
import subprocess
import sys
from pathlib import Path

RAM_TOTAL = 2560  # ATmega32u4


def parse_avr_size(elf: Path) -> int:
    """Return the sum of .data + .bss + .noinit in bytes."""
    out = subprocess.check_output(
        ["avr-size", "--mcu=atmega32u4", "--format=avr", str(elf)],
        text=True,
    )
    # `avr-size --format=avr` output includes a line like:
    #   Data:       1993 bytes (77.9% Full)
    # We want the raw byte count.
    for line in out.splitlines():
        s = line.strip()
        if s.startswith("Data:"):
            return int(s.split()[1])
    raise RuntimeError(f"could not parse RAM usage from avr-size output:\n{out}")


def main() -> int:
    if len(sys.argv) != 3:
        print("usage: check_ram.py <elf> <ceiling-pct>", file=sys.stderr)
        return 2
    elf = Path(sys.argv[1])
    pct = int(sys.argv[2])
    ceiling = RAM_TOTAL * pct // 100
    used = parse_avr_size(elf)
    used_pct = 100.0 * used / RAM_TOTAL
    if used > ceiling:
        # Loud banner + concrete next-step pointers. If this fires, the dev
        # just wrote something that pushed globals past our safety margin;
        # they probably need the audit framework from the last pass.
        print("", file=sys.stderr)
        print("=" * 60, file=sys.stderr)
        print(f"  RAM GATE FAILED: {used} / {RAM_TOTAL} B ({used_pct:.1f}%)", file=sys.stderr)
        print(f"  Ceiling is {pct}% ({ceiling} B). Over by {used - ceiling} B.", file=sys.stderr)
        print("", file=sys.stderr)
        print("  Top RAM consumers (run to audit):", file=sys.stderr)
        print(f"    avr-nm --size-sort --radix=d {elf} | grep ' [BbDd] ' | tail -20", file=sys.stderr)
        print("", file=sys.stderr)
        print("  To intentionally ship over ceiling:", file=sys.stderr)
        print(f"    make RAM_CEILING_PCT=92", file=sys.stderr)
        print("=" * 60, file=sys.stderr)
        return 1
    # Under ceiling: quiet OK line so the build output still has a trace
    # showing the gate ran.
    print(f"RAM gate OK: {used} B / {RAM_TOTAL} B ({used_pct:.1f}%, ceiling {pct}%)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
