"""Tests for the palette stages -- the GBA hardware discipline. Pure functions,
fully deterministic."""

import unittest

import numpy as np

from spritetool.core import SpriteData
from spritetool.stages.palette import (
    MAX_OPAQUE_COLORS,
    palette_check,
    palette_reduce,
    rgb555_snap,
)


def _solid(h, w, rgba):
    px = np.zeros((h, w, 4), dtype=np.uint8)
    px[:, :] = rgba
    return SpriteData(px)


class PaletteTests(unittest.TestCase):
    def test_rgb555_snaps_to_32_levels(self):
        s = _solid(1, 3, (8, 130, 250, 255))
        out = rgb555_snap(s)
        r, g, b, a = out.pixels[0, 0]
        valid = {round(n / 31 * 255) for n in range(32)}
        self.assertIn(int(r), valid)
        self.assertIn(int(g), valid)
        self.assertIn(int(b), valid)
        self.assertEqual(a, 255)

    def test_rgb555_forces_1bit_alpha(self):
        out = rgb555_snap(_solid(1, 2, (100, 100, 100, 120)))  # 120 < 128 -> clear
        self.assertEqual(out.pixels[0, 0, 3], 0)
        out2 = rgb555_snap(_solid(1, 2, (100, 100, 100, 200)))  # 200 -> opaque
        self.assertEqual(out2.pixels[0, 0, 3], 255)

    def test_palette_reduce_caps_opaque_colors_at_15(self):
        px = np.zeros((1, 40, 4), dtype=np.uint8)
        for i in range(40):
            px[0, i] = (i * 6, 255 - i * 6, (i * 13) % 256, 255)
        out = palette_reduce(SpriteData(px), max_colors=16)
        opaque = out.pixels[out.pixels[:, :, 3] == 255][:, :3]
        distinct = np.unique(opaque, axis=0)
        self.assertLessEqual(len(distinct), MAX_OPAQUE_COLORS)
        self.assertLessEqual(len(out.meta["palette"]), MAX_OPAQUE_COLORS)

    def test_palette_reduce_preserves_transparency(self):
        px = np.zeros((1, 4, 4), dtype=np.uint8)
        px[0, 0] = (200, 50, 50, 255)
        px[0, 1] = (0, 0, 0, 0)
        px[0, 2] = (50, 200, 50, 255)
        px[0, 3] = (0, 0, 0, 0)
        out = palette_reduce(SpriteData(px), max_colors=16)
        self.assertEqual(out.pixels[0, 1, 3], 0)
        self.assertEqual(out.pixels[0, 3, 3], 0)

    def test_few_colors_pass_through_unclustered(self):
        px = np.zeros((1, 3, 4), dtype=np.uint8)
        px[0, 0] = (255, 0, 0, 255)
        px[0, 1] = (0, 255, 0, 255)
        px[0, 2] = (0, 0, 255, 255)
        out = palette_reduce(SpriteData(px), max_colors=16)
        self.assertEqual(len(out.meta["palette"]), 3)

    def test_palette_check_preserves_colors_within_budget(self):
        # 3 RGB555-valid colors -> check leaves them exactly (no clustering).
        px = np.zeros((1, 3, 4), dtype=np.uint8)
        px[0, 0] = (255, 0, 0, 255)
        px[0, 1] = (0, 255, 0, 255)
        px[0, 2] = (0, 0, 255, 255)
        before = px.copy()
        out = palette_check(SpriteData(px), max_colors=16)
        # RGB555 snap may adjust values, but 255/0 are already exact grid points.
        np.testing.assert_array_equal(out.pixels, before)
        self.assertEqual(len(out.meta["palette"]), 3)

    def test_palette_check_does_not_reduce_when_over_budget(self):
        # 40 colors -> check does NOT cluster (it only warns); colors survive.
        px = np.zeros((1, 40, 4), dtype=np.uint8)
        for i in range(40):
            px[0, i] = (i * 6, 255 - i * 6, (i * 13) % 256, 255)
        out = palette_check(SpriteData(px), max_colors=16)
        # After only RGB555 snap, still many colors (not clustered to 15).
        self.assertGreater(len(out.meta["palette"]), MAX_OPAQUE_COLORS)


if __name__ == "__main__":
    unittest.main()
