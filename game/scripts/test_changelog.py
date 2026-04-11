#!/usr/bin/env python3
"""
Tests for changelog.py. Pure stdlib unittest, no deps.

Run: python game/scripts/test_changelog.py
"""

from __future__ import annotations

import sys
import tempfile
import textwrap
import unittest
from pathlib import Path

# Make changelog.py importable.
sys.path.insert(0, str(Path(__file__).parent))
import changelog  # noqa: E402


class TestExtractSection(unittest.TestCase):
    def test_basic_extraction(self) -> None:
        body = textwrap.dedent(
            """
            ## Summary
            Some prose.

            ## Changelog
            - Added: New feature.

            ## Test plan
            stuff
            """
        ).strip()
        lines, header_line = changelog.extract_section(body)
        self.assertEqual(lines, ["- Added: New feature."])
        self.assertEqual(header_line, 4)

    def test_missing_section(self) -> None:
        with self.assertRaises(changelog.ChangelogError) as ctx:
            changelog.extract_section("## Summary\nNo changelog here.")
        self.assertIn("missing", str(ctx.exception).lower())

    def test_duplicate_sections(self) -> None:
        body = "## Changelog\n- Added: One.\n## Changelog\n- Added: Two."
        with self.assertRaises(changelog.ChangelogError) as ctx:
            changelog.extract_section(body)
        self.assertIn("more than one", str(ctx.exception))

    def test_section_at_end_of_body(self) -> None:
        body = "## Summary\nfoo\n\n## Changelog\n- Fixed: Bug.\n"
        lines, _ = changelog.extract_section(body)
        self.assertEqual(lines, ["- Fixed: Bug."])

    def test_section_strips_blank_lines(self) -> None:
        body = "## Changelog\n\n\n- Added: Thing.\n\n\n## End"
        lines, _ = changelog.extract_section(body)
        self.assertEqual(lines, ["- Added: Thing."])

    def test_header_must_be_exact(self) -> None:
        # `## Changelog notes` should not match.
        body = "## Changelog notes\n- Added: Foo."
        with self.assertRaises(changelog.ChangelogError):
            changelog.extract_section(body)


class TestParseSection(unittest.TestCase):
    def test_skip(self) -> None:
        result = changelog.parse_section(["skip"], 1)
        self.assertEqual(result, {})

    def test_skip_with_blank_lines(self) -> None:
        result = changelog.parse_section(["", "skip", ""], 1)
        self.assertEqual(result, {})

    def test_single_bullet(self) -> None:
        result = changelog.parse_section(
            ["- Added: Dark mode toggle."], 1
        )
        self.assertEqual(result, {"Added": ["Dark mode toggle."]})

    def test_multiple_categories(self) -> None:
        result = changelog.parse_section(
            [
                "- Added: New weapon class.",
                "- Fixed: Crash on startup.",
                "- Performance: Reduced UIRenderer overhead.",
            ],
            1,
        )
        self.assertEqual(
            result,
            {
                "Added": ["New weapon class."],
                "Fixed": ["Crash on startup."],
                "Performance": ["Reduced UIRenderer overhead."],
            },
        )

    def test_multiple_bullets_same_category(self) -> None:
        result = changelog.parse_section(
            [
                "- Added: Thing one.",
                "- Added: Thing two.",
            ],
            1,
        )
        self.assertEqual(result, {"Added": ["Thing one.", "Thing two."]})

    def test_empty_section(self) -> None:
        with self.assertRaises(changelog.ChangelogError) as ctx:
            changelog.parse_section([], 1)
        self.assertIn("empty", str(ctx.exception).lower())

    def test_skip_mixed_with_bullets(self) -> None:
        with self.assertRaises(changelog.ChangelogError) as ctx:
            changelog.parse_section(
                ["skip", "- Added: Thing."], 1
            )
        self.assertIn("skip", str(ctx.exception).lower())

    def test_invalid_category(self) -> None:
        with self.assertRaises(changelog.ChangelogError) as ctx:
            changelog.parse_section(
                ["- Refactored: Some code."], 1
            )
        self.assertIn("Refactored", str(ctx.exception))

    def test_lowercase_category(self) -> None:
        with self.assertRaises(changelog.ChangelogError):
            changelog.parse_section(["- added: Thing."], 1)

    def test_missing_period(self) -> None:
        with self.assertRaises(changelog.ChangelogError):
            changelog.parse_section(["- Added: Thing"], 1)

    def test_missing_space_after_colon(self) -> None:
        with self.assertRaises(changelog.ChangelogError):
            changelog.parse_section(["- Added:Thing."], 1)

    def test_missing_colon(self) -> None:
        with self.assertRaises(changelog.ChangelogError):
            changelog.parse_section(["- Added Thing."], 1)

    def test_lowercase_description(self) -> None:
        with self.assertRaises(changelog.ChangelogError):
            changelog.parse_section(["- Added: dark mode toggle."], 1)

    def test_asterisk_bullet_rejected(self) -> None:
        with self.assertRaises(changelog.ChangelogError):
            changelog.parse_section(["* Added: Thing."], 1)

    def test_trailing_whitespace_rejected(self) -> None:
        with self.assertRaises(changelog.ChangelogError) as ctx:
            changelog.parse_section(["- Added: Thing. "], 1)
        self.assertIn("trailing whitespace", str(ctx.exception))

    def test_all_seven_categories(self) -> None:
        body = [
            "- Added: A.",
            "- Changed: B.",
            "- Deprecated: C.",
            "- Removed: D.",
            "- Fixed: E.",
            "- Security: F.",
            "- Performance: G.",
        ]
        result = changelog.parse_section(body, 1)
        self.assertEqual(set(result.keys()), set(changelog.ALLOWED_CATEGORIES))


