"""Encode/decode roundtrip tests — the dangerous part of the tool.

If these fail, every sprite in every game built with this tool is wrong.
Uses unittest (stdlib-only) rather than pytest to avoid a dep.
"""

from __future__ import annotations

import unittest

import numpy as np

from tools.spritebake.core import SpriteData
from tools.spritebake.encode import encode, decode, format_c_array, parse_byte_array, LAYOUTS


class TestRoundtrip(unittest.TestCase):
    """For every registered layout, encode(decode(x)) == x and decode(encode(x)) == x."""

    def _random_sprite(self, w: int, h: int, seed: int = 0) -> SpriteData:
        rng = np.random.default_rng(seed)
        return SpriteData(pixels=rng.random((h, w)) > 0.5)

    def test_col_major_topbit0_roundtrip(self):
        for w, h in [(8, 8), (20, 24), (16, 12), (1, 1), (32, 7)]:
            with self.subTest(size=(w, h)):
                s = self._random_sprite(w, h)
                bytes_ = encode(s, "col-major-topbit0")
                s2 = decode(bytes_, w, h, "col-major-topbit0")
                self.assertTrue((s.pixels == s2.pixels).all())

    def test_all_layouts_roundtrip(self):
        for layout in LAYOUTS:
            with self.subTest(layout=layout):
                s = self._random_sprite(16, 12, seed=hash(layout) & 0xFFFF)
                bytes_ = encode(s, layout)
                s2 = decode(bytes_, 16, 12, layout)
                self.assertTrue((s.pixels == s2.pixels).all())

    def test_empty_sprite(self):
        s = SpriteData(pixels=np.zeros((8, 8), dtype=bool))
        bytes_ = encode(s)
        self.assertEqual(bytes_, [0] * 8)
        s2 = decode(bytes_, 8, 8)
        self.assertTrue((s.pixels == s2.pixels).all())

    def test_full_sprite(self):
        s = SpriteData(pixels=np.ones((8, 8), dtype=bool))
        bytes_ = encode(s)
        self.assertEqual(bytes_, [0xFF] * 8)

    def test_known_drop_sprite(self):
        """The SANGUE_DROP teardrop should encode to exactly these bytes
        (matches the value in games/rpg/sprites.cpp)."""
        pixels = np.zeros((8, 8), dtype=bool)
        # Drawn to match the teardrop we baked:
        # r2: c3
        # r3: c2..3
        # r4: c2..4
        # r5: c3..4
        pixels[2, 3] = True
        pixels[3, 2] = pixels[3, 3] = True
        pixels[4, 2] = pixels[4, 3] = pixels[4, 4] = True
        pixels[5, 3] = pixels[5, 4] = True
        s = SpriteData(pixels=pixels)
        bytes_ = encode(s)
        self.assertEqual(bytes_, [0x00, 0x00, 0x18, 0x3C, 0x30, 0x00, 0x00, 0x00])


class TestFormatting(unittest.TestCase):
    def test_format_c_array_default(self):
        pixels = np.zeros((8, 4), dtype=bool)
        pixels[0, 0] = True
        s = SpriteData(pixels=pixels)
        out = format_c_array("foo_data", s)
        self.assertIn("const u8 foo_data[4] PROGMEM = {", out)
        self.assertIn("0x01", out)
        self.assertTrue(out.endswith("};"))

    def test_format_c_array_no_progmem(self):
        s = SpriteData.empty(2, 8)
        out = format_c_array("foo_data", s, progmem=False)
        self.assertIn("const u8 foo_data[2] = {", out)
        self.assertNotIn("PROGMEM", out)


class TestParse(unittest.TestCase):
    def test_parse_mixed(self):
        text = "0x00, 0x0A, 0b1010, 42, // comment\n0xFF"
        self.assertEqual(parse_byte_array(text), [0, 10, 10, 42, 0xFF])

    def test_parse_with_braces(self):
        text = "const u8 x[3] PROGMEM = { 0x12, 0x34, 0x56 };"
        self.assertEqual(parse_byte_array(text), [0x12, 0x34, 0x56])

    def test_parse_empty(self):
        self.assertEqual(parse_byte_array(""), [])

    def test_parse_clamps_over_255(self):
        # Decimal 300 should be filtered (out of byte range)
        self.assertEqual(parse_byte_array("0x10, 300, 0x20"), [0x10, 0x20])


if __name__ == "__main__":
    unittest.main()
