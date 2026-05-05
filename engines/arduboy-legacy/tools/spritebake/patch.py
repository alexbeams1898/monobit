"""Safe patcher for sprites.cpp.

Replaces existing `const u8 NAME_data[N] PROGMEM = { ... };` blocks with
freshly-baked ones. Preserves the file's surrounding text, indentation,
comments, and include order. Only touches the specific declaration being
updated.

Invariants enforced:
  - Exactly one match or no-op (multiple matches = abort, ambiguous)
  - Replacement roundtrip-decodes to the same SpriteData (byte-identical)
  - File is UTF-8 throughout
  - No trailing-whitespace or CRLF churn (preserves thy line endings)

These rules exist because a silent miswrite here poisons every sprite
silently; the build still greens but the game renders garbage.
"""

from __future__ import annotations

import re
from dataclasses import dataclass
from pathlib import Path

from .core import SpriteData
from .encode import encode, decode, format_c_array


@dataclass
class PatchResult:
    name: str
    status: str  # "replaced" | "not_found" | "ambiguous" | "unchanged"
    bytes_written: int = 0
    old_len: int = 0
    new_len: int = 0


_DECL_RE_FMT = (
    r"(?P<leading>^[ \t]*)"
    r"const\s+u8\s+{name}\s*\[(?P<old_n>\d+)\]"
    r"\s*PROGMEM\s*=\s*\{{(?P<body>[^}}]*)\}}\s*;"
)


def _detect_line_ending(text: str) -> str:
    if "\r\n" in text:
        return "\r\n"
    return "\n"


def _preserve_line_endings(new_block: str, target_le: str) -> str:
    if target_le == "\r\n":
        return new_block.replace("\n", "\r\n")
    return new_block


def patch_sprites_cpp(
    path: Path | str,
    named_sprites: dict[str, SpriteData],
    *,
    layout: str = "col-major-topbit0",
    progmem: bool = True,
    dry_run: bool = False,
    verify_roundtrip: bool = True,
) -> list[PatchResult]:
    """Replace `NAME_data` blocks for every sprite in the mapping.

    Names in the mapping are the *full* C identifiers (e.g. "boss_lucifer_data"),
    not bare sprite names — the caller controls the _data suffix convention.
    """
    path = Path(path)
    original = path.read_text(encoding="utf-8")
    text = original
    le = _detect_line_ending(original)

    results: list[PatchResult] = []

    for ident, sprite in named_sprites.items():
        pat = re.compile(_DECL_RE_FMT.format(name=re.escape(ident)),
                         flags=re.MULTILINE | re.DOTALL)
        matches = list(pat.finditer(text))
        if len(matches) == 0:
            results.append(PatchResult(ident, "not_found"))
            continue
        if len(matches) > 1:
            results.append(PatchResult(ident, "ambiguous"))
            continue

        m = matches[0]
        # Build the replacement block. We preserve the leading whitespace
        # of the matched line so block indentation stays right.
        leading = m.group("leading")
        new_decl_plain = format_c_array(
            ident, sprite, layout=layout, progmem=progmem,
            comment_header=False,  # patcher doesn't add a header; sprite has comments around it already
        )
        # Re-indent every line with the leading whitespace.
        indented = "\n".join(leading + ln if ln.strip() else ln
                             for ln in new_decl_plain.split("\n"))
        new_decl = _preserve_line_endings(indented, le)

        if verify_roundtrip:
            bytes_ = encode(sprite, layout)
            round = decode(bytes_, sprite.width, sprite.height, layout)
            if not (round.pixels == sprite.pixels).all():
                raise RuntimeError(
                    f"roundtrip verification failed for {ident!r} — encode/decode mismatch"
                )

        old_n = int(m.group("old_n"))
        new_n = len(encode(sprite, layout))
        text = text[:m.start()] + new_decl + text[m.end():]
        results.append(PatchResult(
            ident, "replaced",
            bytes_written=new_n,
            old_len=old_n, new_len=new_n,
        ))

    if not dry_run and text != original:
        # Windows-safe atomic write: write to temp then replace.
        tmp = path.with_suffix(path.suffix + ".tmp")
        tmp.write_text(text, encoding="utf-8", newline="")
        tmp.replace(path)
    return results


def update_size_tables(
    path: Path | str,
    widths: dict[str, int],
    heights: dict[str, int],
    *,
    dry_run: bool = False,
) -> None:
    """Update the WIDTHS[COUNT] and HEIGHTS[COUNT] PROGMEM arrays in sprites.cpp.

    Locates the arrays by their `const u8 WIDTHS[COUNT] PROGMEM` /
    `const u8 HEIGHTS[COUNT] PROGMEM` prefix. Each line in the body is
    expected to have a trailing `// NAME` or similar comment naming the
    sprite; we update numbers in place by matching those comments.

    This is necessarily comment-driven because the arrays don't carry
    per-entry identifiers. Callers who want cleaner updates should move
    to a generated sprite-table header.
    """
    # Intentionally minimal — most callers just rewrite the whole table
    # by pattern after bake. Keeping this as a stub so the shape is
    # clear; full implementation deferred until a caller needs it.
    raise NotImplementedError(
        "update_size_tables is not yet implemented — generate the "
        "WIDTHS/HEIGHTS tables from the manifest instead (see codegen.py)"
    )


def read_existing_sprite(path: Path | str, ident: str,
                         width: int, height: int,
                         layout: str = "col-major-topbit0") -> SpriteData | None:
    """Pull an existing NAME_data block out of a .cpp file and decode it.

    Useful for round-tripping hand-edits into the editor, or for diffs.
    """
    text = Path(path).read_text(encoding="utf-8")
    pat = re.compile(_DECL_RE_FMT.format(name=re.escape(ident)),
                     flags=re.MULTILINE | re.DOTALL)
    m = pat.search(text)
    if not m:
        return None
    from .encode import parse_byte_array
    bytes_ = parse_byte_array(m.group("body"))
    return decode(bytes_, width, height, layout)
