#!/usr/bin/env python3
"""
changelog.py -- changelog validator, PR-body extractor, and CHANGELOG.md updater.

Single source of truth for the changelog pipeline. Pure stdlib, no deps.

Subcommands:
  validate-pr-body <file>
      Read a PR body from <file> (or - for stdin), validate that it has a
      well-formed `## Changelog` section. Exit 0 on success, 1 on failure.
      Prints precise error messages with GitHub Actions ::error annotations.

  extract-pr-body <file>
      Read a PR body, extract the bullets from the `## Changelog` section,
      print them to stdout grouped by category. Used by the release workflow
      after validate-pr-body has passed.

  prepend <pr-body-file>
      Validate the PR body, extract bullets, prepend them to the [Unreleased]
      section of this game's CHANGELOG.md. Idempotent on identical input.

  release <version>
      Rename the [Unreleased] section to [<version>] - YYYY-MM-DD, insert a
      fresh empty [Unreleased] above it, and update link references at the
      bottom of the file. Exits with the new section's content on stdout
      so the release workflow can pipe it to `gh release create --notes-file -`.

  notes <version>
      Print just the body (no heading) of the [<version>] section. Used by
      the release workflow to fetch notes for an existing version.

Format spec:
  - PR body must contain exactly one `## Changelog` section.
  - The section is either:
      (a) The single line `skip` (case-sensitive), meaning the PR has no
          user-facing impact (chore/docs/infra), OR
      (b) One or more bullets, each matching:
              - <Category>: <Description ending with period>
          where:
              <Category> is one of: Added, Changed, Deprecated, Removed,
                                    Fixed, Security, Performance
              <Description> starts with an uppercase letter, contains at
                            least one non-whitespace character, ends with `.`
  - No mixing of `skip` with bullets.
  - Bullets only -- no sub-bullets, no inline headings.

CHANGELOG.md format (Keep a Changelog flavor with [Unreleased]):
  # Changelog

  All notable changes to this project will be documented in this file.

  The format is based on [Keep a Changelog]...

  ## [Unreleased]

  ### Added

  - Bullet text.

  ## [0.3.0] - 2026-04-11

  ### Added

  - LPC paper-doll sprite pipeline + character customization.

  [unreleased]: https://github.com/.../compare/v0.3.0...HEAD
  [0.3.0]: https://github.com/.../releases/tag/v0.3.0
"""

from __future__ import annotations

import argparse
import datetime as _dt
import re
import sys
from pathlib import Path

ALLOWED_CATEGORIES = (
    "Added",
    "Changed",
    "Deprecated",
    "Removed",
    "Fixed",
    "Security",
    "Performance",
)

# Order categories appear in CHANGELOG.md sections.
CATEGORY_ORDER = {name: i for i, name in enumerate(ALLOWED_CATEGORIES)}

# One script for every scope. Which CHANGELOG.md it edits comes from --scope-dir,
# and the distribution repo it links to comes from that scope's
# .release-config.yml -- the file that already declares it for the release
# workflow. A copy of this script per game drifts; a copy of that repo name in
# the script drifts from the config beside it.
SCRIPT_DIR = Path(__file__).resolve().parent
REPO_ROOT = SCRIPT_DIR.parent

_scope_dir = REPO_ROOT
CHANGELOG_PATH = _scope_dir / "CHANGELOG.md"


def set_scope(scope_dir: str | None) -> None:
    """Point the tool at one scope. Called once from main()."""
    global _scope_dir, CHANGELOG_PATH
    _scope_dir = (REPO_ROOT / scope_dir).resolve() if scope_dir else REPO_ROOT
    CHANGELOG_PATH = _scope_dir / "CHANGELOG.md"


def public_releases_repo() -> str:
    """The scope's distribution repo, read from its .release-config.yml.

    Flat `key: value` lines, so a regex reads it without a YAML dependency --
    these scripts are deliberately stdlib-only. Empty when the scope declares
    no public repo, which is how link refs are omitted for one.
    """
    cfg = _scope_dir / ".release-config.yml"
    if not cfg.is_file():
        return ""
    m = re.search(r"^public_repo:\s*(\S+)\s*$", cfg.read_text(encoding="utf-8"), re.M)
    return m.group(1) if m else ""


