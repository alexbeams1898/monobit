"""Normalize a glb's coordinate system from cm-with-100x-scale to
meters-with-unit-scale before gltf2ozz consumes it.

Background: when an FBX is uploaded with cm-encoded vertex data and
armature scale=1.0, Mixamo's "With Skin" download carries bone
translations in cm magnitude and applies a 100x scale on the hips +
mesh nodes. FBX2glTF passes those values verbatim into the glb.
gltf2ozz strips per-joint scale during animation extraction but
keeps the cm translations, so the runtime renders the character
at 100x.

For every node with a non-unit uniform scale:
  - new_translation = old_translation / scale  (static + every
    keyframe in animation tracks targeting that node)
  - new_scale = [1, 1, 1]

Idempotent: nodes already at unit scale are no-ops, so running this
on a previously-normalized glb does nothing.

Usage:
    python normalize_glb_scale.py --inplace path/to/file.glb
"""

import argparse
import json
import struct
import sys


def read_glb(path):
    with open(path, "rb") as f:
        magic, version, total = struct.unpack("<III", f.read(12))
        if magic != 0x46546C67:
            raise SystemExit(f"[normalize_glb] not a glb: {path} (bad magic)")
        chunks = []
        while f.tell() < total:
            chunk_len, chunk_type = struct.unpack("<II", f.read(8))
            chunk_data = f.read(chunk_len)
            chunks.append((chunk_type, chunk_data))
        return version, chunks


def write_glb(path, version, chunks):
    body = b""
    for chunk_type, chunk_data in chunks:
        body += struct.pack("<II", len(chunk_data), chunk_type) + chunk_data
    header = struct.pack("<III", 0x46546C67, version, 12 + len(body))
    with open(path, "wb") as f:
        f.write(header + body)


def collect_node_scales(nodes):
    """Return {node_index: uniform_scale_factor} for nodes whose
    scale is uniform-non-unit. Unit-scale + non-uniform skipped.
    """
    out = {}
    for i, n in enumerate(nodes):
        scale = n.get("scale")
        if scale is None or scale == [1.0, 1.0, 1.0]:
            continue
        sx, sy, sz = scale
        if abs(sx - sy) > 1e-6 or abs(sy - sz) > 1e-6:
            sys.stderr.write(
                f"[normalize_glb] warning: non-uniform scale on node "
                f"'{n.get('name', '?')}' (index {i}): {scale} -- leaving alone\n")
            continue
        out[i] = sx
    return out


def normalize_node_static_transforms(nodes, scales):
    """Bake each scale into its node's static translation; set scale
    back to identity.
    """
    for idx, s in scales.items():
        n = nodes[idx]
        n["translation"] = [t / s for t in n.get("translation", [0, 0, 0])]
        n["scale"] = [1.0, 1.0, 1.0]


def find_translation_track_accessors(gltf, scales):
    """Return [(accessor_index, divisor)] for every animation
    translation track targeting a scaled node.
    """
    out = []
    for anim in gltf.get("animations", []):
        samplers = anim["samplers"]
        for ch in anim["channels"]:
            target_node = ch["target"]["node"]
            if target_node not in scales:
                continue
            if ch["target"]["path"] != "translation":
                continue
            sampler = samplers[ch["sampler"]]
            out.append((sampler["output"], scales[target_node]))
    return out


def scale_translation_accessor(gltf, bin_buf, accessor_idx, factor):
    ac = gltf["accessors"][accessor_idx]
    if ac["type"] != "VEC3":
        raise SystemExit(
            f"[normalize_glb] translation accessor {accessor_idx} is type "
            f"'{ac['type']}' not VEC3 -- glb is malformed")
    if ac["componentType"] != 5126:  # FLOAT
        raise SystemExit(
            f"[normalize_glb] translation accessor {accessor_idx} is "
            f"componentType {ac['componentType']} not FLOAT (5126) -- "
            f"glb is malformed")
    bv = gltf["bufferViews"][ac["bufferView"]]
    offset = bv.get("byteOffset", 0) + ac.get("byteOffset", 0)
    count = ac["count"]
    n_floats = count * 3
    fmt = f"<{n_floats}f"
    end = offset + 4 * n_floats
    values = list(struct.unpack(fmt, bin_buf[offset:end]))
    scaled = [v / factor for v in values]
    bin_buf[offset:end] = struct.pack(fmt, *scaled)


