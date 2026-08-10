#!/usr/bin/env python3
"""Design lint: the visual standards, enforced by machine.

Standards (violations are build-stopping, not suggestions -- taste re-applied by hand decays
one exception at a time, exactly like the folder taxonomy):

1. ONE FONT, ONE LOADER. The game's face is VT323 (the trade's dot-matrix paperwork). Only
   ScreenStyle may call FontManager::loadFont or name a .ttf; assets/fonts holds only the
   allowlisted files. Anyone needing text goes through screen_style.

2. ONE PALETTE. Color{...} literals may be constructed only inside ScreenStyle -- every other
   file uses the named constants (screen_style::k*). A colour that is not in the palette is a
   colour the game does not have.

3. INTEGER UI SCALE. Font sizes come off ScreenStyle's integer ladder (base px * k, where k is
   a whole number from the window height) -- fractional sizes blur a pixel face. Enforced
   structurally by rule 1: sizing lives in the one place, reviewed once.

Run from the repo root; exits 1 with a list of violations.
"""

from __future__ import annotations

import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
SRC = ROOT / "src"
INCLUDE = ROOT / "include"
FONTS = ROOT / "assets" / "fonts"

FONT_ALLOWLIST = {"VT323-Regular.ttf", "VT323-OFL.txt"}
STYLE_FILES = {"ScreenStyle.cpp", "ScreenStyle.h"}

COLOR_LITERAL = re.compile(r"\bColor\s*\{")
FONT_LOAD = re.compile(r"FontManager::loadFont")
TTF_LITERAL = re.compile(r'"[^"]*\.ttf"')
# Alignment is ScreenStyle's idiom: measuring text anywhere else means someone is doing
# per-site alignment math, which is how boxes stop centring.
MEASURE = re.compile(r"UIRenderer::measureText|FontManager::lineHeight")


def main() -> int:
    problems: list[str] = []

    for f in FONTS.iterdir():
        if f.name not in FONT_ALLOWLIST:
            problems.append(f"assets/fonts/{f.name}: not an allowlisted face -- the game has "
                            f"one font, chosen once")

    for f in list(SRC.rglob("*.cpp")) + list(INCLUDE.rglob("*.h")):
        if f.name in STYLE_FILES:
            continue
        text = f.read_text(encoding="utf-8")
        rel = f.relative_to(ROOT)
        for lineno, line in enumerate(text.splitlines(), 1):
            if COLOR_LITERAL.search(line):
                problems.append(f"{rel}:{lineno}: Color literal outside ScreenStyle -- use a "
                                f"palette constant, or add one")
            if FONT_LOAD.search(line):
                problems.append(f"{rel}:{lineno}: fonts load in ScreenStyle only")
            if TTF_LITERAL.search(line):
                problems.append(f"{rel}:{lineno}: .ttf paths belong to ScreenStyle only")
            if MEASURE.search(line):
                problems.append(f"{rel}:{lineno}: text measuring outside ScreenStyle -- use "
                                f"textRight/textInBox instead of per-site alignment math")

    if problems:
        print(f"check_design: {len(problems)} violation(s)")
        for p in problems:
            print(f"  {p}")
        return 1
    print("check_design: clean")
    return 0


if __name__ == "__main__":
    sys.exit(main())
