#!/usr/bin/env python3
"""
Tests for check_assets.py. Pure stdlib unittest, no deps.

Run: python scripts/test_check_assets.py
"""

from __future__ import annotations

import sys
import tempfile
import textwrap
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).parent))
import check_assets  # noqa: E402
import releaseconfig  # noqa: E402
from test_package import GameFixture  # noqa: E402

MANIFEST = textwrap.dedent("""\
    cmake_target: demo
    artifact_prefix: demo
    public_repo: ""
    size_limit_mb: 10
    include:
      - config
      - assets/audio/*.ogg
    """)


def _fixture(tmp: str, manifest: str = MANIFEST) -> GameFixture:
    f = GameFixture(tmp)
    f.add(releaseconfig.CONFIG_NAME, manifest)
    return f


class TestDeclaredAssets(unittest.TestCase):
    def test_finds_paths_in_config_json(self) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            f = _fixture(tmp)
            f.add("config/audio.json", '{"path": "assets/audio/a.ogg"}')
            f.commit()
            found = check_assets.declared_assets(f.game)
            self.assertIn("assets/audio/a.ogg", found)

    def test_finds_paths_nested_at_any_depth(self) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            f = _fixture(tmp)
            f.add("config/x.json",
                  '{"a": {"b": [{"path": "assets/audio/deep.ogg"}]}}')
            f.commit()
            self.assertIn("assets/audio/deep.ogg", check_assets.declared_assets(f.game))

    def test_finds_literals_in_cpp(self) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            f = _fixture(tmp)
            f.add("src/Main.cpp", 'load("assets/audio/fromcode.ogg");\n')
            f.commit()
            self.assertIn("assets/audio/fromcode.ogg",
                          check_assets.declared_assets(f.game))

    def test_directory_prefixes_are_not_treated_as_files(self) -> None:
        # Code builds paths like "assets/characters/" + id at runtime. A
        # prefix names no file and must not be reported as missing.
        with tempfile.TemporaryDirectory() as tmp:
            f = _fixture(tmp)
            f.add("src/Main.cpp", 'const std::string d = "assets/characters/";\n')
            f.commit()
            self.assertEqual(check_assets.declared_assets(f.game), {})

    def test_format_strings_are_not_treated_as_paths(self) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            f = _fixture(tmp)
            f.add("src/Main.cpp", 'printf("see assets/regions/SCHEMA.md.\\n");\n')
            f.commit()
            self.assertEqual(check_assets.declared_assets(f.game), {})

    def test_malformed_json_does_not_crash(self) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            f = _fixture(tmp)
            f.add("config/broken.json", "{not json")
            f.commit()
            self.assertEqual(check_assets.declared_assets(f.game), {})


class TestCheckGame(unittest.TestCase):
    def setUp(self) -> None:
        self._orig = check_assets.REPO_ROOT

    def tearDown(self) -> None:
        check_assets.REPO_ROOT = self._orig

    def _check(self, f: GameFixture) -> list[str]:
        check_assets.REPO_ROOT = f.root
        return check_assets.check_game(f.game)

    def test_clean_game_has_no_problems(self) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            f = _fixture(tmp)
            f.add("assets/audio/a.ogg")
            f.add("config/audio.json", '{"path": "assets/audio/a.ogg"}')
            f.commit()
            self.assertEqual(self._check(f), [])

    def test_declared_but_absent_file_is_reported(self) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            f = _fixture(tmp)
            f.add("config/audio.json", '{"path": "assets/audio/gone.ogg"}')
            f.commit()
            problems = self._check(f)
            self.assertEqual(len(problems), 1)
            self.assertIn("does not exist", problems[0])
            self.assertIn("config/audio.json", problems[0].replace("\\", "/"))

    def test_existing_file_outside_the_manifest_is_reported(self) -> None:
        # The failure mode inclusion-only packaging trades for: the asset is
        # there, the game loads it, and the zip does not contain it.
        with tempfile.TemporaryDirectory() as tmp:
            f = _fixture(tmp)
            f.add("assets/music/theme.mp3")
            f.add("config/audio.json", '{"path": "assets/music/theme.mp3"}')
            f.commit()
            problems = self._check(f)
            self.assertEqual(len(problems), 1)
            self.assertIn("would not ship", problems[0])

    def test_uncovered_attribution_is_reported(self) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            f = _fixture(tmp)
            f.add("assets/audio/LICENSE.txt")
            f.commit()
            problems = self._check(f)
            self.assertEqual(len(problems), 1)
            self.assertIn("attribution", problems[0])

    def test_missing_include_list_is_reported(self) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            f = _fixture(tmp, "cmake_target: demo\n")
            f.commit()
            problems = self._check(f)
            self.assertEqual(len(problems), 1)
            self.assertIn("include", problems[0])


class TestRealRepo(unittest.TestCase):
    def test_every_game_passes_its_own_gate(self) -> None:
        """The shipped tree must satisfy the gate this script enforces."""
        problems: list[str] = []
        for game in check_assets.games(check_assets.REPO_ROOT):
            problems.extend(check_assets.check_game(game))
        self.assertEqual(problems, [], "\n".join(problems))


if __name__ == "__main__":
    unittest.main(verbosity=2)