# ---------------------------------------------------------------------------
# Errors
# ---------------------------------------------------------------------------


class ChangelogError(Exception):
    """Validation error with optional line number for GitHub annotations."""

    def __init__(self, message: str, line: int | None = None) -> None:
        super().__init__(message)
        self.line = line


def _emit_error(err: ChangelogError, source: str) -> None:
    """Print a GitHub Actions ::error annotation + human-readable line."""
    if err.line is not None:
        print(
            f"::error file={source},line={err.line}::{err}",
            file=sys.stderr,
        )
    else:
        print(f"::error file={source}::{err}", file=sys.stderr)
    print(f"ERROR: {err}", file=sys.stderr)


# ---------------------------------------------------------------------------
# PR body parsing
# ---------------------------------------------------------------------------


# Match `## Changelog` (exact, case-sensitive, no trailing text on the line).
_SECTION_HEADER = re.compile(r"^##\s+Changelog\s*$")
_NEXT_SECTION = re.compile(r"^##\s+\S")

# Match a bullet: "- Category: Description."
# Description must start with uppercase, contain at least one non-space char,
# end with a period. No trailing whitespace.
_BULLET = re.compile(
    r"^- (?P<category>[A-Z][a-zA-Z]+): (?P<desc>[A-Z].*\.)$"
)


def extract_section(pr_body: str) -> tuple[list[str], int]:
    """
    Find the `## Changelog` section in pr_body. Return (lines, header_line_number).
    Lines exclude the header itself but include all content up to the next `##`
    heading (or end of body). Trailing blank lines are stripped.
    """
    lines = pr_body.splitlines()
    header_idx = None
    for i, line in enumerate(lines):
        if _SECTION_HEADER.match(line):
            header_idx = i
            break
    if header_idx is None:
        raise ChangelogError(
            "PR body is missing the required `## Changelog` section. "
            "See .github/pull_request_template.md for the format."
        )

    # Reject duplicate sections.
    for j in range(header_idx + 1, len(lines)):
        if _SECTION_HEADER.match(lines[j]):
            raise ChangelogError(
                "PR body contains more than one `## Changelog` section.",
                line=j + 1,
            )

    # Collect content until next ## heading or EOF.
    body: list[str] = []
    for j in range(header_idx + 1, len(lines)):
        if _NEXT_SECTION.match(lines[j]):
            break
        body.append(lines[j])

    # Strip leading + trailing blank lines.
    while body and not body[0].strip():
        body.pop(0)
    while body and not body[-1].strip():
        body.pop()
    return body, header_idx + 1


def parse_section(body_lines: list[str], header_line: int) -> dict[str, list[str]]:
    """
    Parse the body of a `## Changelog` section into a category -> [descriptions]
    dict, OR return {} if the section says `skip`.
    Raises ChangelogError on any format violation.
    """
    if not body_lines:
        raise ChangelogError(
            "`## Changelog` section is empty. Either add bullets or write `skip`.",
            line=header_line,
        )

    # Skip mode: section is exactly `skip` and nothing else.
    non_blank = [ln for ln in body_lines if ln.strip()]
    if len(non_blank) == 1 and non_blank[0] == "skip":
        return {}

    if any(ln.strip() == "skip" for ln in non_blank):
        raise ChangelogError(
            "`skip` cannot be mixed with bullets. Either delete the bullets "
            "or delete the `skip` line.",
            line=header_line,
        )

    # Bullets mode: every non-blank line must be a valid bullet.
    grouped: dict[str, list[str]] = {}
    for offset, line in enumerate(body_lines):
        absolute = header_line + 1 + offset
        if not line.strip():
            continue
        if line != line.rstrip():
            raise ChangelogError(
                f"Bullet has trailing whitespace: {line!r}",
                line=absolute,
            )
        if not line.startswith("- "):
            raise ChangelogError(
                f"Bullets must start with `- ` (dash + space). Got: {line!r}",
                line=absolute,
            )
        m = _BULLET.match(line)
        if not m:
            raise ChangelogError(
                f"Malformed bullet: {line!r}\n"
                f"  Expected: - <Category>: <Description ending with period>\n"
                f"  Categories: {', '.join(ALLOWED_CATEGORIES)}\n"
                f"  Description must start with an uppercase letter and end with a period.",
                line=absolute,
            )
        category = m.group("category")
        if category not in CATEGORY_ORDER:
            raise ChangelogError(
                f"Unknown category {category!r}. Allowed: {', '.join(ALLOWED_CATEGORIES)}",
                line=absolute,
            )
        desc = m.group("desc")
        if not desc.strip("."):
            raise ChangelogError(
                f"Empty description after period: {line!r}",
                line=absolute,
            )
        grouped.setdefault(category, []).append(desc)

    if not grouped:
        raise ChangelogError(
            "`## Changelog` section has no valid bullets and is not `skip`.",
            line=header_line,
        )
    return grouped


