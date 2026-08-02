"""Tests for sheet assembly + manifest -- verifies the engine layout
(column = dir*max_frames + frame, rows = states)."""

import unittest

import numpy as np

from spritetool.assemble import StateSpec, animation_manifest, assemble_sheet
from spritetool.core import SpriteData

DIRS = ["south", "west", "east", "north"]


def _cell(fw, fh, tag):
    px = np.zeros((fh, fw, 4), dtype=np.uint8)
    px[:, :] = (tag, tag, tag, 255)
    return SpriteData(px)


class AssembleTests(unittest.TestCase):
    def test_assemble_places_frames_at_engine_columns(self):
        fw, fh = 8, 16
        states = [
            StateSpec("walk", frames=2, duration=0.1, row=0),
            StateSpec("idle", frames=1, duration=0.0, row=1),
        ]
        frames = {
            "walk": {d: [_cell(fw, fh, 10 + i) for i in range(2)] for d in DIRS},
            "idle": {d: [_cell(fw, fh, 99)] for d in DIRS},
        }
        sheet = assemble_sheet(frames, fw, fh, 4, states)

        max_frames = 2
        self.assertEqual(sheet.shape, (2 * fh, 4 * max_frames * fw, 4))

        # East (dir 2) walk frame 1 -> column 2*2 + 1 = 5, row 0.
        col = 2 * max_frames + 1
        self.assertEqual(sheet[0, col * fw, 0], 11)

        # North (dir 3) idle frame 0 -> column 3*2 + 0 = 6, row 1.
        col = 3 * max_frames + 0
        self.assertEqual(sheet[fh, col * fw, 0], 99)

    def test_manifest_shape_matches_engine_loader(self):
        states = [
            StateSpec("idle", frames=1, duration=0.0, row=1),
            StateSpec("walk", frames=4, duration=0.14, row=0),
        ]
        m = animation_manifest("assets/sprites/player.png", 32, 64, 4, states)
        self.assertEqual(m["frame_width"], 32)
        self.assertEqual(m["frame_height"], 64)
        self.assertEqual(m["direction_count"], 4)
        self.assertEqual(m["max_frames_per_state"], 4)
        self.assertEqual(m["states"]["walk"], {"row": 0, "frames": 4, "duration": 0.14})
        self.assertEqual(m["states"]["idle"]["duration"], 0.0)


if __name__ == "__main__":
    unittest.main()
