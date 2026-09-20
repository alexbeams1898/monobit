#!/usr/bin/env python3
"""
Tests for package.py. Pure stdlib unittest, no deps.

The manifest tests build a real git repository in a temp directory, because
what ships is bounded by what git tracks and a fixture that fakes that would
not test the thing most likely to break.

Run: python scripts/test_package.py
"""

from __future__ import annotations

import subprocess
import sys
import tempfile
import textwrap
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).parent))
import package  # noqa: E402


def _git(repo: Path, *args: str) -> None:
    subprocess.run(["git", *args], cwd=repo, check=True,
                   capture_output=True)


def _write(path: Path, content: str = "x") -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(content, encoding="utf-8")


class GameFixture:
    """A throwaway game directory inside a real git repo."""

    def __init__(self, tmp: str) -> None:
        self.root = Path(tmp)
        _git(self.root, "init", "-q")
        _git(self.root, "config", "user.email", "t@example.com")
        _git(self.root, "config", "user.name", "t")
        self.game = self.root / "games" / "demo"
        self.game.mkdir(parents=True)

    def add(self, rel: str, content: str = "x", tracked: bool = True) -> Path:
        p = self.game / rel
        _write(p, content)
        if tracked:
            _git(self.root, "add", "-f", str(p.relative_to(self.root)))
        return p

    def commit(self) -> None:
        _git(self.root, "commit", "-q", "-m", "fixture")


class TestManifest(unittest.TestCase):
    def test_directory_pattern_takes_the_whole_tree(self) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            f = GameFixture(tmp)
            f.add("config/a.json")
            f.add("config/nested/b.json")
            f.commit()
            got = package.manifest_files(f.game, ["config"])
            self.assertEqual(sorted(str(p).replace("\\", "/") for p in got),
                             ["config/a.json", "config/nested/b.json"])

    def test_glob_pattern_matches_by_extension(self) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            f = GameFixture(tmp)
            f.add("assets/audio/a.ogg")
            f.add("assets/audio/b.mp3")
            f.commit()
            got = package.manifest_files(f.game, ["assets/audio/*.ogg"])
            self.assertEqual([str(p).replace("\\", "/") for p in got],
                             ["assets/audio/a.ogg"])

    def test_non_recursive_glob_skips_subdirectories(self) -> None:
        # This is what keeps audio/source/ originals out of a release.
        with tempfile.TemporaryDirectory() as tmp:
            f = GameFixture(tmp)
            f.add("assets/audio/keep.ogg")
            f.add("assets/audio/source/original.ogg")
            f.commit()
            got = package.manifest_files(f.game, ["assets/audio/*.ogg"])
            self.assertEqual([str(p).replace("\\", "/") for p in got],
                             ["assets/audio/keep.ogg"])

    def test_untracked_files_never_ship(self) -> None:
        # A release must not depend on what is lying around on the builder's
        # disk. This is the check that makes a local run match CI.
        with tempfile.TemporaryDirectory() as tmp:
            f = GameFixture(tmp)
            f.add("assets/sprites/tracked.png")
            f.add("assets/sprites/untracked.png", tracked=False)
            f.commit()
            got = package.manifest_files(f.game, ["assets/sprites/*.png"])
            self.assertEqual([str(p).replace("\\", "/") for p in got],
                             ["assets/sprites/tracked.png"])

    def test_gitignored_files_never_ship(self) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            f = GameFixture(tmp)
            _write(f.game / ".gitignore", "assets/hair/\n")
            _git(f.root, "add", "-f", "games/demo/.gitignore")
            f.add("assets/hair/huge.png", tracked=False)
            f.add("assets/sprites/ok.png")
            f.commit()
            got = package.manifest_files(f.game, ["assets/**/*.png"])
            self.assertEqual([str(p).replace("\\", "/") for p in got],
                             ["assets/sprites/ok.png"])

    def test_overlapping_patterns_do_not_duplicate(self) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            f = GameFixture(tmp)
            f.add("assets/a.png")
            f.commit()
            got = package.manifest_files(f.game, ["assets/*.png", "assets/**/*.png"])
            self.assertEqual(len(got), 1)


class TestAttribution(unittest.TestCase):
    def test_uncovered_licence_is_reported(self) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            f = GameFixture(tmp)
            f.add("assets/art/LICENSE.txt")
            f.add("assets/art/a.png")
            f.commit()
            covered = package.manifest_files(f.game, ["assets/art/*.png"])
            missing = package.uncovered_attribution(f.game, covered)
            self.assertEqual([str(p).replace("\\", "/") for p in missing],
                             ["assets/art/LICENSE.txt"])

    def test_detection_is_case_insensitive(self) -> None:
        # Upstream packs are inconsistent -- this repo carries both
        # LICENSE-upstream.txt and license.txt. A case-sensitive glob finds one
        # of them on Linux and both on Windows, which is the worst kind of gate.
        with tempfile.TemporaryDirectory() as tmp:
            f = GameFixture(tmp)
            for name in ("license.txt", "LICENSE-upstream.txt",
                         "CREDITS.csv", "VT323-OFL.txt"):
                f.add(f"assets/art/{name}")
            f.commit()
            missing = package.uncovered_attribution(f.game, {})
            self.assertEqual(len(missing), 4)

    def test_covered_licence_is_not_reported(self) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            f = GameFixture(tmp)
            f.add("assets/art/LICENSE.txt")
            f.commit()
            covered = package.manifest_files(f.game, ["assets/**/LICENSE*"])
            self.assertEqual(package.uncovered_attribution(f.game, covered), [])

    def test_non_asset_licence_is_ignored(self) -> None:
        # The repo's own top-level LICENSE is not an asset attribution file.
        with tempfile.TemporaryDirectory() as tmp:
            f = GameFixture(tmp)
            f.add("LICENSE")
            f.add("assets/a.png")
            f.commit()
            covered = package.manifest_files(f.game, ["assets/*.png"])
            self.assertEqual(package.uncovered_attribution(f.game, covered), [])