def validate_pr_body(pr_body: str) -> dict[str, list[str]]:
    """
    Run extract + parse. Returns the grouped bullets dict (empty for skip).
    Raises ChangelogError on any failure.
    """
    body_lines, header_line = extract_section(pr_body)
    return parse_section(body_lines, header_line)


# ---------------------------------------------------------------------------
# CHANGELOG.md manipulation
# ---------------------------------------------------------------------------


def _read_changelog() -> str:
    if not CHANGELOG_PATH.exists():
        raise ChangelogError(f"CHANGELOG.md not found at {CHANGELOG_PATH}")
    return CHANGELOG_PATH.read_text(encoding="utf-8")


def _write_changelog(content: str) -> None:
    CHANGELOG_PATH.write_text(content, encoding="utf-8")


def _split_into_releases(content: str) -> tuple[str, list[tuple[str, list[str]]], list[str]]:
    """
    Split CHANGELOG.md content into:
        (preamble, releases, link_refs)
    where:
        preamble    = everything before the first `## [` heading (verbatim,
                      including its trailing newline)
        releases    = list of (heading_line, body_lines) tuples in file order
        link_refs   = list of lines that are link reference definitions
                      `[name]: url`, taken from the bottom of the file
    """
    lines = content.split("\n")

    # Identify link refs at the bottom.
    link_re = re.compile(r"^\[[^\]]+\]:\s+\S+")
    link_refs: list[str] = []
    body_end = len(lines)
    for i in range(len(lines) - 1, -1, -1):
        s = lines[i].strip()
        if not s:
            continue
        if link_re.match(s):
            link_refs.insert(0, lines[i])
            body_end = i
        else:
            break

    body_lines = lines[:body_end]

    # Find first release heading.
    release_header_re = re.compile(r"^##\s+\[")
    first = None
    for i, ln in enumerate(body_lines):
        if release_header_re.match(ln):
            first = i
            break
    if first is None:
        return "\n".join(body_lines), [], link_refs

    preamble = "\n".join(body_lines[:first])

    # Group remaining lines by release heading.
    releases: list[tuple[str, list[str]]] = []
    current: list[str] | None = None
    current_heading: str | None = None
    for ln in body_lines[first:]:
        if release_header_re.match(ln):
            if current_heading is not None:
                releases.append((current_heading, _trim_blanks(current or [])))
            current_heading = ln
            current = []
        else:
            assert current is not None
            current.append(ln)
    if current_heading is not None:
        releases.append((current_heading, _trim_blanks(current or [])))

    return preamble, releases, link_refs


def _trim_blanks(lines: list[str]) -> list[str]:
    while lines and not lines[0].strip():
        lines.pop(0)
    while lines and not lines[-1].strip():
        lines.pop()
    return lines


def _render(preamble: str, releases: list[tuple[str, list[str]]], link_refs: list[str]) -> str:
    parts: list[str] = []
    if preamble.strip():
        parts.append(preamble.rstrip("\n"))
        parts.append("")
    for heading, body in releases:
        parts.append(heading)
        if body:
            parts.append("")
            parts.extend(body)
        parts.append("")
    if link_refs:
        parts.extend(link_refs)
    text = "\n".join(parts).rstrip("\n") + "\n"
    return text


