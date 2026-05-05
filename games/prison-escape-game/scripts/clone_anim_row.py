"""
Clone an animation row to create a new independent row in the spritesheet.

This script:
1. Adds a new STATE_ROW entry to assemble_spritesheet.py's config
2. Adds a new animation state to lpc_humanoid.json
3. Copies hand anchors from the source row to the new row
4. Updates NUM_STATES and FALLBACK_H

Usage:
  python scripts/clone_anim_row.py <source_name> <new_name>

Example:
  python scripts/clone_anim_row.py thrust rifle_shoot
"""

import argparse
import json
import os
import re
import sys

SCRIPT_DIR = os.path.dirname(os.path.abspath(__file__))
GAME_DIR = os.path.dirname(SCRIPT_DIR)
ANIM_CONFIG = os.path.join(GAME_DIR, "config", "animations", "lpc_humanoid.json")
ASSEMBLE_SCRIPT = os.path.join(SCRIPT_DIR, "assemble_spritesheet.py")
COMPOSITOR = os.path.join(os.path.dirname(GAME_DIR), "engine", "src", "SpriteCompositor.cpp")


def main():
    parser = argparse.ArgumentParser(description="Clone an animation row.")
    parser.add_argument("source", help="Name of the source animation (e.g. 'thrust')")
    parser.add_argument("new_name", help="Name for the new animation (e.g. 'rifle_shoot')")
    args = parser.parse_args()

    # --- 1. Read lpc_humanoid.json ---
    with open(ANIM_CONFIG) as f:
        anim = json.load(f)

    states = anim["states"]
    if args.source not in states:
        print(f"Error: source '{args.source}' not found in states: {list(states.keys())}")
        sys.exit(1)
    if args.new_name in states:
        print(f"Error: '{args.new_name}' already exists in states")
        sys.exit(1)

    source_state = states[args.source]
    source_row = source_state["row"]

    # Find the next available row index.
    max_row = max(s["row"] for s in states.values())
    new_row = max_row + 1

    # Add the new state.
    states[args.new_name] = {
        "row": new_row,
        "frames": source_state["frames"],
        "duration": source_state["duration"],
    }

    # Copy hand anchors if they exist for the source row.
    anchors = anim.get("hand_anchors", {}).get("rows", {})
    source_row_str = str(source_row)
    if source_row_str in anchors:
        import copy
        anchors[str(new_row)] = copy.deepcopy(anchors[source_row_str])
        anchors[str(new_row)]["_comment"] = f"{args.new_name} ({source_state['frames']} frames) — cloned from {args.source}"

    with open(ANIM_CONFIG, "w") as f:
        json.dump(anim, f, indent=4)
    print(f"Added '{args.new_name}' as row {new_row} in {ANIM_CONFIG}")

    # --- 2. Update assemble_spritesheet.py ---
    with open(ASSEMBLE_SCRIPT) as f:
        script = f.read()

    # Update NUM_STATES.
    new_num = new_row + 1
    script = re.sub(r"NUM_STATES = \d+", f"NUM_STATES = {new_num}", script)

    # Add the new STATE_ROW entry after the source row.
    source_anim_key = args.source
    new_entry = f'    ({new_row}, "{source_anim_key}",{" " * max(1, 13 - len(source_anim_key))}{source_state["frames"]},  False, 0),  # {args.new_name} (cloned from {args.source})\n'

    # Find the source row line and insert after it.
    lines = script.split("\n")
    for i, line in enumerate(lines):
        if f'# {args.source}' in line.lower() or (f'"{source_anim_key}"' in line and "STATE_ROWS" not in line):
            lines.insert(i + 1, new_entry.rstrip())
            break

    script = "\n".join(lines)

    with open(ASSEMBLE_SCRIPT, "w") as f:
        f.write(script)
    print(f"Updated {ASSEMBLE_SCRIPT}: NUM_STATES={new_num}, added row {new_row}")

    # --- 3. Update SpriteCompositor fallback height ---
    with open(COMPOSITOR) as f:
        comp = f.read()

    old_h = 576  # original
    new_h = new_num * 64
    if f"FALLBACK_H = {old_h}" in comp:
        comp = comp.replace(f"FALLBACK_H = {old_h}", f"FALLBACK_H = {new_h}")
    else:
        # Try to find whatever the current value is.
        comp = re.sub(r"FALLBACK_H = \d+", f"FALLBACK_H = {new_h}", comp)

    with open(COMPOSITOR, "w") as f:
        f.write(comp)
    print(f"Updated {COMPOSITOR}: FALLBACK_H={new_h}")

    print(f"\nDone. Run 'python scripts/assemble_spritesheet.py --force' to rebuild sheets.")
    print(f"Then update your weapon JSON: \"attack_anim\": \"{args.new_name}\"")


if __name__ == "__main__":
    main()
