"""Byte-layout encoders and decoders.

The mono engine's framebuffer is SSD1306 page-major (column-major, bit 0
= top row of a page). Sprites follow the same convention so draw_sprite
can scan bytes directly. Non-canonical layouts are supported for
interoperability with tools or fonts.

A layout is a named function pair (encode, decode) registered here.
"""

from __future__ import annotations

from dataclasses import dataclass
from typing import Callable

import numpy as np

from .core import SpriteData


@dataclass(frozen=True)
class Layout:
    name: str
    description: str
    encode_fn: Callable[[np.ndarray], list[int]]
    decode_fn: Callable[[list[int] | bytes, int, int], np.ndarray]


# ---- Encoders ----

def _encode_col_major_topbit0(pixels: np.ndarray) -> list[int]:
    """Column-major, each byte covers 8 rows, bit 0 = top row of page.
    This is the project default — matches the SSD1306 page layout."""
    h, w = pixels.shape
    pages = (h + 7) // 8
    out: list[int] = []
    for p in range(pages):
        for x in range(w):
            b = 0
            for bit in range(8):
                y = p * 8 + bit
                if y >= h:
                    break
                if pixels[y, x]:
                    b |= 1 << bit
            out.append(b)
    return out


def _decode_col_major_topbit0(bytes_: list[int] | bytes, w: int, h: int) -> np.ndarray:
    pages = (h + 7) // 8
    arr = np.zeros((h, w), dtype=bool)
    for p in range(pages):
        for x in range(w):
            idx = p * w + x
            if idx >= len(bytes_):
                break
            b = bytes_[idx]
            for bit in range(8):
                y = p * 8 + bit
                if y >= h:
                    break
                if (b >> bit) & 1:
                    arr[y, x] = True
    return arr


def _encode_col_major_topbit7(pixels: np.ndarray) -> list[int]:
    h, w = pixels.shape
    pages = (h + 7) // 8
    out: list[int] = []
    for p in range(pages):
        for x in range(w):
            b = 0
            for bit in range(8):
                y = p * 8 + bit
                if y >= h:
                    break
                if pixels[y, x]:
                    b |= 1 << (7 - bit)
            out.append(b)
    return out


def _decode_col_major_topbit7(bytes_: list[int] | bytes, w: int, h: int) -> np.ndarray:
    pages = (h + 7) // 8
    arr = np.zeros((h, w), dtype=bool)
    for p in range(pages):
        for x in range(w):
            idx = p * w + x
            if idx >= len(bytes_):
                break
            b = bytes_[idx]
            for bit in range(8):
                y = p * 8 + bit
                if y >= h:
                    break
                if (b >> (7 - bit)) & 1:
                    arr[y, x] = True
    return arr


def _encode_row_major_msb(pixels: np.ndarray) -> list[int]:
    h, w = pixels.shape
    stride = (w + 7) // 8
    out: list[int] = []
    for y in range(h):
        for xb in range(stride):
            b = 0
            for bit in range(8):
                x = xb * 8 + bit
                if x >= w:
                    break
                if pixels[y, x]:
                    b |= 1 << (7 - bit)
            out.append(b)
    return out


def _decode_row_major_msb(bytes_: list[int] | bytes, w: int, h: int) -> np.ndarray:
    stride = (w + 7) // 8
    arr = np.zeros((h, w), dtype=bool)
    for y in range(h):
        for xb in range(stride):
            idx = y * stride + xb
            if idx >= len(bytes_):
                break
            b = bytes_[idx]
            for bit in range(8):
                x = xb * 8 + bit
                if x >= w:
                    break
                if (b >> (7 - bit)) & 1:
                    arr[y, x] = True
    return arr


LAYOUTS: dict[str, Layout] = {
    "col-major-topbit0": Layout(
        "col-major-topbit0",
        "Project default: column-major, bit 0 = top row of 8-row page.",
        _encode_col_major_topbit0,
        _decode_col_major_topbit0,
    ),
    "col-major-topbit7": Layout(
        "col-major-topbit7",
        "Column-major, bit 7 = top row of 8-row page.",
        _encode_col_major_topbit7,
        _decode_col_major_topbit7,
    ),
    "row-major-msb": Layout(
        "row-major-msb",
        "Row-major, MSB = leftmost pixel in each byte.",
        _encode_row_major_msb,
        _decode_row_major_msb,
    ),
}


def encode(sprite: SpriteData, layout: str = "col-major-topbit0") -> list[int]:
    """Return byte list in the given layout."""
    if layout not in LAYOUTS:
        raise KeyError(f"unknown layout {layout!r}; available: {list(LAYOUTS)}")
    return LAYOUTS[layout].encode_fn(sprite.pixels)


def decode(bytes_: list[int] | bytes, width: int, height: int,
           layout: str = "col-major-topbit0") -> SpriteData:
    """Reconstruct a SpriteData from packed bytes."""
    if layout not in LAYOUTS:
        raise KeyError(f"unknown layout {layout!r}")
    arr = LAYOUTS[layout].decode_fn(bytes_, width, height)
    return SpriteData(pixels=arr)


def format_c_array(name: str, sprite: SpriteData, *,
                   layout: str = "col-major-topbit0",
                   progmem: bool = True,
                   hex_format: bool = True,
                   per_line: int = 12,
                   comment_header: bool = True) -> str:
    """Emit a C array declaration ready to paste into sprites.cpp.

    Example output:
        // 20x24, col-major-topbit0, 60 bytes.
        const u8 boss_lucifer_data[60] PROGMEM = {
            0x00, 0x00, 0x18, 0x3C, 0x30, 0x00, 0x00, 0x00,
            ...
        };
    """
    bytes_ = encode(sprite, layout)
    fmt = (lambda b: f"0x{b:02X}") if hex_format else str
    lines = []
    for i in range(0, len(bytes_), per_line):
        chunk = ", ".join(fmt(b) for b in bytes_[i:i+per_line])
        lines.append(f"    {chunk},")
    body = "\n".join(lines)

    parts = []
    if comment_header:
        parts.append(f"// {sprite.width}x{sprite.height}, {layout}, {len(bytes_)} bytes.")
    qualifier = " PROGMEM" if progmem else ""
    parts.append(f"const u8 {name}[{len(bytes_)}]{qualifier} = {{")
    parts.append(body)
    parts.append("};")
    return "\n".join(parts)


def parse_byte_array(text: str) -> list[int]:
    """Parse a C-style byte list out of arbitrary text.

    Accepts 0x.., 0b.., and plain decimals INSIDE a `{ ... }` block, or
    in plain comma-separated lists. Ignores //, /* */ comments and any
    text outside the brace pair (so array sizes like `[64]` don't leak
    in as stray bytes).
    """
    import re

    # Strip C++ / C comments
    text = re.sub(r"//[^\n]*", "", text)
    text = re.sub(r"/\*.*?\*/", "", text, flags=re.DOTALL)

    # If the text contains a {...} block, restrict to its contents (the
    # last one wins, to survive nested sample text).
    brace_match = re.search(r"\{(.*)\}", text, flags=re.DOTALL)
    if brace_match:
        text = brace_match.group(1)

    tokens = re.findall(r"0x[0-9A-Fa-f]+|0b[01]+|\d+", text)
    out = []
    for t in tokens:
        if t.lower().startswith("0x"):
            v = int(t, 16)
        elif t.lower().startswith("0b"):
            v = int(t[2:], 2)
        else:
            v = int(t, 10)
        if 0 <= v <= 255:
            out.append(v)
    return out
