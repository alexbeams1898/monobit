#!/usr/bin/env python3
"""Convert any image to a dithered 128x64 1-bit framebuffer blob.

Output format matches the engine's framebuffer (and SSD1306 GDDRAM):
  - 1024 bytes total
  - 8 pages of 128 columns
  - one byte per (column, page); bit 0 = top pixel of that 8-row column,
    bit 7 = bottom

So `fb::draw_full_image()` can copy this straight into `fb::buffer` with
no transposition.

Usage
-----
    python scripts/png_to_image.py art/title.png --name TITLE
    python scripts/png_to_image.py art/title.png --name TITLE --dither bayer
    python scripts/png_to_image.py art/title.png --name TITLE --fit crop
    python scripts/png_to_image.py art/title.png --name TITLE --invert

Flags:
  --name FOO        symbol prefix for the generated array (default: IMAGE)
  --dither MODE     "floyd" (default, organic) or "bayer" (regular crosshatch)
  --fit MODE        "squish" (default, fits whole image), "crop_top",
                    "crop_center", "crop_bottom" — how to fit non-128x64
                    sources to 128x64
  --invert          swap on/off bits (use if your image is dark-foreground-
                    on-light-background)
"""

from __future__ import annotations

import argparse
import sys
from pathlib import Path

try:
    from PIL import Image, ImageEnhance, ImageOps
except ImportError:
    sys.stderr.write("ERROR: Pillow not installed. pacman -S mingw-w64-x86_64-python-pillow\n")
    sys.exit(1)

W = 128
H = 64


def fit_image(img: Image.Image, mode: str, resample: int = Image.LANCZOS) -> Image.Image:
    """Resize/crop the source to exactly 128x64."""
    src_w, src_h = img.size
    if (src_w, src_h) == (W, H):
        return img
    if mode == "squish":
        return img.resize((W, H), resample)
    if mode == "box":
        # Same as "squish" but with BOX averaging — preserves clusters in
        # already-1-bit-adjacent source art. Best paired with --dither thresh*.
        return img.resize((W, H), Image.BOX)
    # Crop modes: scale so width matches 128, then take the requested H slice.
    scale = W / src_w
    new_h = int(round(src_h * scale))
    img = img.resize((W, new_h), Image.LANCZOS)
    if new_h <= H:
        # Padded to top; pad bottom with black.
        out = Image.new("L", (W, H), 0)
        out.paste(img, (0, 0))
        return out
    extra = new_h - H
    if mode == "crop_top":
        top = 0
    elif mode == "crop_bottom":
        top = extra
    else:  # crop_center
        top = extra // 2
    return img.crop((0, top, W, top + H))


def dither(img: Image.Image, mode: str) -> Image.Image:
    """Convert grayscale image to 1-bit using the requested dither algorithm.

    Modes prefixed with "thresh" (e.g. "thresh96") skip dithering entirely
    and just threshold at the given value (0..255). Lower threshold keeps
    more ink; higher threshold drops to skeletal silhouette. Useful when
    the source is already 1-bit-adjacent and dither would only add noise.
    """
    if mode.startswith("thresh"):
        try:
            t = int(mode[len("thresh"):])
        except ValueError:
            t = 128
        return img.point(lambda p: 255 if p >= t else 0, mode="1")
    if mode == "floyd":
        # Pillow's "1" conversion uses Floyd-Steinberg by default.
        return img.convert("1", dither=Image.FLOYDSTEINBERG)
    if mode == "bayer":
        # Manual 4x4 ordered (Bayer) dither — Pillow doesn't expose it directly.
        bayer4 = [
            [ 0,  8,  2, 10],
            [12,  4, 14,  6],
            [ 3, 11,  1,  9],
            [15,  7, 13,  5],
        ]
        out = Image.new("1", (W, H), 0)
        px_in  = img.load()
        px_out = out.load()
        for y in range(H):
            for x in range(W):
                threshold = (bayer4[y % 4][x % 4] + 0.5) * (256.0 / 16.0)
                px_out[x, y] = 255 if px_in[x, y] > threshold else 0
        return out
    if mode == "atkinson":
        # Atkinson dither (used on the original Mac). Spreads only 6/8 of the
        # error to neighbors, so pure-black and pure-white areas stay pure
        # instead of bleeding noise into them. Better for high-contrast images.
        # Pattern (X = current pixel, numbers = error fraction * 8):
        #         X 1 1
        #     1 1 1
        #         1
        out = Image.new("1", (W, H), 0)
        # Work in a mutable float buffer so we can carry error.
        buf = [[float(img.getpixel((x, y))) for x in range(W)] for y in range(H)]
        for y in range(H):
            for x in range(W):
                old = buf[y][x]
                new = 255.0 if old >= 128 else 0.0
                buf[y][x] = new
                err = (old - new) / 8.0  # only 6/8 of the error gets distributed
                for (dx, dy) in [(1, 0), (2, 0), (-1, 1), (0, 1), (1, 1), (0, 2)]:
                    nx, ny = x + dx, y + dy
                    if 0 <= nx < W and 0 <= ny < H:
                        buf[ny][nx] += err
        px_out = out.load()
        for y in range(H):
            for x in range(W):
                px_out[x, y] = 255 if buf[y][x] >= 128 else 0
        return out
    sys.stderr.write(f"ERROR: unknown dither mode: {mode}\n")
    sys.exit(1)


