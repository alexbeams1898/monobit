"""Pipeline determinism — every stage is pure, a pipeline run on the same
input twice yields the same output.
"""

from __future__ import annotations

import unittest

import numpy as np

from tools.spritebake import stages  # noqa: F401, triggers registration
from tools.spritebake.core import SpriteData
from tools.spritebake.pipeline import Pipeline, STAGES, StageSpec


class TestStagesRegistered(unittest.TestCase):
    def test_all_stages_importable(self):
        """Every stage we documented exists in STAGES after stages/ is imported."""
        expected = {
            "autocrop", "aspect_fit", "pad", "downscale", "stretch", "resize_to_target",
            "gaussian_blur", "sobel_edges", "dilate", "erode",
            "threshold_fixed", "threshold_otsu", "threshold_percentile",
            "threshold_edge_biased", "threshold_distance_weighted",
            "morph_open_speckle", "morph_close_holes", "vert_feature_boost",
            "connected_component_filter", "majority_smooth",
            "floyd_steinberg", "edge_preserving_dither", "bayer_ordered_dither",
            "outline_and_fill", "detect_thin_features", "reinforce_thin_features",
            "detect_bottom_features", "detect_dark_spots", "reinforce_dark_spots",
            "silhouette_preserve",
        }
        missing = expected - STAGES.keys()
        self.assertFalse(missing, f"stages not registered: {missing}")


class TestDeterminism(unittest.TestCase):
    def _synthetic_gray(self, seed: int = 42) -> SpriteData:
        rng = np.random.default_rng(seed)
        gray = rng.integers(0, 256, size=(40, 32), dtype=np.uint8).astype(np.float32)
        return SpriteData(pixels=gray >= 128, meta={"gray": gray})

    def test_pipeline_deterministic(self):
        pipe = Pipeline.from_list([
            {"gaussian_blur": {"radius": 0.8}},
            {"downscale": {"width": 16, "height": 16, "method": "lanczos"}},
            "threshold_otsu",
            "morph_close_holes",
            "vert_feature_boost",
        ])
        src = self._synthetic_gray()
        a = pipe.run(src)
        b = pipe.run(src)
        self.assertTrue((a.pixels == b.pixels).all())
        self.assertEqual(a.shape, (16, 16))


class TestStageContracts(unittest.TestCase):
    def test_downscale_sets_dimensions(self):
        src = SpriteData(pixels=np.ones((100, 80), dtype=bool),
                         meta={"gray": np.ones((100, 80), dtype=np.float32) * 200})
        out = STAGES["downscale"](src, width=20, height=24, method="lanczos")
        self.assertEqual(out.shape, (24, 20))

    def test_threshold_strips_gray_meta(self):
        src = SpriteData(pixels=np.zeros((8, 8), dtype=bool),
                         meta={"gray": np.zeros((8, 8), dtype=np.float32)})
        out = STAGES["threshold_otsu"](src)
        self.assertNotIn("gray", out.meta)

    def test_morph_open_preserves_dims(self):
        src = SpriteData(pixels=np.random.rand(20, 20) > 0.5)
        out = STAGES["morph_open_speckle"](src)
        self.assertEqual(out.shape, (20, 20))

    def test_empty_pipeline_is_identity(self):
        pipe = Pipeline([])
        src = SpriteData(pixels=np.ones((3, 3), dtype=bool))
        out = pipe.run(src)
        self.assertTrue((out.pixels == src.pixels).all())


if __name__ == "__main__":
    unittest.main()