class TestFindExe(unittest.TestCase):
    def test_finds_nested_executable(self) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            build = Path(tmp) / "build"
            _write(build / "bin" / "demo" / "demo.exe")
            self.assertEqual(package.find_exe(build, "demo.exe").name, "demo.exe")

    def test_missing_executable_raises(self) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            build = Path(tmp) / "build"
            (build / "bin").mkdir(parents=True)
            with self.assertRaises(package.PackageError):
                package.find_exe(build, "demo.exe")


class TestParseLdd(unittest.TestCase):
    SAMPLE = textwrap.dedent("""\
        \tntdll.dll => /c/WINDOWS/SYSTEM32/ntdll.dll (0x7ffe60e00000)
        \tKERNEL32.DLL => /c/WINDOWS/System32/KERNEL32.DLL (0x7ffe60990000)
        \tlibwinpthread-1.dll => /mingw64/bin/libwinpthread-1.dll (0x7ffe25e80000)
        \tSDL2.dll => /mingw64/bin/SDL2.dll (0x7ffe10000000)
        """)

    def test_system_dlls_are_dropped(self) -> None:
        got = package.parse_ldd(self.SAMPLE)
        self.assertEqual(got, ["/mingw64/bin/SDL2.dll",
                               "/mingw64/bin/libwinpthread-1.dll"])

    def test_backslash_system_paths_are_dropped(self) -> None:
        line = "\tFOO.dll => C:\\Windows\\System32\\FOO.dll (0x1)\n"
        self.assertEqual(package.parse_ldd(line), [])

    def test_lines_without_an_arrow_are_ignored(self) -> None:
        self.assertEqual(package.parse_ldd("\tsome.dll (0x1)\n"), [])

    def test_duplicates_collapse(self) -> None:
        got = package.parse_ldd(self.SAMPLE + self.SAMPLE)
        self.assertEqual(len(got), 2)


class TestVerifyBuildConfig(unittest.TestCase):
    """A build configured the wrong way must not be packaged.

    wayworn-hush compiles an absolute chdir to the build machine's source tree
    unless WAYWORN_RELEASE is on. That exe works everywhere except on somebody
    else's computer, which is the only place a release goes.
    """

    def _cache(self, tmp: str, body: str) -> Path:
        build = Path(tmp) / "build"
        build.mkdir(parents=True)
        _write(build / "CMakeCache.txt", body)
        return build

    def test_no_requirements_is_a_no_op(self) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            package.verify_build_config(Path(tmp) / "nonexistent", [])

    def test_matching_value_passes(self) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            build = self._cache(tmp, "WAYWORN_RELEASE:BOOL=ON\n")
            package.verify_build_config(build, ["WAYWORN_RELEASE=ON"])

    def test_wrong_value_raises(self) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            build = self._cache(tmp, "WAYWORN_RELEASE:BOOL=OFF\n")
            with self.assertRaises(package.PackageError) as ctx:
                package.verify_build_config(build, ["WAYWORN_RELEASE=ON"])
            self.assertIn("WAYWORN_RELEASE", str(ctx.exception))

    def test_absent_setting_raises(self) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            build = self._cache(tmp, "CMAKE_BUILD_TYPE:STRING=Release\n")
            with self.assertRaises(package.PackageError):
                package.verify_build_config(build, ["WAYWORN_RELEASE=ON"])

    def test_missing_cache_raises(self) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            with self.assertRaises(package.PackageError):
                package.verify_build_config(Path(tmp), ["WAYWORN_RELEASE=ON"])

    def test_comments_and_blanks_are_skipped(self) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            build = self._cache(tmp, "# comment\n//doc line\n\nA:BOOL=ON\n")
            self.assertEqual(package.cache_values(build), {"A": "ON"})


class TestReadVersion(unittest.TestCase):
    def test_reads_project_version(self) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            d = Path(tmp)
            _write(d / "CMakeLists.txt", "project(demo VERSION 1.2.3 LANGUAGES CXX)\n")
            self.assertEqual(package.read_version(d), "1.2.3")

    def test_version_may_span_lines(self) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            d = Path(tmp)
            _write(d / "CMakeLists.txt", "project(demo\n    VERSION 0.4.0\n)\n")
            self.assertEqual(package.read_version(d), "0.4.0")

    def test_missing_version_raises(self) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            d = Path(tmp)
            _write(d / "CMakeLists.txt", "project(demo)\n")
            with self.assertRaises(package.PackageError):
                package.read_version(d)


if __name__ == "__main__":
    unittest.main(verbosity=2)