def preprocess(img: Image.Image, contrast: float, posterize_levels: int,
               autocontrast: bool) -> Image.Image:
    """Apply pre-dither tone shaping. All ops are no-ops when disabled."""
    if autocontrast:
        # Stretch histogram so darkest = 0, brightest = 255.
        img = ImageOps.autocontrast(img, cutoff=2)
    if contrast != 1.0:
        img = ImageEnhance.Contrast(img).enhance(contrast)
    if posterize_levels > 0:
        # Reduce to N levels first, so the dither has clean bands to express
        # rather than a continuous gradient that smears across the image.
        # ImageOps.posterize takes "bits to keep" (1..8); convert N levels to bits.
        bits = max(1, min(8, (posterize_levels - 1).bit_length()))
        img = ImageOps.posterize(img, bits)
    return img


def to_framebuffer(img: Image.Image, invert: bool) -> bytes:
    """Pack a 128x64 1-bit image into 1024 bytes in SSD1306 page-major layout."""
    px = img.load()
    out = bytearray(W * H // 8)
    for page in range(H // 8):
        for x in range(W):
            byte = 0
            for bit in range(8):
                y = page * 8 + bit
                pixel_on = px[x, y] != 0
                if invert:
                    pixel_on = not pixel_on
                if pixel_on:
                    byte |= 1 << bit
            out[page * W + x] = byte
    return bytes(out)


def emit_c_array(name: str, data: bytes) -> str:
    lines = [
        f"// 128x64 dithered image ({len(data)} bytes, page-major)",
        f"// Auto-generated by scripts/png_to_image.py",
        f"const u8 {name}_data[{len(data)}] PROGMEM = {{",
    ]
    # 16 bytes per source line, 64 lines total (1024/16).
    for i in range(0, len(data), 16):
        chunk = data[i:i + 16]
        lines.append("    " + ", ".join(f"0x{b:02X}" for b in chunk) + ",")
    lines.append("};")
    return "\n".join(lines)


def main() -> int:
    ap = argparse.ArgumentParser(description="Convert image to 128x64 1-bit framebuffer blob.")
    ap.add_argument("png", type=Path)
    ap.add_argument("--name", default="IMAGE")
    ap.add_argument("--dither", default="floyd",
                    help="floyd | bayer | atkinson | thresh<N> (e.g. thresh96 = no "
                         "dither, threshold at 96)")
    ap.add_argument("--fit", default="squish",
                    choices=["squish", "box", "crop_top", "crop_center", "crop_bottom"])
    ap.add_argument("--invert", action="store_true")
    ap.add_argument("--contrast", type=float, default=1.0,
                    help="Contrast multiplier before dither (1.0 = none, 1.5 = +50%)")
    ap.add_argument("--posterize", type=int, default=0,
                    help="Reduce to N brightness levels before dither (0 = off, "
                         "4 is a good starting point for photos)")
    ap.add_argument("--autocontrast", action="store_true",
                    help="Stretch histogram to use full 0..255 range before dither")
    args = ap.parse_args()

    if not args.png.exists():
        sys.stderr.write(f"ERROR: file not found: {args.png}\n")
        return 1

    img = Image.open(args.png).convert("L")
    img = fit_image(img, args.fit)
    img = preprocess(img, args.contrast, args.posterize, args.autocontrast)
    img = dither(img, args.dither)
    data = to_framebuffer(img, args.invert)
    print(emit_c_array(args.name, data))
    return 0


if __name__ == "__main__":
    sys.exit(main())