class TestValidatePrBody(unittest.TestCase):
    def test_full_pr_body_valid(self) -> None:
        body = textwrap.dedent(
            """
            ## Summary
            Adds a thing.

            ## Changelog
            - Added: Color palette swatch grid for character customization.
            - Performance: Reduced font atlas texture switches.

            ## Test plan
            - [x] Built locally
            """
        ).strip()
        result = changelog.validate_pr_body(body)
        self.assertEqual(
            result,
            {
                "Added": ["Color palette swatch grid for character customization."],
                "Performance": ["Reduced font atlas texture switches."],
            },
        )

    def test_full_pr_body_skip(self) -> None:
        body = "## Summary\nChore.\n\n## Changelog\nskip\n"
        result = changelog.validate_pr_body(body)
        self.assertEqual(result, {})


class TestChangelogFile(unittest.TestCase):
    """Tests that exercise read/write/render against a temp CHANGELOG.md."""

    def setUp(self) -> None:
        self.tmp = tempfile.TemporaryDirectory()
        self.tmp_path = Path(self.tmp.name) / "CHANGELOG.md"
        self._original = changelog.CHANGELOG_PATH
        changelog.CHANGELOG_PATH = self.tmp_path

    def tearDown(self) -> None:
        changelog.CHANGELOG_PATH = self._original
        self.tmp.cleanup()

    def _write_initial(self, content: str) -> None:
        self.tmp_path.write_text(textwrap.dedent(content).lstrip(), encoding="utf-8")

    def _read(self) -> str:
        return self.tmp_path.read_text(encoding="utf-8")

    def test_prepend_into_existing_unreleased(self) -> None:
        self._write_initial(
            """
            # Changelog

            ## [Unreleased]

            ### Added

            - Existing thing.

            ## [0.1.0] - 2026-04-05

            :seedling: Initial release.

            [unreleased]: https://example.com/compare
            [0.1.0]: https://example.com/v0.1.0
            """
        )
        pr_body = "## Changelog\n- Added: New thing.\n- Fixed: A bug."
        changelog.cmd_prepend(pr_body)
        result = self._read()
        self.assertIn("- Existing thing.", result)
        self.assertIn("- New thing.", result)
        self.assertIn("- A bug.", result)
        self.assertIn("### Added", result)
        self.assertIn("### Fixed", result)
        # Link refs should still be at the bottom.
        self.assertTrue(result.rstrip().endswith("[0.1.0]: https://example.com/v0.1.0"))

    def test_prepend_creates_unreleased_if_missing(self) -> None:
        self._write_initial(
            """
            # Changelog

            ## [0.1.0] - 2026-04-05

            :seedling: Initial release.

            [0.1.0]: https://example.com/v0.1.0
            """
        )
        pr_body = "## Changelog\n- Added: First entry."
        changelog.cmd_prepend(pr_body)
        result = self._read()
        self.assertIn("## [Unreleased]", result)
        # Unreleased must come before 0.1.0
        unreleased_idx = result.index("## [Unreleased]")
        v010_idx = result.index("## [0.1.0]")
        self.assertLess(unreleased_idx, v010_idx)

    def test_prepend_skip_does_not_modify(self) -> None:
        original = textwrap.dedent(
            """
            # Changelog

            ## [Unreleased]

            ## [0.1.0] - 2026-04-05

            :seedling: Initial release.

            [0.1.0]: https://example.com/v0.1.0
            """
        ).lstrip()
        self.tmp_path.write_text(original, encoding="utf-8")
        changelog.cmd_prepend("## Changelog\nskip")
        self.assertEqual(self._read(), original)

    def test_release_promotes_unreleased(self) -> None:
        self._write_initial(
            """
            # Changelog

            ## [Unreleased]

            ### Added

            - Cool feature.

            ### Fixed

            - Bad bug.

            ## [0.1.0] - 2026-04-05

            :seedling: Initial release.

            [0.1.0]: https://example.com/v0.1.0
            """
        )
        changelog.cmd_release("0.2.0")
        result = self._read()
        self.assertIn("## [Unreleased]", result)
        self.assertIn("## [0.2.0] -", result)
        self.assertIn("- Cool feature.", result)
        self.assertIn("- Bad bug.", result)
        # Link ref for 0.2.0 added
        self.assertIn("[0.2.0]: https://github.com/alexbeams1898/prison-escape-game-releases/releases/tag/v0.2.0", result)
        # Unreleased link ref points at 0.2.0...HEAD
        self.assertIn("[unreleased]: https://github.com/alexbeams1898/prison-escape-game-releases/compare/v0.2.0...HEAD", result)
        # New Unreleased section is empty
        unrel_idx = result.index("## [Unreleased]")
        next_idx = result.index("## [0.2.0]")
        between = result[unrel_idx:next_idx]
        # Between Unreleased heading and the next release heading there should
        # be only blank lines (no ### or - bullets).
        for line in between.splitlines()[1:]:  # skip the heading itself
            self.assertFalse(line.strip().startswith("###"))
            self.assertFalse(line.strip().startswith("-"))

    def test_release_empty_unreleased_uses_placeholder(self) -> None:
        self._write_initial(
            """
            # Changelog

            ## [Unreleased]

            ## [0.1.0] - 2026-04-05

            :seedling: Initial release.

            [0.1.0]: https://example.com/v0.1.0
            """
        )
        changelog.cmd_release("0.2.0")
        result = self._read()
        self.assertIn("Internal improvements.", result)

    def test_release_no_unreleased_errors(self) -> None:
        self._write_initial(
            """
            # Changelog

            ## [0.1.0] - 2026-04-05

            :seedling: Initial release.

            [0.1.0]: https://example.com/v0.1.0
            """
        )
        with self.assertRaises(changelog.ChangelogError):
            changelog.cmd_release("0.2.0")

    def test_release_invalid_version(self) -> None:
        with self.assertRaises(changelog.ChangelogError):
            changelog.cmd_release("v1.2.3")
        with self.assertRaises(changelog.ChangelogError):
            changelog.cmd_release("1.2")


if __name__ == "__main__":
    unittest.main(verbosity=2)
