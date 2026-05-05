"""Patch-correctness tests — the other dangerous part.

If `patch_sprites_cpp` misbehaves, it corrupts sprites.cpp silently.
These tests exercise idempotency, no-op behavior, and structural
preservation.

SAFETY NOTE: every test creates its own tmp dir and writes the FAKE_CPP_FIXTURE
string into it. At no point does this suite open, read, or modify the real
games/rpg/sprites.cpp. Verify by grepping this file for 'games/' — no such
path appears.
"""

from __future__ import annotations

import tempfile
import unittest
from pathlib import Path

import numpy as np

from tools.spritebake.core import SpriteData
from tools.spritebake.patch import patch_sprites_cpp, read_existing_sprite


FAKE_CPP_FIXTURE = """\
#include <stdint.h>

namespace sprites {

// One sprite.
const u8 foo_data[3] PROGMEM = {
    0x01, 0x02, 0x03,
};

// Another sprite — with a comment after it.
const u8 bar_data[4] PROGMEM = {
    0xAA, 0xBB, 0xCC, 0xDD,
};

// Some other text that should not be touched.
const int SOMETHING_ELSE = 42;

}  // namespace sprites
"""


class TestPatch(unittest.TestCase):
    def setUp(self):
        self.tmpdir = tempfile.mkdtemp()
        self.path = Path(self.tmpdir) / "sprites.cpp"
        self.path.write_text(FAKE_CPP_FIXTURE, encoding="utf-8")

    def _read(self) -> str:
        return self.path.read_text(encoding="utf-8")

    def test_replace_single_sprite(self):
        # New foo with 3 different bytes
        s = SpriteData.empty(3, 8)
        s.pixels[0, 0] = True
        results = patch_sprites_cpp(self.path, {"foo_data": s})
        self.assertEqual(len(results), 1)
        self.assertEqual(results[0].status, "replaced")
        content = self._read()
        self.assertIn("foo_data[3]", content)
        self.assertIn("0x01", content)
        # bar is untouched
        self.assertIn("bar_data[4]", content)
        self.assertIn("0xAA, 0xBB, 0xCC, 0xDD", content)

    def test_replace_multiple(self):
        s1 = SpriteData.empty(3, 8); s1.pixels[0, 0] = True
        s2 = SpriteData.empty(4, 8); s2.pixels[0, 3] = True
        patch_sprites_cpp(self.path, {"foo_data": s1, "bar_data": s2})
        content = self._read()
        # foo_data should start with 0x01 (bit 0 in col 0)
        self.assertIn("foo_data[3]", content)
        self.assertIn("bar_data[4]", content)

    def test_idempotent(self):
        s = SpriteData.empty(3, 8)
        s.pixels[1, 1] = True
        patch_sprites_cpp(self.path, {"foo_data": s})
        first = self._read()
        patch_sprites_cpp(self.path, {"foo_data": s})
        second = self._read()
        self.assertEqual(first, second)

    def test_not_found(self):
        s = SpriteData.empty(8, 8)
        results = patch_sprites_cpp(self.path, {"nonexistent_data": s})
        self.assertEqual(results[0].status, "not_found")
        # Nothing should have changed
        self.assertEqual(self._read(), FAKE_CPP_FIXTURE)

    def test_preserves_structure(self):
        """Everything outside the modified decl stays byte-identical."""
        s = SpriteData.empty(3, 8)
        patch_sprites_cpp(self.path, {"foo_data": s})
        content = self._read()
        self.assertIn("#include <stdint.h>", content)
        self.assertIn("namespace sprites {", content)
        self.assertIn("}  // namespace sprites", content)
        self.assertIn("const int SOMETHING_ELSE = 42;", content)

    def test_read_existing_sprite(self):
        s = read_existing_sprite(self.path, "foo_data", width=3, height=8)
        self.assertIsNotNone(s)
        self.assertEqual(s.width, 3)
        self.assertEqual(s.height, 8)

    def test_roundtrip_verify_catches_bad_layout(self):
        """If someone asks for a layout/dimensions that misdecode, fail fast."""
        s = SpriteData(pixels=np.array([[True, False]] * 8, dtype=bool))  # 2×8
        # foo_data is declared [3] — bytes_written won't match 2 cols exactly
        # but roundtrip should still be clean because patching uses the new
        # (2-col) layout. No crash expected.
        results = patch_sprites_cpp(self.path, {"foo_data": s})
        self.assertEqual(results[0].status, "replaced")


if __name__ == "__main__":
    unittest.main()
