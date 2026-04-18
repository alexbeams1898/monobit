"""
Render a voxel model (from sprite_to_vox.py) from multiple camera angles
and export as pixel art PNGs.

Uses simple orthographic projection — no MagicaVoxel GUI needed.

Usage:
  python render_vox.py <input.vox> <output_dir> [--size 32]

Produces:
  <output_dir>/<name>_side.png    (side view, same as original)
  <output_dir>/<name>_top.png     (top-down view, for N/S facing)
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


def read_vox(filename):
    """Read a .vox file and return voxels and palette."""
    with open(filename, 'rb') as f:
        magic = f.read(4)
        assert magic == b'VOX ', f"Not a .vox file: {magic}"
        version = struct.unpack('<I', f.read(4))[0]

        voxels = []
        palette = [(0, 0, 0, 0)] * 256
        size_x, size_y, size_z = 0, 0, 0

        def read_chunk():
            chunk_id = f.read(4)
            if len(chunk_id) < 4:
                return None, None, None
            n_bytes, n_children = struct.unpack('<II', f.read(8))
            data = f.read(n_bytes)
            # Skip children bytes (they'll be read as subsequent chunks).
            return chunk_id, data, n_children

        # Read MAIN chunk header.
        chunk_id, data, n_children = read_chunk()
        assert chunk_id == b'MAIN'

        # Read child chunks.
        while True:
            chunk_id, data, n_children = read_chunk()
            if chunk_id is None:
                break
            if chunk_id == b'SIZE':
                size_x, size_y, size_z = struct.unpack('<III', data[:12])
            elif chunk_id == b'XYZI':
                n_voxels = struct.unpack('<I', data[:4])[0]
                for i in range(n_voxels):
                    off = 4 + i * 4
                    x, y, z, ci = struct.unpack('<BBBB', data[off:off+4])
                    voxels.append((x, y, z, ci))
            elif chunk_id == b'RGBA':
                for i in range(256):
                    off = i * 4
                    r, g, b, a = struct.unpack('<BBBB', data[off:off+4])
                    palette[i] = (r, g, b, a)

    return voxels, palette, (size_x, size_y, size_z)


def render_orthographic(voxels, palette, size, view, output_size):
    """
    Render voxels from a given view direction using orthographic projection.

    view: 'side' (looking along Y axis, shows XZ plane)
           'top'  (looking along Z axis, shows XY plane)
           'front' (looking along X axis, shows YZ plane)
    """
    sx, sy, sz = size

    if view == 'side':
        # Camera looks along +Y. Screen X = voxel X, Screen Y = voxel Z (flipped).
        proj_w, proj_h = sx, sz
        def project(x, y, z):
            return x, (sz - 1 - z), y  # screen_x, screen_y, depth
    elif view == 'top':
        # Camera looks along -Z (looking down). Screen X = voxel X, Screen Y = voxel Y.
        proj_w, proj_h = sx, sy
        def project(x, y, z):
            return x, y, (sz - z)  # screen_x, screen_y, depth (higher z = closer)
    elif view == 'front':
        # Camera looks along +X. Screen X = voxel Y, Screen Y = voxel Z (flipped).
        proj_w, proj_h = sy, sz
        def project(x, y, z):
            return y, (sz - 1 - z), x  # screen_x, screen_y, depth
    else:
        raise ValueError(f"Unknown view: {view}")

    # Depth buffer + color buffer.
    depth_buf = {}
    color_buf = {}

    for vx, vy, vz, ci in voxels:
        scr_x, scr_y, depth = project(vx, vy, vz)
        key = (scr_x, scr_y)
        if key not in depth_buf or depth < depth_buf[key]:
            depth_buf[key] = depth
            color_buf[key] = ci

    # Render to image.
    img = Image.new('RGBA', (proj_w, proj_h), (0, 0, 0, 0))
    px = img.load()

    for (scr_x, scr_y), ci in color_buf.items():
        if 0 <= scr_x < proj_w and 0 <= scr_y < proj_h:
            r, g, b, a = palette[ci - 1]  # .vox palette is 1-indexed
            px[scr_x, scr_y] = (r, g, b, 255)

    # Resize to output_size using nearest neighbor (pixel art).
    if output_size and (proj_w != output_size or proj_h != output_size):
        # Scale to fit within output_size, centered.
        scale = min(output_size / proj_w, output_size / proj_h)
        new_w = max(1, int(proj_w * scale))
        new_h = max(1, int(proj_h * scale))
        img = img.resize((new_w, new_h), Image.NEAREST)

        # Center on output canvas.
        canvas = Image.new('RGBA', (output_size, output_size), (0, 0, 0, 0))
        ox = (output_size - new_w) // 2
        oy = (output_size - new_h) // 2
        canvas.paste(img, (ox, oy))
        img = canvas

    return img


def main():
    parser = argparse.ArgumentParser(description="Render .vox from multiple angles")
    parser.add_argument("input", help="Input .vox file")
    parser.add_argument("output_dir", help="Output directory for PNGs")
    parser.add_argument("--size", type=int, default=32,
                        help="Output image size (default: 32)")
    args = parser.parse_args()

    os.makedirs(args.output_dir, exist_ok=True)
    name = os.path.splitext(os.path.basename(args.input))[0]

    voxels, palette, size = read_vox(args.input)
    print(f"Loaded {len(voxels)} voxels, size {size[0]}x{size[1]}x{size[2]}")

    for view in ['side', 'top', 'front']:
        img = render_orthographic(voxels, palette, size, view, args.size)
        out_path = os.path.join(args.output_dir, f"{name}_{view}.png")
        img.save(out_path)
        print(f"Saved: {out_path}")


if __name__ == "__main__":
    main()
