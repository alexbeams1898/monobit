#!/usr/bin/env python3
"""Decode PROGMEM logo bytes from sprites.cpp back into a magnified PNG.

Used to eyeball glyphs when a specific letter looks broken. Prints the logo
as ASCII, writes a scale-8 PNG, and opens it.
"""
import argparse, re, subprocess, sys
from pathlib import Path
from PIL import Image

SRC = Path(__file__).resolve().parent.parent / "games" / "rpg" / "sprites.cpp"

def extract_bytes(symbol: str) -> list[int]:
    txt = SRC.read_text()
    m = re.search(rf"{symbol}\s*\[(\d+)\]\s*PROGMEM\s*=\s*\{{(.*?)\}};", txt, re.S)
    if not m:
        sys.exit(f"symbol {symbol} not found in {SRC}")
    count = int(m.group(1))
    body = m.group(2)
    nums = [int(x, 16) for x in re.findall(r"0x[0-9A-Fa-f]+", body)]
    assert len(nums) == count, f"parsed {len(nums)} bytes, expected {count}"
    return nums

def decode(bytes_: list[int], width: int, height: int) -> Image.Image:
    pages = (height + 7) // 8
    assert len(bytes_) == pages * width, f"len={len(bytes_)} pages={pages} width={width}"
    img = Image.new("L", (width, height), 0)
    px = img.load()
    for p in range(pages):
        for col in range(width):
            b = bytes_[p * width + col]
            for bit in range(8):
                y = p * 8 + bit
                if y >= height:
                    break
                if b & (1 << bit):
                    px[col, y] = 255
    return img

def ascii_dump(img: Image.Image) -> str:
    w, h = img.size
    px = img.load()
    lines = []
    for y in range(h):
        lines.append("".join("#" if px[x, y] else "." for x in range(w)))
    return "\n".join(lines)

def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("symbol")
    ap.add_argument("width", type=int)
    ap.add_argument("height", type=int)
    ap.add_argument("--out", required=True)
    ap.add_argument("--scale", type=int, default=8)
    ap.add_argument("--open", action="store_true")
    args = ap.parse_args()

    b = extract_bytes(args.symbol)
    img = decode(b, args.width, args.height)
    print(ascii_dump(img))
    big = img.resize((img.width * args.scale, img.height * args.scale), Image.NEAREST)
    big.save(args.out)
    print(f"\nwrote {args.out}")
    if args.open:
        subprocess.run(["cmd", "/c", "start", "", args.out])

if __name__ == "__main__":
    main()
