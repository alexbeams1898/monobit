"""Tests for the tileset scaler -- the pure x2-nearest + RGB555-snap transform.
Deterministic; no I/O. Run: python -m unittest (from tools/tileset/)."""

import os
import sys
import unittest

import numpy as np
from PIL import Image

sys.path.insert(0, os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
from slice import SCALE, rgb555_snap, scale_sheet  # noqa: E402


class TestRgb555Snap(unittest.TestCase):
    def test_snaps_to_32_levels(self):
        # 0 and 255 are exact grid points; a mid value snaps to the nearest level.
        arr = np.array([[[0, 255, 130, 255]]], dtype=np.uint8)
        out = rgb555_snap(arr)
        self.assertEqual(out[0, 0, 0], 0)    # 0 stays 0
        self.assertEqual(out[0, 0, 1], 255)  # 255 stays 255
        # 130/255*31 = 15.8 -> 16 -> 16/31*255 = 131.6 -> 132
        self.assertEqual(out[0, 0, 2], 132)

    def test_alpha_untouched(self):
        arr = np.array([[[10, 20, 30, 123]]], dtype=np.uint8)
        out = rgb555_snap(arr)
        self.assertEqual(out[0, 0, 3], 123)  # alpha passes through, not snapped

    def test_idempotent(self):
        # Snapping an already-snapped value is a no-op (grid points are fixed).
        arr = (np.random.default_rng(0).integers(0, 256, (4, 4, 4))).astype(np.uint8)
        once = rgb555_snap(arr)
        twice = rgb555_snap(once)
        np.testing.assert_array_equal(once, twice)


class TestScaleSheet(unittest.TestCase):
    def test_doubles_dimensions(self):
        src = Image.new("RGBA", (16, 32), (10, 20, 30, 255))
        out = scale_sheet(src)
        self.assertEqual(out.size, (16 * SCALE, 32 * SCALE))

    def test_nearest_neighbor_blocks_no_new_colors(self):
        # A 2x2 of distinct colors, x2'd, must contain ONLY those 4 colors (nearest
        # never blends) -- each source pixel becomes a clean 2x2 block.
        src = Image.new("RGBA", (2, 2))
        src.putpixel((0, 0), (255, 0, 0, 255))
        src.putpixel((1, 0), (0, 255, 0, 255))
        src.putpixel((0, 1), (0, 0, 255, 255))
        src.putpixel((1, 1), (255, 255, 0, 255))
        out = np.array(scale_sheet(src))
        self.assertEqual(out.shape, (4, 4, 4))
        colors = {tuple(px) for row in out for px in row}
        # All output colors are RGB555-snapped versions of the 4 inputs (pure R/G/B
        # 0/255 are grid points, so they're unchanged) -- exactly 4 distinct.
        self.assertEqual(len(colors), 4)
        # Top-left source pixel fills the top-left 2x2 block (nearest, not blended).
        self.assertTrue((out[0, 0] == [255, 0, 0, 255]).all())
        self.assertTrue((out[1, 1] == [255, 0, 0, 255]).all())

    def test_output_is_rgb555_snapped(self):
        # A non-grid color must be snapped in the scaled output.
        src = Image.new("RGBA", (1, 1), (130, 130, 130, 255))
        out = np.array(scale_sheet(src))
        self.assertEqual(out[0, 0, 0], 132)  # same snap as the unit test above


if __name__ == "__main__":
    unittest.main()