def _parse_release_body(body_lines: list[str]) -> dict[str, list[str]]:
    """
    Parse a release body (the lines under a `## [version] - date` heading) into
    a category -> [descriptions] dict. Each category is `### Category` followed
    by `- Description.` bullets.
    """
    grouped: dict[str, list[str]] = {}
    current_cat: str | None = None
    for ln in body_lines:
        s = ln.strip()
        if not s:
            continue
        if s.startswith("### "):
            current_cat = s[4:].strip()
            grouped.setdefault(current_cat, [])
            continue
        if s.startswith("- ") and current_cat is not None:
            grouped[current_cat].append(s[2:])
    return grouped


def _render_release_body(grouped: dict[str, list[str]]) -> list[str]:
    """Render a category-grouped dict into the body lines for a release."""
    lines: list[str] = []
    sorted_cats = sorted(grouped.keys(), key=lambda c: CATEGORY_ORDER.get(c, 99))
    for i, cat in enumerate(sorted_cats):
        bullets = grouped[cat]
        if not bullets:
            continue
        if i > 0:
            lines.append("")
        lines.append(f"### {cat}")
        lines.append("")
        for b in bullets:
            lines.append(f"- {b}")
    return lines


def _find_unreleased(releases: list[tuple[str, list[str]]]) -> int:
    for i, (heading, _) in enumerate(releases):
        if heading.strip().lower().startswith("## [unreleased]"):
            return i
    return -1


def cmd_prepend(pr_body: str) -> None:
    """Validate PR body and merge its bullets into [Unreleased]."""
    new_bullets = validate_pr_body(pr_body)
    if not new_bullets:
        # skip: do not touch CHANGELOG.md.
        print("PR body says `skip` -- no changelog update.")
        return

    content = _read_changelog()
    preamble, releases, link_refs = _split_into_releases(content)

    idx = _find_unreleased(releases)
    if idx == -1:
        # Insert [Unreleased] at the top.
        releases.insert(0, ("## [Unreleased]", []))
        idx = 0

    heading, body = releases[idx]
    existing = _parse_release_body(body)
    for cat, descs in new_bullets.items():
        existing.setdefault(cat, []).extend(descs)
    releases[idx] = (heading, _render_release_body(existing))

    _write_changelog(_render(preamble, releases, link_refs))
    print("Updated CHANGELOG.md [Unreleased] section.")


def cmd_release(version: str) -> None:
    """
    Promote [Unreleased] to [<version>] - <today>, insert a fresh [Unreleased]
    above it, and add a link reference for the new version.
    """
    if not re.match(r"^\d+\.\d+\.\d+$", version):
        raise ChangelogError(f"Invalid semver version: {version!r}")
    today = _dt.date.today().isoformat()

    content = _read_changelog()
    preamble, releases, link_refs = _split_into_releases(content)

    idx = _find_unreleased(releases)
    if idx == -1:
        raise ChangelogError(
            "CHANGELOG.md has no [Unreleased] section to promote. "
            "Either no PRs landed since the last release, or the file is malformed."
        )

    _, body = releases[idx]
    if not any(ln.strip() for ln in body):
        # Empty Unreleased: still create a release section so the workflow
        # has a non-empty entry. Use a placeholder note rather than failing.
        body = ["### Changed", "", "- Internal improvements."]

    new_heading = f"## [{version}] - {today}"
    releases[idx] = (new_heading, body)
    releases.insert(idx, ("## [Unreleased]", []))

    # Add link ref for the new version, deduped, sorted with newest first
    # after [unreleased].
    new_link = (
        f"[{version}]: https://github.com/{public_releases_repo()}/releases/tag/v{version}"
    )
    unreleased_link = (
        f"[unreleased]: https://github.com/{public_releases_repo()}/compare/v{version}...HEAD"
    )

    # Drop any existing link refs for the new version or [unreleased]; we'll
    # rewrite them in the right order.
    pruned: list[str] = []
    for ref in link_refs:
        s = ref.strip().lower()
        if s.startswith(f"[{version.lower()}]:"):
            continue
        if s.startswith("[unreleased]:"):
            continue
        pruned.append(ref)
    link_refs = [unreleased_link, new_link] + pruned

    _write_changelog(_render(preamble, releases, link_refs))
    cmd_notes(version)


