#!/usr/bin/env python3
"""Measure compressibility of baked PROGMEM image arrays.

Reads <name>_data from images.cpp and reports several lossless byte
compression schemes. Each scheme accounts for a runtime decoder cost
estimate so the comparison is honest about net flash impact.

Pure measurement — never writes to the build tree. Tells us whether
any scheme is worth implementing for that specific image.

    python scripts/_compress_check.py TITLE
    python scripts/_compress_check.py FOREST
"""

from __future__ import annotations

import argparse
import re
import sys
from collections import Counter
from pathlib import Path

ROOT   = Path(__file__).resolve().parents[1]
IMAGES = ROOT / "games" / "rpg" / "images.cpp"


def extract_image(name: str) -> bytes:
    text = IMAGES.read_text(encoding="utf-8")
    pat  = re.compile(rf"const u8 {re.escape(name)}_data\[1024\] PROGMEM = \{{(.*?)\}};",
                      re.DOTALL)
    m = pat.search(text)
    if not m:
        raise SystemExit(f"ERROR: {name}_data not found")
    return bytes(int(x, 16) for x in re.findall(r"0x[0-9A-Fa-f]+", m.group(1)))


# ---------- byte-pair RLE: emit (count, byte) pairs ----------

def rle_pairs(data: bytes, max_run: int = 255) -> bytes:
    """Standard byte-pair RLE: count (1 B, 1..max_run) + byte (1 B).
    Always 2 bytes per run."""
    out = bytearray()
    i = 0
    while i < len(data):
        run_byte = data[i]
        n = 1
        while i + n < len(data) and data[i + n] == run_byte and n < max_run:
            n += 1
        out.extend([n, run_byte])
        i += n
    return bytes(out)


# ---------- escape-byte RLE: bytes pass through, escape marks runs ----------

def rle_escape(data: bytes, escape: int = 0xC0) -> bytes:
    """Escape-byte RLE — most bytes pass through verbatim. Two consecutive
    matching bytes trigger an escape sequence: ESC + count + byte. Wins
    for data with mixed runs and unique bytes (most images)."""
    out = bytearray()
    i = 0
    while i < len(data):
        b = data[i]
        # Escape collisions: lone escape byte must be encoded as ESC + 1 + ESC.
        if b == escape:
            out.extend([escape, 1, escape])
            i += 1
            continue
        # Look for a run of length >= 2.
        n = 1
        while i + n < len(data) and data[i + n] == b and n < 255:
            n += 1
        if n >= 2:
            out.extend([escape, n, b])
            i += n
        else:
            out.append(b)
            i += 1
    return bytes(out)


# ---------- naive LZ77 estimate (limited: 8-bit offsets, 4-bit lengths) ----------

def lz77_estimate(data: bytes) -> int:
    """Greedy LZ77 with 256-byte sliding window and length 3..18 matches.
    Encoded as: literal byte = `0x00 byte`, match = `0x80|length-3` + offset.
    Returns the encoded size — naive but representative. Real impl might
    use bit packing for ~10% more savings."""
    out_len = 0
    i = 0
    n = len(data)
    while i < n:
        best_len = 0
        best_off = 0
        # Scan window for longest match.
        win_start = max(0, i - 256)
        for j in range(win_start, i):
            k = 0
            while k < 18 and i + k < n and data[j + k] == data[i + k]:
                k += 1
            if k >= 3 and k > best_len:
                best_len = k
                best_off = i - j
        if best_len >= 3:
            out_len += 2  # match marker + offset
            i += best_len
        else:
            out_len += 1  # raw literal (no marker; flag bit elsewhere — naive)
            i += 1
    return out_len


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("name", help="image symbol prefix (e.g. TITLE)")
    args = ap.parse_args()

    data = extract_image(args.name)
    n    = len(data)

    # Byte distribution.
    cts        = Counter(data)
    most_byte  = cts.most_common(1)[0]
    zeros      = cts.get(0x00, 0)
    ones       = cts.get(0xFF, 0)
    unique     = len(cts)

    # Run lengths (consecutive identical bytes).
    runs = []
    i = 0
    while i < n:
        b = data[i]
        run = 1
        while i + run < n and data[i + run] == b:
            run += 1
        runs.append(run)
        i += run
    avg_run = sum(runs) / len(runs)
    long_runs = sum(1 for r in runs if r >= 4)

    # Compressors.
    rle_p_size      = len(rle_pairs(data))
    rle_e_size      = len(rle_escape(data))
    lz77_size       = lz77_estimate(data)

    # Decoder cost estimates (compiled AVR bytes, rough).
    DEC_RLE_P  = 50    # tiny: 8-line decode loop
    DEC_RLE_E  = 70    # slightly more state for escape handling
    DEC_LZ77   = 180   # window ring buffer + match-copy loop

    print(f"# {args.name}_data — compressibility check")
    print(f"#   raw size:        {n} B")
    print(f"#   unique bytes:    {unique} / 256")
    print(f"#   most common:     0x{most_byte[0]:02X} × {most_byte[1]} ({100*most_byte[1]//n}%)")
    print(f"#   zeros:           {zeros} ({100*zeros//n}%)")
    print(f"#   0xFFs:           {ones} ({100*ones//n}%)")
    print(f"#   total runs:      {len(runs)} (avg {avg_run:.1f}, {long_runs} runs ≥4 long)")
    print()
    print(f"# Compression schemes (lossless, decoder-cost-adjusted):")
    print(f"#   raw                     {n:>5} B  (baseline)")
    rows = [
        ("byte-pair RLE",  rle_p_size,  DEC_RLE_P),
        ("escape RLE",     rle_e_size,  DEC_RLE_E),
        ("LZ77 (8b/4b)",   lz77_size,   DEC_LZ77),
    ]
    for label, comp, dec in rows:
        net = comp + dec
        delta = n - net
        verdict = "saves" if delta > 0 else "costs"
        print(f"#   {label:<22} {comp:>5} B + {dec:>3} B decoder = {net:>5} B → {verdict} {abs(delta):>4} B")
    print()
    print(f"# Note: decoder is paid ONCE across all compressed images. If 2+")
    print(f"# images use the same compressor, divide its overhead across them.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
