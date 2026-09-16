#!/usr/bin/env python3
"""
Tests for filter_compile_db.py. Pure stdlib unittest, no deps.

Run: python scripts/test_filter_compile_db.py
"""

from __future__ import annotations

import json
import sys
import tempfile
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).parent))
import filter_compile_db  # noqa: E402

ROOT = "/repo"


def entry(path: str, directory: str = "/repo/build") -> dict:
    return {"directory": directory, "file": path, "command": "clang++ -c " + path}


class TestIsOwned(unittest.TestCase):
    def owned(self, path: str, directory: str = "/repo/build") -> bool:
        return filter_compile_db.is_owned(path, directory, ROOT)

    def test_engine_and_game_sources_are_owned(self) -> None:
        self.assertTrue(self.owned("/repo/engines/engine/src/Engine.cpp"))
        self.assertTrue(self.owned("/repo/games/selva-oscura/src/main.cpp"))

    def test_fetched_dependencies_are_not(self) -> None:
        self.assertFalse(self.owned("/repo/build/_deps/sdl2-src/src/SDL.c"))
        self.assertFalse(self.owned("/repo/build/_deps/imgui-src/imgui.cpp"))

    def test_vendored_code_is_not(self) -> None:
        self.assertFalse(self.owned("/repo/vendor/stb/stb_image.c"))

    def test_archived_trees_are_not(self) -> None:
        self.assertFalse(self.owned("/repo/engines/arduboy-legacy/src/core.cpp"))
        self.assertFalse(self.owned("/repo/games/rpg-arduboy/src/game.cpp"))

    def test_paths_outside_the_repo_are_not(self) -> None:
        self.assertFalse(self.owned("/elsewhere/engines/engine/src/Engine.cpp"))

    def test_relative_paths_resolve_against_the_entry_directory(self) -> None:
        self.assertTrue(self.owned("../engines/engine/src/Engine.cpp", "/repo/build"))
        self.assertFalse(self.owned("../vendor/stb/stb_image.c", "/repo/build"))

    def test_a_name_merely_starting_with_an_owned_tree_is_not_owned(self) -> None:
        # "engines-scratch" is not "engines".
        self.assertFalse(self.owned("/repo/engines-scratch/src/a.cpp"))

    def test_separators_are_normalised(self) -> None:
        self.assertTrue(
            filter_compile_db.is_owned(
                r"\repo\engines\engine\src\Engine.cpp".replace("\\", "/"), "/repo/build", ROOT
            )
        )


class TestMain(unittest.TestCase):
    def run_filter(self, entries: list) -> list:
        with tempfile.TemporaryDirectory() as tmp:
            src = Path(tmp) / "compile_commands.json"
            dst = Path(tmp) / "owned.json"
            src.write_text(json.dumps(entries), encoding="utf-8")
            argv = sys.argv
            sys.argv = ["filter_compile_db.py", str(src), "-o", str(dst), "--root", ROOT]
            try:
                code = filter_compile_db.main()
            finally:
                sys.argv = argv
            if code != 0:
                raise AssertionError(f"filter exited {code}")
            return json.loads(dst.read_text(encoding="utf-8"))

    def test_keeps_only_owned_entries(self) -> None:
        kept = self.run_filter(
            [
                entry("/repo/engines/engine/src/Engine.cpp"),
                entry("/repo/build/_deps/sdl2-src/src/SDL.c"),
                entry("/repo/games/point-of-entry/src/main.cpp"),
            ]
        )
        self.assertEqual(
            [e["file"] for e in kept],
            ["/repo/engines/engine/src/Engine.cpp", "/repo/games/point-of-entry/src/main.cpp"],
        )

    def test_entries_are_passed_through_unchanged(self) -> None:
        original = entry("/repo/engines/engine/src/Engine.cpp")
        kept = self.run_filter([original])
        self.assertEqual(kept[0], original)

    def test_a_database_with_nothing_owned_is_an_error(self) -> None:
        # Silently writing an empty database would let analysis "pass" having
        # read nothing, which is the failure this whole script exists to stop.
        with self.assertRaises(AssertionError):
            self.run_filter([entry("/repo/build/_deps/sdl2-src/src/SDL.c")])


if __name__ == "__main__":
    unittest.main(verbosity=2)
