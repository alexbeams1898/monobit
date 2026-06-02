"""Dump skeleton + animation + scene metadata for a third-party asset
(FBX / glTF / GLB).

Run via Blender CLI:
    "/c/Program Files/Blender Foundation/Blender 5.1/blender.exe" \
        --background --python audit_skeleton_asset.py -- --src <path>

Used as the FIRST step when bringing in any new Sketchfab / Quaternius /
Mixamo asset. Tells you what the joint names actually are (Mixamo
'mixamorig:Hips' vs custom 'Rig:bone_01' vs generic 'Bone.001'), what
the unit scale is (m vs cm), what the forward axis is, and what
animations ship. Without this you're guessing at the joint map, which
is the textbook way to ship a sampler that crashes on bone-name
lookup with no warning.

Output goes to stderr (so it's visible in the IDE pane when run from
the build target) and is also written to <src>.audit.json next to the
source file for diffing later.

Reusable across every skeleton -- the bug surface is per-asset, but
the diagnostic shape is identical.
"""

import argparse
import json
import os
import sys
from pathlib import Path

import bpy  # type: ignore


def parse_args() -> argparse.Namespace:
    # Blender passes its own args before --; strip them out.
    argv = sys.argv[sys.argv.index("--") + 1 :] if "--" in sys.argv else sys.argv[1:]
    p = argparse.ArgumentParser()
    p.add_argument("--src", required=True, help="Path to FBX / glTF / GLB")
    return p.parse_args(argv)


def clear_scene() -> None:
    bpy.ops.wm.read_factory_settings(use_empty=True)


def import_asset(src: str) -> None:
    ext = Path(src).suffix.lower()
    if ext == ".fbx":
        bpy.ops.import_scene.fbx(filepath=src)
    elif ext in {".gltf", ".glb"}:
        bpy.ops.import_scene.gltf(filepath=src)
    else:
        raise SystemExit(f"Unknown asset format: {ext}")


def collect_audit(src: str) -> dict:
    scene = bpy.context.scene

    armatures = [o for o in bpy.data.objects if o.type == "ARMATURE"]
    meshes = [o for o in bpy.data.objects if o.type == "MESH"]
    actions = list(bpy.data.actions)

    audit = {
        "src": src,
        "unit_system": scene.unit_settings.system,  # 'METRIC' / 'IMPERIAL' / 'NONE'
        "unit_scale_length": scene.unit_settings.scale_length,  # 1.0 = m; 0.01 = cm-scaled
        "frame_start": scene.frame_start,
        "frame_end": scene.frame_end,
        "fps": scene.render.fps,
        "armatures": [],
        "meshes": [],
        "animations": [],
    }

    for arm in armatures:
        bones = arm.data.bones
        # Sample bone names + parent links + rest-pose head/tail in
        # armature space. Enough to spot Mixamo prefix conventions,
        # quadruped vs biped topology, and unusual rest poses.
        bone_data = []
        for b in bones:
            bone_data.append(
                {
                    "name": b.name,
                    "parent": b.parent.name if b.parent else None,
                    "head_local": list(b.head_local),
                    "tail_local": list(b.tail_local),
                    "length": b.length,
                }
            )
        audit["armatures"].append(
            {
                "name": arm.name,
                "location": list(arm.location),
                "rotation_euler": list(arm.rotation_euler),
                "scale": list(arm.scale),
                "bone_count": len(bones),
                "bones": bone_data,
            }
        )

    for m in meshes:
        # Vertex/poly counts + bounding box let us spot scale issues
        # (Quaternius is m-scaled, Sketchfab uploads often cm-scaled).
        bbox = [list(corner) for corner in m.bound_box]
        audit["meshes"].append(
            {
                "name": m.name,
                "vertex_count": len(m.data.vertices),
                "polygon_count": len(m.data.polygons),
                "bbox_local": bbox,
                "scale": list(m.scale),
                "location": list(m.location),
                "materials": [s.material.name if s.material else None for s in m.material_slots],
            }
        )

    for a in actions:
        frame_range = a.frame_range
        # Blender 5.1+ moved fcurves under Action.slots[*].channelbags[*].
        # Fall back to len(a.fcurves) on older Blenders if the attr exists.
        fcurve_count = 0
        if hasattr(a, "slots"):
            for slot in a.slots:
                for layer in a.layers:
                    for strip in layer.strips:
                        if hasattr(strip, "channelbag"):
                            cb = strip.channelbag(slot)
                            if cb is not None:
                                fcurve_count += len(cb.fcurves)
        elif hasattr(a, "fcurves"):
            fcurve_count = len(a.fcurves)
        audit["animations"].append(
            {
                "name": a.name,
                "frame_start": frame_range[0],
                "frame_end": frame_range[1],
                "duration_frames": frame_range[1] - frame_range[0],
                "fcurve_count": fcurve_count,
            }
        )

    return audit


