"""Find the .ozz file gltf2ozz produced in a temp dir and copy it to
the final clip output path.

gltf2ozz names its output by the animation's internal name (e.g.
'mixamo.com.ozz' for Mixamo FBXs, 'Scene.ozz' for Blender-re-exported
FBXs). The bake pipeline used to hard-code 'mixamo.com.ozz' which
broke for Blender-re-exported clips. This script globs for any .ozz
in the temp dir and copies it — names aren't load-bearing in the
build pipeline.

Usage:
    python stage_baked_ozz.py --temp-dir <dir> --out <final.ozz>
"""

import argparse
import glob
import shutil
import sys
import os


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--temp-dir", required=True)
    ap.add_argument("--out", required=True)
    args = ap.parse_args()

    candidates = sorted(glob.glob(os.path.join(args.temp_dir, "*.ozz")))
    if not candidates:
        print(f"[stage-baked-ozz] no .ozz found in {args.temp_dir}", file=sys.stderr)
        return 1
    if len(candidates) > 1:
        print(
            f"[stage-baked-ozz] WARN: multiple .ozz in {args.temp_dir}: {candidates}; "
            f"taking first: {candidates[0]}",
            file=sys.stderr,
        )
    os.makedirs(os.path.dirname(args.out), exist_ok=True)
    shutil.copyfile(candidates[0], args.out)
    return 0


if __name__ == "__main__":
    sys.exit(main())
