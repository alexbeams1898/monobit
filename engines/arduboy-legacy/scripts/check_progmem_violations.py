#!/usr/bin/env python3
"""Fail the build if any file outside platform/arduboy/ includes
<avr/pgmspace.h> directly.

The rule from CLAUDE.md: engine/ and games/ must go through the portable
shim engine/progmem.h. Direct <avr/pgmspace.h> pulls in <avr/io.h> which
drops AVR register macros (SE, EE, etc.) into the global namespace and
collides with enum names like Dir::SE.

Run via `make audit`.
"""

from __future__ import annotations

import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
ALLOWED_PREFIX = "platform/arduboy/"
ALSO_ALLOWED = {"engine/progmem.h"}  # the shim itself obviously includes it

BAD_INCLUDE = re.compile(r'^\s*#\s*include\s*<avr/pgmspace\.h>', re.MULTILINE)

SEARCH_DIRS = ("engine", "games", "platform")
SEARCH_EXTS = (".cpp", ".c", ".h", ".hpp")


def main() -> int:
    violations: list[str] = []
    for sub in SEARCH_DIRS:
        base = ROOT / sub
        if not base.exists():
            continue
        for p in base.rglob("*"):
            if p.is_dir():
                continue
            if p.suffix not in SEARCH_EXTS:
                continue
            rel = p.relative_to(ROOT).as_posix()
            if rel.startswith(ALLOWED_PREFIX):
                continue
            if rel in ALSO_ALLOWED:
                continue
            try:
                text = p.read_text(encoding="utf-8")
            except UnicodeDecodeError:
                continue
            for m in BAD_INCLUDE.finditer(text):
                line = text[: m.start()].count("\n") + 1
                violations.append(f"{rel}:{line}: raw <avr/pgmspace.h>")

    if violations:
        sys.stderr.write(
            "\n"
            "#### PROGMEM VIOLATION ####\n"
            "The following files include <avr/pgmspace.h> directly.\n"
            "Use #include \"progmem.h\" (the engine's portable shim) instead.\n"
            "\n"
        )
        for v in violations:
            sys.stderr.write(f"  {v}\n")
        sys.stderr.write("\n")
        return 1

    print("progmem check OK")
    return 0


if __name__ == "__main__":
    sys.exit(main())
