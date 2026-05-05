#!/usr/bin/env python3
"""Decode a dbg::trace byte stream into a human-readable timeline.

Records are 5 bytes each:

    [0]  0xAA framing
    [1]  tag
    [2]  value lo
    [3]  value hi
    [4]  XOR of bytes [0..3]

The parser walks the input byte-by-byte, looks for a valid 5-byte
record at each candidate framing position, and prints one line per
valid record. Bytes between valid records are reported as "skipped"
so the operator knows when stream cuts happened (e.g. UART buffer
overflow on Ardens, audio ISR jitter, etc.).

Usage:
    python tools/dbg_trace/parse.py trace.bin
    python tools/dbg_trace/parse.py trace.bin --annotate annotations.txt
    cat trace.bin | python tools/dbg_trace/parse.py -

The annotations file maps tag -> string, one per line, hex prefix
optional:

    0x01 reached commit_slot_change entry
    0x02 dest_off
    3    save_erase_sector returned

Lines starting with `#` and blank lines are ignored.
"""

from __future__ import annotations

import argparse
import sys
from pathlib import Path

FRAME_BYTE = 0xAA


def parse_annotations(path: Path | None) -> dict[int, str]:
    """Read tag -> string mapping. Tolerant: hex (0x..), decimal, both
    work. Empty / comment lines skipped."""
    if path is None:
        return {}
    out: dict[int, str] = {}
    for raw in path.read_text(encoding="utf-8").splitlines():
        line = raw.strip()
        if not line or line.startswith("#"):
            continue
        # Split on first whitespace; left = tag, right = description.
        parts = line.split(maxsplit=1)
        if len(parts) != 2:
            continue
        try:
            tag = int(parts[0], 0)
        except ValueError:
            continue
        out[tag] = parts[1]
    return out


def find_record(data: bytes, start: int) -> tuple[int, int, int] | None:
    """Try to read a valid record at `start`. Returns (tag, value,
    next_start) on success or None if no valid record fits."""
    if start + 5 > len(data):
        return None
    if data[start] != FRAME_BYTE:
        return None
    tag = data[start + 1]
    lo = data[start + 2]
    hi = data[start + 3]
    cksum = data[start + 4]
    want = FRAME_BYTE ^ tag ^ lo ^ hi
    if cksum != want:
        return None
    # Reserved tag values are not legitimate.
    if tag in (0x00, 0xFF, FRAME_BYTE):
        return None
    value = lo | (hi << 8)
    return tag, value, start + 5


def main() -> int:
    ap = argparse.ArgumentParser(description="Decode dbg::trace stream")
    ap.add_argument("input", help="path to captured byte stream, or - for stdin")
    ap.add_argument(
        "--annotate",
        type=Path,
        default=None,
        help="optional file mapping tag -> description",
    )
    ap.add_argument(
        "--show-skipped",
        action="store_true",
        help="print one line per byte that didn't form a valid record",
    )
    args = ap.parse_args()

    if args.input == "-":
        data = sys.stdin.buffer.read()
    else:
        data = Path(args.input).read_bytes()

    annotations = parse_annotations(args.annotate)

    n = len(data)
    i = 0
    record_idx = 0
    skipped_run = 0

    while i < n:
        rec = find_record(data, i)
        if rec is None:
            skipped_run += 1
            i += 1
            continue
        if skipped_run > 0 and args.show_skipped:
            print(f"... skipped {skipped_run} unaligned byte(s)")
        skipped_run = 0
        tag, value, next_i = rec
        anno = annotations.get(tag, "")
        # Pad index for stable column width up to 9999 records — dense
        # traces beyond that are usually a sign we're tracing too hot a
        # path and should drop tags rather than chase the formatting.
        anno_part = f"  ({anno})" if anno else ""
        print(f"[{record_idx:>4}] tag=0x{tag:02X} value=0x{value:04X}{anno_part}")
        record_idx += 1
        i = next_i

    if skipped_run > 0 and args.show_skipped:
        print(f"... skipped {skipped_run} trailing unaligned byte(s)")

    if record_idx == 0:
        print("(no valid records found — wrong file? framing corrupted?)", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
