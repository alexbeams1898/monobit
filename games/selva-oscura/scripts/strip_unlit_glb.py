#!/usr/bin/env python3
"""Strip KHR_materials_unlit extension from .glb files.

Kenney's Nature Kit (and other UniGLTF / Unity-exported asset packs)
ship .glb files with the `KHR_materials_unlit` extension declared.
Our static-mesh loader (cgltf) rejects these as cgltf_result_invalid_gltf
even though the geometry is perfectly valid -- the unlit extension is
a shading-only concern that we don't use.

This script rewrites a .glb in place: removes `extensionsUsed`,
`extensionsRequired`, and `extensions` fields from materials. The
binary geometry chunk is untouched; only the JSON chunk header and
the file total-length get patched.

Usage:
  python strip_unlit_glb.py <file.glb> [<file2.glb> ...]
  python strip_unlit_glb.py games/selva-oscura/assets/world/materials/*/*.glb

Idempotent -- running twice on the same file is a no-op.
"""

import json
import struct
import sys
from pathlib import Path


def strip_unlit(path: Path) -> bool:
    raw = path.read_bytes()
    if raw[:4] != b'glTF':
        print(f'{path}: not a glTF binary; skipping', file=sys.stderr)
        return False
    version = struct.unpack('<I', raw[4:8])[0]
    total_len = struct.unpack('<I', raw[8:12])[0]
    json_chunk_len = struct.unpack('<I', raw[12:16])[0]
    json_chunk_type = raw[16:20]
    if json_chunk_type != b'JSON':
        print(f'{path}: first chunk is not JSON; skipping', file=sys.stderr)
        return False
    json_data = raw[20:20 + json_chunk_len]
    rest = raw[20 + json_chunk_len:]

    parsed = json.loads(json_data.decode('utf-8').rstrip())
    touched = False
    if 'extensionsUsed' in parsed:
        parsed.pop('extensionsUsed')
        touched = True
    if 'extensionsRequired' in parsed:
        parsed.pop('extensionsRequired')
        touched = True
    for mat in parsed.get('materials', []):
        if 'extensions' in mat:
            mat.pop('extensions')
            touched = True

    if not touched:
        print(f'{path}: no extensions to strip; unchanged')
        return False

    new_json = json.dumps(parsed, separators=(',', ':')).encode('utf-8')
    pad = (4 - len(new_json) % 4) % 4
    new_json += b' ' * pad
    new_chunk_len = len(new_json)
    delta = new_chunk_len - json_chunk_len
    new_total = total_len + delta

    out = bytearray()
    out += b'glTF'
    out += struct.pack('<I', version)
    out += struct.pack('<I', new_total)
    out += struct.pack('<I', new_chunk_len)
    out += b'JSON'
    out += new_json
    out += rest

    path.write_bytes(bytes(out))
    print(f'{path}: stripped (json delta={delta:+d}, total {len(raw)} -> {len(out)})')
    return True


def main(argv):
    if len(argv) < 2:
        print('Usage: strip_unlit_glb.py <file.glb> [...]', file=sys.stderr)
        return 1
    for arg in argv[1:]:
        strip_unlit(Path(arg))
    return 0


if __name__ == '__main__':
    sys.exit(main(sys.argv))
