#!/usr/bin/env python3
"""Convert art/inferno_sprites.h (row-major MSB-left) to our engine's
column-major page-major byte format. Emits paste-ready C++ to stdout.
"""
from __future__ import annotations
import re, sys
from pathlib import Path

text = Path("art/inferno_sprites.h").read_text()
pattern = re.compile(
    r"static\s+const\s+uint8_t\s+(\w+)\[\]\s+PROGMEM\s*=\s*\{(.*?)\};",
    re.DOTALL,
)

def parse_bytes(blob): return [int(x, 0) for x in re.findall(r"0x[0-9A-Fa-f]+|\b\d+\b", blob)]

def transpose(width, height, row_major):
    bytes_per_row = (width + 7) // 8
    grid = [[False] * width for _ in range(height)]
    for y in range(height):
        for col_byte in range(bytes_per_row):
            byte = row_major[y * bytes_per_row + col_byte]
            for bit in range(8):
                x = col_byte * 8 + bit
                if x >= width: break
                if byte & (1 << (7 - bit)):
                    grid[y][x] = True
    pages = (height + 7) // 8
    out = bytearray(width * pages)
    for p in range(pages):
        for x in range(width):
            b = 0
            for bit in range(8):
                y = p * 8 + bit
                if y >= height: break
                if grid[y][x]: b |= 1 << bit
            out[p * width + x] = b
    return bytes(out)

def emit(name, width, height, data):
    lines = [f"// {width}x{height} {name}",
             f"const u8 {name.lower()}_data[{len(data)}] PROGMEM = {{"]
    pages = (height + 7) // 8
    for p in range(pages):
        chunk = data[p * width:(p + 1) * width]
        for i in range(0, len(chunk), 16):
            lines.append("    " + ", ".join(f"0x{b:02X}" for b in chunk[i:i + 16]) + ",")
    lines.append("};")
    return "\n".join(lines)

print("// === Auto-converted Dante sprites ===\n")
for m in pattern.finditer(text):
    name = m.group(1)
    nums = parse_bytes(m.group(2))
    width, height = nums[0], nums[1]
    src_bytes = nums[2:]
    expected = ((width + 7) // 8) * height
    if len(src_bytes) != expected:
        sys.stderr.write(f"WARN: {name} expected {expected} bytes, got {len(src_bytes)}\n")
    out = transpose(width, height, src_bytes)
    print(emit(name, width, height, out))
    print(f"// widths[{name}] = {width}, heights[{name}] = {height}\n")
