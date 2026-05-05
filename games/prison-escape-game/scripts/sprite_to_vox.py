"""
Convert a 2D pixel art sprite into a MagicaVoxel .vox file by extruding
non-transparent pixels into voxel slabs.

The side-view sprite becomes the XZ plane (width x height), extruded along
Y (depth). Each pixel gets a configurable depth in voxels.

Usage:
  python sprite_to_vox.py <input.png> <output.vox> [--depth N] [--palette]

  --depth N    Extrusion depth in voxels (default: 4)
  --palette    Include the sprite's colors in the .vox palette
"""

import argparse
import os
import struct
import sys

try:
    from PIL import Image
except ImportError:
    print("Pillow required: pip install Pillow")
    sys.exit(1)


def write_vox(filename, voxels, palette):
    """
    Write a MagicaVoxel .vox file.
    voxels: list of (x, y, z, color_index) tuples
    palette: list of (r, g, b, a) tuples, max 255 entries (index 1-255)
    """
    # --- SIZE chunk ---
    # Find bounding box.
    max_x = max(v[0] for v in voxels) + 1
    max_y = max(v[1] for v in voxels) + 1
    max_z = max(v[2] for v in voxels) + 1

    size_data = struct.pack('<III', max_x, max_y, max_z)
    size_chunk = b'SIZE' + struct.pack('<II', len(size_data), 0) + size_data

    # --- XYZI chunk ---
    xyzi_data = struct.pack('<I', len(voxels))
    for x, y, z, ci in voxels:
        xyzi_data += struct.pack('<BBBB', x, y, z, ci)
    xyzi_chunk = b'XYZI' + struct.pack('<II', len(xyzi_data), 0) + xyzi_data

    # --- RGBA chunk (palette) ---
    rgba_data = b''
    for i in range(256):
        if i < len(palette):
            r, g, b, a = palette[i]
            rgba_data += struct.pack('<BBBB', r, g, b, a)
        else:
            rgba_data += struct.pack('<BBBB', 0, 0, 0, 255)
    rgba_chunk = b'RGBA' + struct.pack('<II', len(rgba_data), 0) + rgba_data

    # --- MAIN chunk ---
    children = size_chunk + xyzi_chunk + rgba_chunk
    main_chunk = b'MAIN' + struct.pack('<II', 0, len(children)) + children

    # --- File ---
    with open(filename, 'wb') as f:
        f.write(b'VOX ')
        f.write(struct.pack('<I', 150))  # version
        f.write(main_chunk)


def main():
    parser = argparse.ArgumentParser(description="Convert sprite to MagicaVoxel .vox")
    parser.add_argument("input", help="Input PNG sprite")
    parser.add_argument("output", help="Output .vox file")
    parser.add_argument("--depth", type=int, default=4,
                        help="Extrusion depth in voxels (default: 4)")
    args = parser.parse_args()

    img = Image.open(args.input).convert("RGBA")
    w, h = img.size
    px = img.load()

    # Build color palette from unique non-transparent colors.
    color_map = {}  # (r,g,b) -> palette index (1-based for .vox)
    palette = []

    for y in range(h):
        for x in range(w):
            r, g, b, a = px[x, y]
            if a == 0:
                continue
            key = (r, g, b)
            if key not in color_map:
                idx = len(palette) + 1  # .vox palette is 1-indexed
                if idx > 255:
                    # Reuse closest existing color.
                    idx = 1
                color_map[key] = idx
                palette.append((r, g, b, 255))

    # Generate voxels: X = sprite x, Z = sprite y (flipped so top=high Z),
    # Y = depth extrusion.
    voxels = []
    depth = args.depth

    for sy in range(h):
        for sx in range(w):
            r, g, b, a = px[sx, sy]
            if a == 0:
                continue
            ci = color_map[(r, g, b)]
            vx = sx
            vz = (h - 1) - sy  # Flip Y so top of sprite = high Z

            # Extrude along Y axis, centered.
            y_start = 0
            for dy in range(depth):
                voxels.append((vx, y_start + dy, vz, ci))

    print(f"Sprite: {w}x{h}, {len(color_map)} colors, {len(voxels)} voxels")
    print(f"Extrusion depth: {depth}")

    write_vox(args.output, voxels, palette)
    print(f"Saved: {args.output}")
    print(f"Open in MagicaVoxel, sculpt the depth, then render from different angles.")


if __name__ == "__main__":
    main()
