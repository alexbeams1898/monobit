"""One-shot extractor for the SECOND_DEATH LZ77 byte stream baked into
games/rpg/sprites.cpp. Writes the bytes to data/fx/second_death_lz77.bin
so they can be packed into the FX image and freed from PROGMEM.

Run once (after the bytes are at the canonical location); subsequent
edits should re-encode and overwrite the .bin directly.
"""
from __future__ import annotations

import re
from pathlib import Path

REPO = Path(__file__).resolve().parent.parent
SRC = REPO / "games" / "rpg" / "sprites.cpp"
OUT = REPO / "data" / "fx" / "second_death_lz77.bin"


def main():
    text = SRC.read_text()
    m = re.search(
        r"const u8 logo_second_death_LZ77\[(\d+)\] PROGMEM = \{([^}]+)\};",
        text,
        re.S,
    )
    if not m:
        raise SystemExit("logo_second_death_LZ77 array not found")
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
