#!/usr/bin/env python3
"""Generate a 32x32 top-down ladder sprite (PNG) using only stdlib."""

import struct
import zlib
import os

W, H = 32, 32

# Colors (RGBA)
TRANSPARENT = (0, 0, 0, 0)
RAIL_DARK   = (101, 67, 33, 255)   # dark wood
RAIL_MID    = (139, 90, 43, 255)   # medium wood
RAIL_LIGHT  = (160, 110, 60, 255)  # highlight
RUNG_DARK   = (120, 80, 40, 255)
RUNG_MID    = (150, 105, 55, 255)
RUNG_LIGHT  = (170, 125, 70, 255)
SHADOW      = (80, 50, 20, 180)

def make_png(pixels, w, h):
    """Build a minimal valid PNG from raw RGBA pixel data."""
    def chunk(chunk_type, data):
        c = chunk_type + data
        crc = struct.pack('>I', zlib.crc32(c) & 0xFFFFFFFF)
        return struct.pack('>I', len(data)) + c + crc

    # PNG signature
    sig = b'\x89PNG\r\n\x1a\n'
    # IHDR: width, height, bit depth 8, color type 6 (RGBA)
    ihdr = chunk(b'IHDR', struct.pack('>IIBBBBB', w, h, 8, 6, 0, 0, 0))

    # IDAT: raw image data with filter byte 0 (None) per row
    raw = b''
    for y in range(h):
        raw += b'\x00'  # filter: None
        for x in range(w):
            r, g, b, a = pixels[y * w + x]
            raw += struct.pack('BBBB', r, g, b, a)

    idat = chunk(b'IDAT', zlib.compress(raw))
    iend = chunk(b'IEND', b'')
    return sig + ihdr + idat + iend


def generate_ladder():
    pixels = [TRANSPARENT] * (W * H)

    def put(x, y, color):
        if 0 <= x < W and 0 <= y < H:
            pixels[y * W + x] = color

    # Rail positions (two vertical rails)
    left_rail = 9
    right_rail = 22

    # Draw rails (full height with slight taper at ends)
    for y in range(1, 31):
        # Left rail: 3px wide
        put(left_rail - 1, y, RAIL_DARK)
        put(left_rail,     y, RAIL_MID)
        put(left_rail + 1, y, RAIL_LIGHT)
        # Right rail: 3px wide
        put(right_rail - 1, y, RAIL_LIGHT)
        put(right_rail,     y, RAIL_MID)
        put(right_rail + 1, y, RAIL_DARK)

    # Draw rungs (horizontal bars connecting the rails)
    rung_ys = [4, 10, 16, 22, 28]
    for ry in rung_ys:
        for x in range(left_rail + 2, right_rail - 1):
            put(x, ry - 1, RUNG_LIGHT)  # top highlight
            put(x, ry,     RUNG_MID)     # main rung
            put(x, ry + 1, RUNG_DARK)    # bottom shadow
        # Shadow under rung
        for x in range(left_rail + 2, right_rail - 1):
            put(x, ry + 2, SHADOW)

    # Rail caps (top and bottom)
    for rx in [left_rail, right_rail]:
        put(rx - 1, 0,  RAIL_DARK)
        put(rx,     0,  RAIL_MID)
        put(rx + 1, 0,  RAIL_DARK)
        put(rx - 1, 31, RAIL_DARK)
        put(rx,     31, RAIL_MID)
        put(rx + 1, 31, RAIL_DARK)

    return pixels


def main():
    pixels = generate_ladder()
    png_data = make_png(pixels, W, H)

    out_dir = os.path.join(os.path.dirname(__file__), '..', 'assets', 'sprites', 'icons')
    os.makedirs(out_dir, exist_ok=True)
    out_path = os.path.join(out_dir, 'ladder.png')
    with open(out_path, 'wb') as f:
        f.write(png_data)
    print(f"Wrote {len(png_data)} bytes to {out_path}")


if __name__ == '__main__':
    main()
