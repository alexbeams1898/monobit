#!/usr/bin/env python3
"""
Tests for releaseconfig.py. Pure stdlib unittest, no deps.

Run: python scripts/test_releaseconfig.py
"""

from __future__ import annotations

import sys
import tempfile
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).parent))
import releaseconfig  # noqa: E402


class TestParse(unittest.TestCase):
    def test_flat_scalars(self) -> None:
        d = releaseconfig.parse("cmake_target: selva-oscura\nartifact_prefix: selva\n")
        self.assertEqual(d["cmake_target"], "selva-oscura")
        self.assertEqual(d["artifact_prefix"], "selva")

    def test_quotes_are_stripped(self) -> None:
        self.assertEqual(releaseconfig.parse('a: "x/y"\n')["a"], "x/y")
        self.assertEqual(releaseconfig.parse("a: 'x/y'\n")["a"], "x/y")

    def test_empty_quoted_value_is_a_string_not_a_list(self) -> None:
        # The changelog tool asks this file for a repo NAME and would choke on
        # a list. `public_repo: ""` is how a scope declares no mirror.
        d = releaseconfig.parse('public_repo: ""\n')
        self.assertEqual(d["public_repo"], "")
        self.assertIsInstance(d["public_repo"], str)

    def test_bare_key_with_nothing_under_it_is_empty_string(self) -> None:
        d = releaseconfig.parse("public_repo:\nother: 1\n")
        self.assertEqual(d["public_repo"], "")

    def test_list_under_key(self) -> None:
        d = releaseconfig.parse("include:\n  - config\n  - assets/**/*.png\n")
        self.assertEqual(d["include"], ["config", "assets/**/*.png"])

    def test_list_follows_a_key_that_looked_empty(self) -> None:
        d = releaseconfig.parse("size_limit_mb: 45\ninclude:\n  - config\n")
        self.assertEqual(d["size_limit_mb"], "45")
        self.assertEqual(d["include"], ["config"])

    def test_scalar_after_list_ends_the_list(self) -> None:
        d = releaseconfig.parse("include:\n  - a\npublic_repo: owner/repo\n")
        self.assertEqual(d["include"], ["a"])
        self.assertEqual(d["public_repo"], "owner/repo")

    def test_comments_and_blank_lines_ignored(self) -> None:
        d = releaseconfig.parse("# header\n\na: 1  # trailing\n\n  # indented\nb: 2\n")
        self.assertEqual(d, {"a": "1", "b": "2"})

    def test_hash_inside_quotes_survives(self) -> None:
        self.assertEqual(releaseconfig.parse('a: "x#y"\n')["a"], "x#y")

    def test_list_item_before_any_key_is_an_error(self) -> None:
        with self.assertRaises(releaseconfig.ConfigError):
            releaseconfig.parse("  - orphan\n")

    def test_line_that_is_not_a_key_is_an_error(self) -> None:
        with self.assertRaises(releaseconfig.ConfigError):
            releaseconfig.parse("just some prose\n")


class TestLoad(unittest.TestCase):
    def test_missing_file_raises(self) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            with self.assertRaises(releaseconfig.ConfigError):
                releaseconfig.load(Path(tmp))

    def test_reads_from_directory(self) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            (Path(tmp) / releaseconfig.CONFIG_NAME).write_text(
                "cmake_target: demo\n", encoding="utf-8")
            self.assertEqual(releaseconfig.load(Path(tmp))["cmake_target"], "demo")

    def test_get_missing_key_raises_without_default(self) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            (Path(tmp) / releaseconfig.CONFIG_NAME).write_text("a: 1\n", encoding="utf-8")
            with self.assertRaises(releaseconfig.ConfigError):
                releaseconfig.get(Path(tmp), "nope")

    def test_get_missing_key_returns_default(self) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            (Path(tmp) / releaseconfig.CONFIG_NAME).write_text("a: 1\n", encoding="utf-8")
            self.assertEqual(releaseconfig.get(Path(tmp), "nope", default="fb"), "fb")


class TestRealConfigs(unittest.TestCase):
    """The shipped configs must carry what the packager requires."""

    def test_every_game_declares_a_usable_manifest(self) -> None:
        games = sorted((releaseconfig.REPO_ROOT / "games").iterdir())
        checked = 0
        for game in games:
            if not (game / releaseconfig.CONFIG_NAME).is_file():
                continue
            cfg = releaseconfig.load(game)
            self.assertIn("cmake_target", cfg, game.name)
            self.assertIsInstance(cfg.get("include"), list, game.name)
            self.assertTrue(cfg["include"], f"{game.name} include: is empty")
            self.assertTrue(str(cfg.get("size_limit_mb", "")).strip(),
                            f"{game.name} declares no size_limit_mb")
            checked += 1
        self.assertGreater(checked, 0, "no game configs found")


if __name__ == "__main__":
    unittest.main(verbosity=2)
