#!/usr/bin/env python3
"""Generate assets/sprites/ember.png -- 8x8 radial gradient ember particle.

Bright amber center fading to deep crimson edges with soft alpha falloff.
"""

import math
import os

from PIL import Image

SIZE = 8
img = Image.new("RGBA", (SIZE, SIZE), (0, 0, 0, 0))
pixels = img.load()
cx, cy = SIZE / 2.0 - 0.5, SIZE / 2.0 - 0.5
max_dist = math.sqrt(cx * cx + cy * cy)

for y in range(SIZE):
    for x in range(SIZE):
        dist = math.sqrt((x - cx) ** 2 + (y - cy) ** 2)
        t = min(dist / max_dist, 1.0)
        r = int(255 - t * 75)
        g = int(160 - t * 130)
        b = int(40 - t * 35)
        a = int(255 * (1.0 - t * t))
        pixels[x, y] = (r, g, b, a)

script_dir = os.path.dirname(os.path.abspath(__file__))
out_path = os.path.join(script_dir, "..", "assets", "sprites", "ember.png")
os.makedirs(os.path.dirname(out_path), exist_ok=True)
img.save(out_path)
print(f"Written: {out_path}")
