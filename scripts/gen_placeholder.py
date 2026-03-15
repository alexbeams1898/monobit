#!/usr/bin/env python3
"""
Generate assets/sprites.png — a minimal placeholder sprite sheet.

Layout (64x64 RGBA):
  Rows  0-31, cols 0-31: blue  (0, 100, 200) — guard   (32x32, src_x=0, src_y=0)
  Rows 32-59, cols 0-27: green (0, 200, 100) — player  (28x28, src_x=0, src_y=32)

Run from repo root:  python scripts/gen_placeholder.py
Requires Pillow:     pip install Pillow
"""

from PIL import Image

WIDTH, HEIGHT = 64, 64
img = Image.new("RGBA", (WIDTH, HEIGHT), (0, 0, 0, 0))
pixels = img.load()

# Guard — blue 32x32 at (0, 0)
for y in range(32):
    for x in range(32):
        pixels[x, y] = (0, 100, 200, 255)

# Player — green 28x28 at (0, 32)
for y in range(32, 60):
    for x in range(28):
        pixels[x, y] = (0, 200, 100, 255)

img.save("assets/sprites.png")
print("Written: assets/sprites.png")
