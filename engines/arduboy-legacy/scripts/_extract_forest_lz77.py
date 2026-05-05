"""Extract FOREST_LZ77_data bytes from images.cpp into data/fx/forest_lz77.bin
so the bytes can move to FX flash. Mirrors the title_lz77.bin pattern.
"""
from __future__ import annotations

import re
from pathlib import Path

REPO = Path(__file__).resolve().parent.parent
SRC = REPO / "games" / "rpg" / "images.cpp"
OUT = REPO / "data" / "fx" / "forest_lz77.bin"


def main():
    text = SRC.read_text()
    m = re.search(
        r"const u8 FOREST_LZ77_data\[(\d+)\] PROGMEM = \{([^}]+)\};",
        text,
        re.S,
    )
    if not m:
        raise SystemExit("FOREST_LZ77_data array not found")
    count = int(m.group(1))
    body = m.group(2)
    bytes_out = bytes(int(tok, 0) for tok in re.findall(r"0x[0-9A-Fa-f]+", body))
    if len(bytes_out) != count:
        raise SystemExit(f"size mismatch: declared {count}, parsed {len(bytes_out)}")
    OUT.parent.mkdir(parents=True, exist_ok=True)
    OUT.write_bytes(bytes_out)
    print(f"wrote {OUT} ({len(bytes_out)} B)")


if __name__ == "__main__":
    main()
