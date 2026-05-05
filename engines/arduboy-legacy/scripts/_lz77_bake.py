#!/usr/bin/env python3
"""LZ77-bake a baked PROGMEM image and verify lossless round-trip.

Reads <name>_data from games/rpg/images.cpp, compresses via the same
byte-aligned LZ77 format the C decoder (engine/lz77.cpp) consumes, runs
a Python copy of the C decoder against the compressed bytes, and asserts
the round-trip is byte-identical to the source.

Underscore-prefixed = throwaway dev script. Outputs the C array to stdout
(--bake) plus a stats summary so you can decide whether the savings
clear the decoder's overhead before committing anything.

    python scripts/_lz77_bake.py FOREST              # measure only
    python scripts/_lz77_bake.py FOREST --bake       # also emit C array

Format (kept in lockstep with engine/lz77.h):
  0xxxxxxx              : N literals follow (1..127)
  10xxxxxx YYYYYYYY     : back-ref len=(xxxxxx+3, 3..66) off=(YYYYYYYY+1, 1..256)
  11000000              : END
"""

from __future__ import annotations

import argparse
import re
import sys
from pathlib import Path

ROOT   = Path(__file__).resolve().parents[1]
IMAGES  = ROOT / "games" / "rpg" / "images.cpp"
SPRITES = ROOT / "games" / "rpg" / "sprites.cpp"


def extract_image(name: str) -> bytes:
    """Find a `const u8 <name>_data[N] PROGMEM = {...}` definition in
    either images.cpp or sprites.cpp and return its bytes. The size N is
    free — full-screen images are 1024 B, boss portraits are 130-280 B.
    """
    pat = re.compile(rf"const u8 {re.escape(name)}_data\[(\d+)\] PROGMEM = \{{(.*?)\}};",
                     re.DOTALL)
    for path in (IMAGES, SPRITES):
        if not path.exists():
            continue
        m = pat.search(path.read_text(encoding="utf-8"))
        if m:
            n   = int(m.group(1))
            raw = bytes(int(x, 16) for x in re.findall(r"0x[0-9A-Fa-f]+", m.group(2)))
            if len(raw) != n:
                raise SystemExit(f"ERROR: {name}_data declared size {n} but parsed {len(raw)} bytes")
            return raw
    raise SystemExit(f"ERROR: {name}_data not found in images.cpp or sprites.cpp")


# --------------------------------------------------------------- encoder --

MAX_LITERAL = 127      # 7-bit literal run length
MIN_MATCH   = 3        # back-ref minimum (less = use literals)
MAX_MATCH   = 66       # 6-bit length field + 3 = 3..66
MAX_OFFSET  = 256      # 8-bit offset field + 1 = 1..256
END         = 0xC0     # 11000000


def find_match(data: bytes, pos: int) -> tuple[int, int]:
    """Find the longest back-reference match within MAX_OFFSET bytes.
    Returns (length, offset). length=0 if no match >= MIN_MATCH."""
    best_len = 0
    best_off = 0
    win_start = max(0, pos - MAX_OFFSET)
    n = len(data)
    for j in range(win_start, pos):
        k = 0
        max_k = min(MAX_MATCH, n - pos)
        while k < max_k and data[j + k] == data[pos + k]:
            k += 1
        if k >= MIN_MATCH and k > best_len:
            best_len = k
            best_off = pos - j
            if k == MAX_MATCH:
                break
    return best_len, best_off


def encode(data: bytes) -> bytes:
    """Greedy LZ77 encoder. At each position, try to find the longest
    back-ref; if shorter than MIN_MATCH, accumulate as a literal."""
    out = bytearray()
    i = 0
    n = len(data)
    pending_literals = bytearray()

    def flush_literals():
        # Flush any pending literals as 1+ literal-run tokens.
        while pending_literals:
            chunk_len = min(len(pending_literals), MAX_LITERAL)
            out.append(chunk_len)  # 0xxxxxxx, top bit = 0
            out.extend(pending_literals[:chunk_len])
            del pending_literals[:chunk_len]

    while i < n:
        match_len, match_off = find_match(data, i)
        if match_len >= MIN_MATCH:
            flush_literals()
            # 10xxxxxx (length-3) + offset-1
            length_field = (match_len - MIN_MATCH) & 0x3F
            offset_field = (match_off - 1) & 0xFF
            out.append(0x80 | length_field)
            out.append(offset_field)
            i += match_len
        else:
            pending_literals.append(data[i])
            i += 1

    flush_literals()
    out.append(END)
    return bytes(out)


# --------------------------------------------------------------- decoder --
# Python mirror of engine/lz77.cpp::decode. Used to verify the round-trip
# matches what the C decoder will produce on-device.

def decode(src: bytes) -> bytes:
    out = bytearray()
    si = 0
    while True:
        token = src[si]; si += 1
        if token == END:
            break
        if (token & 0x80) == 0:
            n = token & 0x7F
            out.extend(src[si:si + n])
            si += n
        else:
            length = (token & 0x3F) + MIN_MATCH
            offset = src[si] + 1; si += 1
            for _ in range(length):
                out.append(out[len(out) - offset])
    return bytes(out)


# ---------------------------------------------------------------- main --

def emit_c_array(name: str, data: bytes, per_line: int = 16) -> str:
    lines = [f"const u8 {name}[{len(data)}] PROGMEM = {{"]
    for i in range(0, len(data), per_line):
        chunk = data[i:i + per_line]
        lines.append("    " + ", ".join(f"0x{b:02X}" for b in chunk) + ",")
    lines.append("};")
    return "\n".join(lines)


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("name", help="image symbol prefix")
    ap.add_argument("--bake", action="store_true",
                    help="emit the C array to stdout after stats")
    args = ap.parse_args()

    raw      = extract_image(args.name)
    encoded  = encode(raw)
    decoded  = decode(encoded)

    # Verify round-trip.
    ok = (decoded == raw)
    diff = sum(1 for a, b in zip(decoded, raw) if a != b) if not ok else 0

    print(f"# {args.name}_data — LZ77 bake")
    print(f"#   raw:        {len(raw):>5} B")
    print(f"#   compressed: {len(encoded):>5} B  ({100 * len(encoded) // len(raw)}% of raw)")
    print(f"#   savings:    {len(raw) - len(encoded):>5} B (before decoder)")
    print(f"#   decoder:      ~80 B (one-time, shared across all LZ77 images)")
    print(f"#   round-trip: {'OK (lossless)' if ok else f'FAILED ({diff} bytes differ)'}")

    if not ok:
        return 1

    if args.bake:
        print()
        print(f"// LZ77-compressed via scripts/_lz77_bake.py. Decode with"
              f" lz77::decode().")
        print(f"// Compressed: {len(encoded)} B vs raw {len(raw)} B"
              f" (saves {len(raw) - len(encoded)} B before ~80 B shared decoder).")
        print(emit_c_array(f"{args.name}_LZ77", encoded))
    return 0


if __name__ == "__main__":
    sys.exit(main())