def print_summary(audit: dict) -> None:
    out = sys.stderr.write

    out(f"\n[audit] === {audit['src']} ===\n")
    out(f"[audit] unit_system={audit['unit_system']} scale={audit['unit_scale_length']:.4f}\n")
    out(f"[audit] frame_range=[{audit['frame_start']}..{audit['frame_end']}] fps={audit['fps']}\n\n")

    for arm in audit["armatures"]:
        out(f"[audit] ARMATURE '{arm['name']}' -- {arm['bone_count']} bones\n")
        out(f"[audit]   location={[round(v,3) for v in arm['location']]}\n")
        out(f"[audit]   rotation_euler={[round(v,3) for v in arm['rotation_euler']]}\n")
        out(f"[audit]   scale={[round(v,3) for v in arm['scale']]}\n")
        out(f"[audit]   bones:\n")
        for b in arm["bones"]:
            parent = b["parent"] or "(root)"
            out(f"[audit]     {b['name']:<40s} parent={parent:<30s} len={b['length']:.3f}\n")
        out("\n")

    for m in audit["meshes"]:
        out(f"[audit] MESH '{m['name']}' -- {m['vertex_count']}v / {m['polygon_count']}p\n")
        mins = [min(c[i] for c in m["bbox_local"]) for i in range(3)]
        maxs = [max(c[i] for c in m["bbox_local"]) for i in range(3)]
        size = [maxs[i] - mins[i] for i in range(3)]
        out(
            f"[audit]   bbox_local size=({size[0]:.3f}, {size[1]:.3f}, {size[2]:.3f})"
            f" mins=({mins[0]:.3f}, {mins[1]:.3f}, {mins[2]:.3f})\n"
        )
        out(f"[audit]   scale={[round(v,3) for v in m['scale']]}\n")
        out(f"[audit]   materials={m['materials']}\n\n")

    out(f"[audit] {len(audit['animations'])} animation(s):\n")
    for a in audit["animations"]:
        out(
            f"[audit]   {a['name']:<40s} frames=[{a['frame_start']:.0f}..{a['frame_end']:.0f}]"
            f" dur={a['duration_frames']:.0f}f fcurves={a['fcurve_count']}\n"
        )
    out("\n")


def main() -> None:
    args = parse_args()
    src = os.path.abspath(args.src)
    if not os.path.isfile(src):
        raise SystemExit(f"source not found: {src}")

    clear_scene()
    import_asset(src)
    audit = collect_audit(src)
    print_summary(audit)

    # Persist beside source for diffing later (e.g. when re-downloading
    # an updated version, or comparing wolf vs leopard rig topology).
    out_path = src + ".audit.json"
    with open(out_path, "w", encoding="utf-8") as f:
        json.dump(audit, f, indent=2)
    sys.stderr.write(f"[audit] wrote {out_path}\n")


if __name__ == "__main__":
    main()