def cmd_notes(version: str) -> None:
    """Print the body (no heading) of the [<version>] section to stdout."""
    content = _read_changelog()
    _, releases, _ = _split_into_releases(content)
    for heading, body in releases:
        m = re.match(r"^##\s+\[([^\]]+)\]", heading)
        if not m:
            continue
        if m.group(1).lower() == version.lower():
            text = "\n".join(body).strip()
            if not text:
                print("Internal improvements.")
            else:
                print(text)
            return
    raise ChangelogError(f"Version {version!r} not found in CHANGELOG.md")


# ---------------------------------------------------------------------------
# CLI
# ---------------------------------------------------------------------------


def _read_input(path: str) -> str:
    if path == "-":
        return sys.stdin.read()
    return Path(path).read_text(encoding="utf-8")


# Bumps that change no version, and therefore produce no changelog entry.
NO_VERSION_BUMPS = ("chore", "docs")

# Scopes with nothing shippable, so nothing to write a changelog about.
NO_CHANGELOG_SCOPES = ("engine",)


def changelog_required(branch: str) -> bool:
    """Whether this branch is one a changelog entry can come from.

    The branch prefix already says. `chore/` and `docs/` are defined as
    no-version-bump, and the engine ships no artifact -- asking the author to
    type `skip` is asking them to restate what they chose when they named the
    branch, in a second place that can disagree with the first.
    """
    parts = branch.split("/")
    if len(parts) < 3:
        return True  # unparseable: ask for one rather than let it through
    bump, scope = parts[0], parts[1]
    return bump not in NO_VERSION_BUMPS and scope not in NO_CHANGELOG_SCOPES


def has_changelog_section(text: str) -> bool:
    return re.search(r"^##\s+Changelog\s*$", text, re.M | re.I) is not None


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(prog="changelog.py")
    parser.add_argument("--scope-dir", default=None,
                        help="scope whose CHANGELOG.md to act on, e.g. games/selva-oscura")
    sub = parser.add_subparsers(dest="cmd", required=True)

    p_val = sub.add_parser("validate-pr-body", help="Validate a PR body")
    p_val.add_argument("file", help="path to PR body file, or - for stdin")
    p_val.add_argument("--branch", default="",
                       help="source branch; scopes that cannot produce a changelog "
                            "entry are not asked for one")

    p_ext = sub.add_parser("extract-pr-body", help="Extract bullets from a PR body")
    p_ext.add_argument("file", help="path to PR body file, or - for stdin")

    p_pre = sub.add_parser("prepend", help="Validate + merge PR bullets into [Unreleased]")
    p_pre.add_argument("file", help="path to PR body file, or - for stdin")

    p_rel = sub.add_parser("release", help="Promote [Unreleased] to a real version")
    p_rel.add_argument("version", help="semver version (e.g. 0.4.0)")

    p_notes = sub.add_parser("notes", help="Print the body of a release section")
    p_notes.add_argument("version", help="semver version")

    args = parser.parse_args(argv)
    set_scope(args.scope_dir)

    try:
        if args.cmd == "validate-pr-body":
            text = _read_input(args.file)
            if not changelog_required(args.branch) and not has_changelog_section(text):
                print(f"OK: branch '{args.branch}' ships no changelog entry; "
                      "no `## Changelog` section required.")
                return 0
            validate_pr_body(text)
            print("OK: `## Changelog` section is valid.")
            return 0
        if args.cmd == "extract-pr-body":
            text = _read_input(args.file)
            grouped = validate_pr_body(text)
            if not grouped:
                print("skip")
                return 0
            for cat in sorted(grouped.keys(), key=lambda c: CATEGORY_ORDER[c]):
                print(f"### {cat}")
                for desc in grouped[cat]:
                    print(f"- {desc}")
            return 0
        if args.cmd == "prepend":
            text = _read_input(args.file)
            cmd_prepend(text)
            return 0
        if args.cmd == "release":
            cmd_release(args.version)
            return 0
        if args.cmd == "notes":
            cmd_notes(args.version)
            return 0
    except ChangelogError as e:
        source = args.file if args.cmd in ("validate-pr-body", "extract-pr-body", "prepend") else "CHANGELOG.md"
        _emit_error(e, source)
        return 1
    return 1


if __name__ == "__main__":
    raise SystemExit(main())
