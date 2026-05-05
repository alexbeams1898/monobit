"""Shared scan infrastructure: file walker, source iterator, finding type."""

from __future__ import annotations

from dataclasses import dataclass
from pathlib import Path
from typing import Iterable

# Repo root, computed relative to this file (tools/efficiency_lint/lint.py).
ROOT = Path(__file__).resolve().parents[2]

# Where to scan. Per CLAUDE.md, platform/arduboy/ is allowed to use raw
# AVR headers and other "unsafe" idioms; rules irrelevant there are
# exempted per-file in rules.py's _RULE_EXEMPT_FILES. The platform scope
# is in because cross-platform traps (e.g. Windows stdio CRLF translation)
# can only be caught by reading platform/<host>/ code.
DEFAULT_SCAN_DIRS = ("engine", "games", "platform")
DEFAULT_EXTS = (".cpp", ".c", ".h", ".hpp")


@dataclass(frozen=True)
class Finding:
    """One linter hit. Renders as `path:line: rule: message`."""
    rule: str
    path: str          # repo-relative posix path
    line: int          # 1-indexed
    message: str
    severity: str = "error"  # "error" fails the build; "warn" reports only

    def render(self) -> str:
        return f"{self.path}:{self.line}: [{self.severity}:{self.rule}] {self.message}"


def iter_sources(scan_dirs: Iterable[str] = DEFAULT_SCAN_DIRS,
                 exts: Iterable[str] = DEFAULT_EXTS) -> Iterable[tuple[str, str]]:
    """Yield (relpath, content) for every source file under scan_dirs.

    Skips binary/undecodable files silently. Paths are repo-relative posix
    so findings render consistently across platforms.
    """
    for sub in scan_dirs:
        base = ROOT / sub
        if not base.exists():
            continue
        for p in sorted(base.rglob("*")):
            if p.is_dir():
                continue
            if p.suffix not in exts:
                continue
            try:
                text = p.read_text(encoding="utf-8")
            except UnicodeDecodeError as e:
                # Don't silently skip — broken encoding hides the file
                # from cross-file rules (e.g. orphan-progmem-extern), which
                # then false-positive on every symbol the file defined.
                # Loud failure beats silent miss.
                import sys
                print(f"WARN: {p.relative_to(ROOT).as_posix()} is not valid "
                      f"UTF-8 ({e}); lint scan will skip it. Re-save the "
                      f"file as UTF-8 to include it.", file=sys.stderr)
                continue
            yield p.relative_to(ROOT).as_posix(), text


def line_of(text: str, char_offset: int) -> int:
    """1-indexed line number of a character offset within `text`."""
    return text.count("\n", 0, char_offset) + 1