def normalize_glb_file(in_path: str, out_path: str) -> int:
    version, chunks = read_glb(in_path)
    json_idx = None
    bin_idx = None
    for i, (ct, _) in enumerate(chunks):
        if ct == 0x4E4F534A and json_idx is None:  # 'JSON'
            json_idx = i
        elif ct == 0x004E4942 and bin_idx is None:  # 'BIN\0'
            bin_idx = i
    if json_idx is None:
        raise SystemExit("[normalize_glb] glb has no JSON chunk")
    if bin_idx is None:
        raise SystemExit("[normalize_glb] glb has no BIN chunk")

    json_payload = chunks[json_idx][1].rstrip(b"\x00 ").decode("utf-8")
    gltf = json.loads(json_payload)
    bin_buf = bytearray(chunks[bin_idx][1])

    scales = collect_node_scales(gltf.get("nodes", []))
    if not scales:
        # Idempotent / not-a-Mixamo-cm download: no-op pass-through.
        if in_path != out_path:
            write_glb(out_path, version, chunks)
        return 0

    track_targets = find_translation_track_accessors(gltf, scales)
    seen_accessors = set()
    for ac_idx, factor in track_targets:
        if ac_idx in seen_accessors:
            continue
        scale_translation_accessor(gltf, bin_buf, ac_idx, factor)
        seen_accessors.add(ac_idx)

    # Bake the scale into the static translation + reset scale to unit
    # so the resulting glb is internally consistent (no scale anywhere,
    # everything in meters). This is REQUIRED for the rig glb -- without
    # it, gltf2ozz extracts skeleton.ozz with the scale baked into the
    # joint rest pose, which then propagates to every bone palette at
    # runtime (100x giant character).
    #
    # IMPORTANT: this must be applied to BOTH the rig glb AND every
    # clip glb. If only one side gets normalized, the bind pose
    # mismatches between clip and skeleton, and gltf2ozz collapses
    # tracks toward identity (stringy-fingers/contorted-arms symptom).
    for idx, s in scales.items():
        n = gltf["nodes"][idx]
        n["translation"] = [t / s for t in n.get("translation", [0, 0, 0])]
        n["scale"] = [1.0, 1.0, 1.0]

    new_json = json.dumps(gltf, separators=(",", ":")).encode("utf-8")
    pad = (4 - (len(new_json) % 4)) % 4
    new_json = new_json + b" " * pad
    new_chunks = list(chunks)
    new_chunks[json_idx] = (0x4E4F534A, new_json)
    new_chunks[bin_idx] = (0x004E4942, bytes(bin_buf))

    write_glb(out_path, version, new_chunks)
    sys.stderr.write(
        f"[normalize_glb] nodes normalized: {len(scales)}, "
        f"animation accessors scaled: {len(seen_accessors)}\n")
    return len(scales)


def main():
    p = argparse.ArgumentParser()
    src = p.add_mutually_exclusive_group(required=True)
    src.add_argument("--in", dest="input", help="input glb")
    src.add_argument("--inplace", help="in-place glb to normalize")
    p.add_argument("--out", dest="output", help="output glb (required with --in)")
    args = p.parse_args()
    if args.inplace:
        in_path = out_path = args.inplace
    else:
        if not args.output:
            p.error("--out required when using --in")
        in_path = args.input
        out_path = args.output
    normalize_glb_file(in_path, out_path)


if __name__ == "__main__":
    main()
