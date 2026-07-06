"""
Regenerate the PLAYER_MORPHS block in gen_humanoid.py from
config/characters/sliders.json.

Reads every slider whose applies_to is morph_target / morph_pair /
morph_pair_mirrored, collects the unique set of morph names it
references, resolves each name to its MPFB2 target-file path, and
substitutes the resulting list into gen_humanoid.py between the
auto-generated markers.

Run this after editing sliders.json (adding sliders, moving sliders
between tiers, dropping sliders), then rebake humanoid.glb so the
new morph targets exist in the runtime asset.

Usage:
    python3 games/selva-oscura/scripts/blender/regen_player_morphs.py

Environment:
    MPFB_TARGETS_DIR -- override MPFB2 install path if the addon
    isn't in the default location. Defaults to Blender 4.2's
    extensions/user_default/mpfb/data/targets on Windows.
"""

import json
import os
import sys
from pathlib import Path

SELVA = Path(__file__).resolve().parents[2]  # <repo>/games/selva-oscura
SLIDERS = SELVA / "config/characters/sliders.json"
GEN = SELVA / "scripts/blender/gen_humanoid.py"

DEFAULT_MPFB = os.path.expandvars(
    r"%APPDATA%\Blender Foundation\Blender\4.2\extensions\user_default\mpfb\data\targets"
)
MPFB = os.environ.get("MPFB_TARGETS_DIR", DEFAULT_MPFB)


def collect_morph_names(sliders_path: Path) -> set[str]:
    data = json.loads(sliders_path.read_text(encoding="utf-8"))
    morphs: set[str] = set()
    for s in data["sliders"]:
        applies = s.get("applies_to", "")
        if applies not in ("morph_target", "morph_pair", "morph_pair_mirrored"):
            continue
        if s.get("param"):
            morphs.add(s["param"])
        for key in ("param_decr", "param_incr"):
            if s.get(key):
                for p in s[key].split(","):
                    p = p.strip()
                    if p:
                        morphs.add(p)
    return morphs


def resolve_paths(morphs: set[str], mpfb_root: str) -> tuple[list[tuple[str, str]], list[str]]:
    """Returns (resolved, missing) where resolved = sorted list of
    (name, rel_path) and missing = list of morph names not found."""
    name_to_path: dict[str, str] = {}
    for root, _, files in os.walk(mpfb_root):
        for f in files:
            if not f.endswith(".target.gz"):
                continue
            name = f[: -len(".target.gz")]
            rel = os.path.relpath(os.path.join(root, f), mpfb_root).replace(os.sep, "/")
            name_to_path[name] = "targets/" + rel
    missing = sorted(m for m in morphs if m not in name_to_path)
    resolved = sorted(
        ((m, name_to_path[m]) for m in morphs if m in name_to_path),
        key=lambda t: t[0],
    )
    return resolved, missing


def build_block(resolved: list[tuple[str, str]]) -> str:
    lines = [
        "    # Auto-generated from config/characters/sliders.json by",
        "    # scripts/blender/regen_player_morphs.py -- do NOT hand-edit.",
        "    # To add a morph: append a slider entry to sliders.json,",
        "    # re-run the regen script, then rebake the humanoid.",
        "    PLAYER_MORPHS = [",
        "        # (slider_id_in_glb, mpfb_target_path_relative_to_data_targets)",
    ]
    for name, rel in resolved:
        lines.append(f'        ("{name}", "{rel}"),')
    lines.append("    ]")
    return "\n".join(lines) + "\n"


def substitute(gen_path: Path, new_block: str) -> None:
    src = gen_path.read_text(encoding="utf-8")
    start_marker = "    PLAYER_MORPHS = [\n"
    end_close = "\n    ]\n"
    start = src.find(start_marker)
    if start < 0:
        raise RuntimeError(f"start marker not found in {gen_path}")
    # Rewind to the leading "    # Auto-generated..." comment if present so
    # subsequent regens don't double-stack comments.
    rewind = start
    for _ in range(6):
        prev_newline = src.rfind("\n", 0, rewind - 1)
        if prev_newline < 0:
            break
        line = src[prev_newline + 1 : rewind]
        if line.startswith("    # Auto-generated") or line.startswith(
            "    # scripts/blender/regen_player_morphs"
        ) or line.startswith("    # To add a morph") or line.startswith(
            "    # re-run the regen script"
        ):
            rewind = prev_newline + 1
        else:
            break
    end = src.find(end_close, start)
    if end < 0:
        raise RuntimeError(f"end marker not found in {gen_path}")
    before = src[:rewind]
    after = src[end + len(end_close):]
    gen_path.write_text(before + new_block + after, encoding="utf-8")


def main() -> int:
    if not os.path.isdir(MPFB):
        print(f"[regen] MPFB2 targets dir not found: {MPFB}", file=sys.stderr)
        print("[regen] set MPFB_TARGETS_DIR env var to override", file=sys.stderr)
        return 1
    morphs = collect_morph_names(SLIDERS)
    resolved, missing = resolve_paths(morphs, MPFB)
    if missing:
        print(
            f"[regen] {len(missing)} morphs referenced in sliders.json not found in MPFB2:",
            file=sys.stderr,
        )
        for m in missing:
            print(f"  {m}", file=sys.stderr)
        return 1
    block = build_block(resolved)
    substitute(GEN, block)
    print(f"[regen] wrote {len(resolved)} PLAYER_MORPHS entries into {GEN.name}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
