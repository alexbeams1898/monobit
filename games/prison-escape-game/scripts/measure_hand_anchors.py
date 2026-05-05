"""
Measure hand anchor positions from LPC body spritesheets.

Opens the assembled body spritesheet and displays frames one at a time.
For each frame, click on the hand currently being measured to record its
pixel offset from the frame center (32, 32).

Supports two hands: 'left' is the player's left hand (trigger hand for 1H
weapons); 'right' is the player's right hand (support hand for two-handed
weapons). When measuring one hand, the other hand's existing anchor (if any)
is shown as a faded marker so you can visually verify it and aim relative
to it.

Outputs JSON in the format expected by
games/prison-escape-game/config/animations/lpc_humanoid.json `hand_anchors.rows`.
Each direction cell is stored as:
    "S": { "left": [[x,y],...], "right": [[x,y],...] }
The --in-place merge preserves whichever hand you did NOT measure.

Usage:
  # Measure only the right hand for the walk row (default --hand right)
  python scripts/measure_hand_anchors.py --row 1 --in-place

  # Re-measure left hand for the walk row
  python scripts/measure_hand_anchors.py --row 1 --hand left --in-place

  # Measure both hands in one pass (left click first, then right)
  python scripts/measure_hand_anchors.py --row 1 --hand both --in-place

Sheet layout follows our engine format:
  Rows    = states: idle(0), walk(1), slash(2), hit(3), death(4), run(5),
            thrust(6), shoot(7), reverse_slash(8)
  Columns = direction blocks, each 13 frames wide
  Dir order: South(0), West(1), East(2), North(3)
"""

import argparse
import json
import os
import re
import sys

try:
    from PIL import Image, ImageDraw, ImageTk
    import tkinter as tk
except ImportError as e:
    print(f"ERROR: Missing dependency: {e}")
    print("Install with: pip install Pillow")
    sys.exit(1)

FRAME_SIZE = 64
MAX_FRAMES_PER_DIR = 13
CENTER_X = FRAME_SIZE // 2  # 32
CENTER_Y = FRAME_SIZE // 2  # 32

DIR_NAMES = ["S", "W", "E", "N"]

# Human-readable direction labels for the UI.
DIR_LABELS = {
    0: "South (facing viewer)",
    1: "West (walking left)",
    2: "East (walking right)",
    3: "North (facing away)",
}

# Human-readable per-direction hand hints, keyed by (dir_idx, hand_name).
HAND_HINTS = {
    (0, "left"):  "Both hands visible. Click player's LEFT hand (viewer's RIGHT side).",
    (0, "right"): "Both hands visible. Click player's RIGHT hand (viewer's LEFT side).",
    (1, "left"):  "Player's LEFT hand is near side. Click it.",
    (1, "right"): "Player's RIGHT hand is on the far side of the body. Click where it would be.",
    (2, "left"):  "Player's LEFT hand is behind the body. Click where it would be.",
    (2, "right"): "Player's RIGHT hand is near side. Click it.",
    (3, "left"):  "Facing away: player's LEFT hand is on viewer's LEFT. Click it.",
    (3, "right"): "Facing away: player's RIGHT hand is on viewer's RIGHT. Click it.",
}

ROW_NAMES = {
    0: "idle",
    1: "walk",
    2: "slash",
    3: "hit",
    4: "death",
    5: "run",
    6: "thrust",
    7: "shoot",
    8: "reverse_slash",
}

def load_row_frame_counts(anim_config_path):
    """Read frame counts per row from lpc_humanoid.json states."""
    counts = {}
    try:
        with open(anim_config_path, "r", encoding="utf-8") as f:
            data = json.load(f)
        for state in data.get("states", {}).values():
            counts[state["row"]] = state["frames"]
    except (FileNotFoundError, json.JSONDecodeError, KeyError):
        pass
    return counts


# Loaded later from the animation config; fallback for legacy use.
ROW_FRAME_COUNTS = {}

SCRIPT_DIR = os.path.dirname(os.path.abspath(__file__))
GAME_DIR = os.path.dirname(SCRIPT_DIR)
DEFAULT_SHEET = os.path.join(
    GAME_DIR, "assets", "sprites", "lpc", "assembled", "body", "tone_1.png"
)

# Marker color for confirmed clicks.
MARKER_COLOR = "#00FF00"
# Marker color for pending (unconfirmed) clicks.
PENDING_COLOR = "#FFFF00"


def extract_frame(sheet, row, dir_col, frame_idx):
    """Extract a single 64x64 frame from the assembled sheet."""
    col = dir_col * MAX_FRAMES_PER_DIR + frame_idx
    x = col * FRAME_SIZE
    y = row * FRAME_SIZE
    return sheet.crop((x, y, x + FRAME_SIZE, y + FRAME_SIZE))


class AnchorPicker:
    def __init__(self, sheet, rows_to_measure, dirs_to_measure,
                 hands_to_measure, existing_anchors):
        """
        hands_to_measure: list of "left" / "right" strings, in click order.
            ["left"]          -> one click per frame, measures left
            ["right"]         -> one click per frame, measures right
            ["left", "right"] -> two clicks per frame (left first, then right)
        existing_anchors: dict from load_existing_anchors() used to display
            the OTHER hand as a faded reference marker. Lets you eyeball-check
            that the hand you already measured is still where you think it is.
        """
        self.sheet = sheet
        self.rows = rows_to_measure
        self.dirs = dirs_to_measure
        self.hands = hands_to_measure
        self.existing = existing_anchors
        self.results = {}
        self.scale = 8  # display magnification

        # Queue entry = (row, dir_idx, frame, hand_name).
        self.queue = []
        for row in self.rows:
            frame_count = ROW_FRAME_COUNTS.get(row, 1)
            for dir_idx in self.dirs:
                for frame in range(frame_count):
                    for hand in self.hands:
                        self.queue.append((row, dir_idx, frame, hand))

        self.current_idx = 0
        self.anchors = []  # entries: (row, dir_idx, frame, hand, x, y)

        # Pending click: None or (anchor_x, anchor_y, canvas_x, canvas_y).
        self.pending_click = None

        self.root = tk.Tk()
        self.root.title("Hand Anchor Measurement")

        # Top instruction bar.
        self.instruction = tk.Label(
            self.root,
            text="Click the CENTER OF THE GRIP on the player's LEFT hand.",
            font=("Consolas", 12, "bold"),
            fg="#CC0000",
            wraplength=600,
        )
        self.instruction.pack(pady=(8, 0))

        canvas_size = FRAME_SIZE * self.scale
        self.canvas = tk.Canvas(
            self.root, width=canvas_size, height=canvas_size, bg="black"
        )
        self.canvas.pack(pady=8)

        # Status label: which frame we're on.
        self.status_label = tk.Label(self.root, text="", font=("Consolas", 13))
        self.status_label.pack()

        # Hand hint label: which hand to click for this direction.
        self.hint_label = tk.Label(
            self.root, text="", font=("Consolas", 11), fg="#0066AA", wraplength=600
        )
        self.hint_label.pack(pady=(0, 4))

        # Button bar.
        btn_frame = tk.Frame(self.root)
        btn_frame.pack(pady=8)

        self.confirm_btn = tk.Button(
            btn_frame,
            text="Confirm (Enter)",
            font=("Consolas", 12),
            command=self.on_confirm,
            state="disabled",
            bg="#88CC88",
        )
        self.confirm_btn.pack(side="left", padx=4)

        self.undo_btn = tk.Button(
            btn_frame,
            text="Undo last (U)",
            font=("Consolas", 12),
            command=self.on_undo,
        )
        self.undo_btn.pack(side="left", padx=4)

        self.skip_btn = tk.Button(
            btn_frame,
            text="Skip frame (S)",
            font=("Consolas", 12),
            command=self.on_skip,
        )
        self.skip_btn.pack(side="left", padx=4)

        self.quit_btn = tk.Button(
            btn_frame,
            text="Quit (Esc)",
            font=("Consolas", 12),
            command=self.on_quit,
        )
        self.quit_btn.pack(side="left", padx=4)

        # Coordinate display.
        self.coord_label = tk.Label(
            self.root, text="", font=("Consolas", 11), fg="#666666"
        )
        self.coord_label.pack(pady=(0, 8))

        # Key bindings.
        self.canvas.bind("<Button-1>", self.on_click)
        self.root.bind("<Return>", lambda e: self.on_confirm())
        self.root.bind("<u>", lambda e: self.on_undo())
        self.root.bind("<s>", lambda e: self.on_skip())
        self.root.bind("<Escape>", lambda e: self.on_quit())

        self.show_current()

    def _reference_anchor(self, row, dir_idx, frame, other_hand):
        """Return (x, y) for the other hand on this frame, or None if we
        don't have a measurement to display."""
        cell = self.existing.get(row, {}).get(dir_idx)
        if cell is None:
            return None
        arr = cell.get(other_hand, [])
        if frame >= len(arr):
            return None
        entry = arr[frame]
        return (float(entry[0]), float(entry[1]))

    def _draw_reference(self, draw, ref):
        """Draw a faded reference marker for the other hand's existing anchor.
        Helps you aim the click for the hand you're currently measuring."""
        if ref is None:
            return
        rx = (ref[0] + CENTER_X) * self.scale
        ry = (ref[1] + CENTER_Y) * self.scale
        size = 8
        # Faded cyan X marker.
        draw.line([(rx - size, ry - size), (rx + size, ry + size)],
                  fill="#44CCFF", width=1)
        draw.line([(rx - size, ry + size), (rx + size, ry - size)],
                  fill="#44CCFF", width=1)

    def show_current(self):
        if self.current_idx >= len(self.queue):
            self.finish()
            return

        self.pending_click = None
        self.confirm_btn.config(state="disabled")

        row, dir_idx, frame, hand = self.queue[self.current_idx]
        other_hand = "right" if hand == "left" else "left"
        reference = self._reference_anchor(row, dir_idx, frame, other_hand)

        frame_img = extract_frame(self.sheet, row, dir_idx, frame)

        # Scale up for visibility.
        display = frame_img.resize(
            (FRAME_SIZE * self.scale, FRAME_SIZE * self.scale), Image.NEAREST
        )

        # Draw crosshair at center.
        draw = ImageDraw.Draw(display)
        cx = CENTER_X * self.scale
        cy = CENTER_Y * self.scale
        full = FRAME_SIZE * self.scale
        draw.line([(cx, 0), (cx, full)], fill="#FF000066", width=1)
        draw.line([(0, cy), (full, cy)], fill="#FF000066", width=1)

        self._draw_reference(draw, reference)

        self.photo = ImageTk.PhotoImage(display)
        self.canvas.delete("all")
        self.canvas.create_image(0, 0, anchor="nw", image=self.photo)

        row_name = ROW_NAMES.get(row, f"row{row}")
        dir_label = DIR_LABELS.get(dir_idx, DIR_NAMES[dir_idx])
        total = len(self.queue)
        self.status_label.config(
            text=(
                f"[{self.current_idx + 1}/{total}]  {row_name} / {dir_label} / "
                f"frame {frame}  ---  measuring {hand.upper()} hand"
            )
        )
        self.hint_label.config(text=HAND_HINTS.get((dir_idx, hand), ""))
        ref_note = " (cyan X = existing other hand)" if reference else ""
        self.coord_label.config(
            text=f"Click on the {hand.upper()} hand to place marker...{ref_note}"
        )

    def on_click(self, event):
        """Place or move the pending marker."""
        fx = event.x / self.scale
        fy = event.y / self.scale
        anchor_x = round(fx - CENTER_X, 1)
        anchor_y = round(fy - CENTER_Y, 1)

        self.pending_click = (anchor_x, anchor_y, event.x, event.y)
        self.confirm_btn.config(state="normal")

        # Redraw the frame with the pending marker.
        self.redraw_with_marker(event.x, event.y, PENDING_COLOR)
        self.coord_label.config(
            text=f"Pending: ({anchor_x:+.1f}, {anchor_y:+.1f}) -- press Enter to confirm, or click again to move"
        )

    def redraw_with_marker(self, canvas_x, canvas_y, color):
        """Redraw the current frame with a crosshair marker at the given position."""
        row, dir_idx, frame, hand = self.queue[self.current_idx]
        other_hand = "right" if hand == "left" else "left"
        reference = self._reference_anchor(row, dir_idx, frame, other_hand)

        frame_img = extract_frame(self.sheet, row, dir_idx, frame)
        display = frame_img.resize(
            (FRAME_SIZE * self.scale, FRAME_SIZE * self.scale), Image.NEAREST
        )

        draw = ImageDraw.Draw(display)
        cx = CENTER_X * self.scale
        cy = CENTER_Y * self.scale
        full = FRAME_SIZE * self.scale
        draw.line([(cx, 0), (cx, full)], fill="#FF000044", width=1)
        draw.line([(0, cy), (full, cy)], fill="#FF000044", width=1)
        self._draw_reference(draw, reference)

        # Draw marker crosshair at click position.
        marker_size = 12
        draw.line(
            [(canvas_x - marker_size, canvas_y), (canvas_x + marker_size, canvas_y)],
            fill=color,
            width=2,
        )
        draw.line(
            [(canvas_x, canvas_y - marker_size), (canvas_x, canvas_y + marker_size)],
            fill=color,
            width=2,
        )
        # Small circle at center.
        r = 4
        draw.ellipse(
            [canvas_x - r, canvas_y - r, canvas_x + r, canvas_y + r],
            outline=color,
            width=2,
        )

        self.photo = ImageTk.PhotoImage(display)
        self.canvas.delete("all")
        self.canvas.create_image(0, 0, anchor="nw", image=self.photo)

    def on_confirm(self):
        """Confirm the pending click and advance."""
        if self.pending_click is None:
            return

        anchor_x, anchor_y, cx, cy = self.pending_click
        row, dir_idx, frame, hand = self.queue[self.current_idx]
        self.anchors.append((row, dir_idx, frame, hand, anchor_x, anchor_y))

        # Flash green briefly.
        self.redraw_with_marker(cx, cy, MARKER_COLOR)
        self.coord_label.config(
            text=f"Confirmed: ({anchor_x:+.1f}, {anchor_y:+.1f})"
        )

        self.pending_click = None
        self.current_idx += 1
        # Small delay so user sees the green flash.
        self.root.after(150, self.show_current)

    def on_skip(self):
        """Skip this frame with a (0, 0) anchor."""
        self.pending_click = None
        row, dir_idx, frame, hand = self.queue[self.current_idx]
        self.anchors.append((row, dir_idx, frame, hand, 0.0, 0.0))
        self.current_idx += 1
        self.show_current()

    def on_undo(self):
        """Go back to the previous frame."""
        if self.current_idx > 0 and self.anchors:
            self.anchors.pop()
            self.current_idx -= 1
            self.show_current()

    def on_quit(self):
        self.finish()

    def finish(self):
        self.build_results()
        self.root.destroy()

    def build_results(self):
        # Organize anchors into a nested dict:
        #   results[row_idx][dir_name][hand] = [[x,y], ...]
        # Frames are appended in queue order (left-to-right, frame 0..N-1).
        for row_idx, dir_idx, frame_idx, hand, ax, ay in self.anchors:
            if row_idx not in self.results:
                self.results[row_idx] = {}
            dir_name = DIR_NAMES[dir_idx]
            if dir_name not in self.results[row_idx]:
                self.results[row_idx][dir_name] = {}
            if hand not in self.results[row_idx][dir_name]:
                self.results[row_idx][dir_name][hand] = []
            self.results[row_idx][dir_name][hand].append([ax, ay])

    def run(self):
        if not self.queue:
            print("Nothing to measure.")
            return {}
        self.root.mainloop()
        return self.results


DEFAULT_IN_PLACE_PATH = os.path.join(
    os.path.dirname(os.path.abspath(__file__)),
    "..", "config", "animations", "lpc_humanoid.json",
)


def load_existing_anchors(json_path: str) -> dict:
    """Parse lpc_humanoid.json and return:
        {row_idx: {dir_idx: {"left": [[x,y],...], "right": [[x,y],...]}}}
    Handles both legacy flat-array format (left-only) and the new object
    format. Missing keys mean 'not measured yet'. Never raises -- returns
    empty dict if the file is missing or malformed.
    """
    out: dict = {}
    if not os.path.exists(json_path):
        return out
    try:
        with open(json_path, "r", encoding="utf-8") as f:
            data = json.load(f)
    except (OSError, ValueError):
        return out
    ha = data.get("hand_anchors", {})
    rows = ha.get("rows", {})
    for row_key, row_val in rows.items():
        if row_key.startswith("_") or not row_key.isdigit():
            continue
        row_idx = int(row_key)
        out[row_idx] = {}
        for d, dir_name in enumerate(DIR_NAMES):
            cell = row_val.get(dir_name)
            if cell is None:
                continue
            if isinstance(cell, list):
                # Legacy: flat array = left-hand only.
                out[row_idx][d] = {"left": cell, "right": []}
            elif isinstance(cell, dict):
                out[row_idx][d] = {
                    "left": cell.get("left", []),
                    "right": cell.get("right", []),
                }
    return out


def format_frames_oneline(frames):
    """Format a list of [x, y] frames as a single-line JSON array, matching
    the hand-written style in lpc_humanoid.json: [[10.5,16.0],[10.6,15.6],...]."""
    parts = []
    for f in frames:
        parts.append(f"[{float(f[0]):.1f},{float(f[1]):.1f}]")
    return "[" + ",".join(parts) + "]"


def format_cell_object(left_frames, right_frames):
    """Format a direction cell as a compact object literal:
        { "left": [[x,y],...], "right": [[x,y],...] }
    If right_frames is empty, the right key is still emitted as []
    (explicit 'nothing measured yet') so future re-runs parse the same way.
    """
    return (
        "{ \"left\": " + format_frames_oneline(left_frames)
        + ", \"right\": " + format_frames_oneline(right_frames) + " }"
    )


def merge_in_place(json_path: str, results: dict,
                   measured_dirs: list, existing: dict):
    """Merge per-row, per-direction, per-hand results into lpc_humanoid.json
    by doing targeted string replacement. Preserves:
      - unmeasured rows/directions (untouched)
      - the other hand's data for measured cells (pulled from `existing`)
      - file-level comments, formatting, key order

    results[row_idx][dir_name][hand] = [[x,y],...]
    existing[row_idx][dir_idx] = {"left": [...], "right": [...]}  (from
        load_existing_anchors; used to preserve the hand NOT measured).
    """
    if not os.path.exists(json_path):
        print(f"ERROR: in-place target not found: {json_path}")
        sys.exit(1)

    with open(json_path, "r", encoding="utf-8") as f:
        text = f.read()

    updates = 0
    for row_idx, dirs in results.items():
        for dir_name in measured_dirs:
            if dir_name not in dirs:
                continue
            measured_hands = dirs[dir_name]
            dir_idx = DIR_NAMES.index(dir_name)
            existing_cell = existing.get(row_idx, {}).get(dir_idx, {})

            # Merge measured + existing: measured hand overwrites, other hand
            # is preserved from existing.
            left = measured_hands.get("left", existing_cell.get("left", []))
            right = measured_hands.get("right", existing_cell.get("right", []))
            new_cell = format_cell_object(left, right)

            row_header = f'"{row_idx}"'
            row_pos = text.find(row_header)
            if row_pos < 0:
                print(f"WARN: row {row_idx} not found in {json_path}; skipping")
                continue
            # Find the row object's braces so we only search within it.
            brace_open = text.find("{", row_pos)
            depth = 1
            brace_close = brace_open + 1
            while brace_close < len(text) and depth > 0:
                c = text[brace_close]
                if c == "{":
                    depth += 1
                elif c == "}":
                    depth -= 1
                brace_close += 1
            # Match either:
            #   "S": [[...],...]            (legacy flat array)
            #   "S": { "left": [...], ... } (new object form)
            flat = re.compile(rf'"{dir_name}"\s*:\s*\[[^\n\]]*(?:\][^\n\]]*)*\]')
            obj = re.compile(rf'"{dir_name}"\s*:\s*\{{[^}}]*\}}')
            m = obj.search(text, brace_open, brace_close)
            if m is None:
                m = flat.search(text, brace_open, brace_close)
            if m is None:
                print(f"WARN: row {row_idx} dir {dir_name} not found; skipping")
                continue
            replacement = f'"{dir_name}": {new_cell}'
            text = text[: m.start()] + replacement + text[m.end():]
            updates += 1

    if updates == 0:
        print("No cells updated.")
        return

    with open(json_path, "w", encoding="utf-8", newline="\n") as f:
        f.write(text)
    print(f"Patched {updates} cell(s) -> {json_path}")


def main():
    parser = argparse.ArgumentParser(description="Measure hand anchor positions.")
    parser.add_argument(
        "--sheet", default=DEFAULT_SHEET, help="Path to assembled body spritesheet"
    )
    parser.add_argument(
        "--row",
        type=int,
        nargs="*",
        default=[0, 1, 5],
        help="Animation rows to measure (default: 0=idle, 1=walk, 5=run)",
    )
    parser.add_argument(
        "--dir",
        nargs="*",
        default=["S", "W", "E", "N"],
        help="Directions to measure (default: all 4)",
    )
    parser.add_argument(
        "--hand",
        choices=["left", "right", "both"],
        default="right",
        help=(
            "Which hand to measure. 'right' (default) prompts for the right "
            "hand only and shows the existing left hand as a reference. "
            "'left' does the opposite. 'both' prompts for both hands per "
            "frame -- left first, then right."
        ),
    )
    parser.add_argument("--output", default=None, help="Output JSON file path")
    parser.add_argument(
        "--in-place",
        action="store_true",
        help=(
            "Merge results directly into lpc_humanoid.json, preserving all "
            "unmeasured rows/dirs/hands and hand-written formatting. Only "
            "the cells you actually measured this run get overwritten, and "
            "the hand you did NOT measure is preserved from the file."
        ),
    )
    parser.add_argument(
        "--in-place-target",
        default=DEFAULT_IN_PLACE_PATH,
        help="Override the in-place target JSON path.",
    )
    args = parser.parse_args()

    if not os.path.exists(args.sheet):
        print(f"ERROR: Sheet not found: {args.sheet}")
        sys.exit(1)

    sheet = Image.open(args.sheet).convert("RGBA")

    dir_indices = []
    measured_dir_names = []
    for d in args.dir:
        idx = DIR_NAMES.index(d.upper())
        dir_indices.append(idx)
        measured_dir_names.append(DIR_NAMES[idx])

    if args.hand == "both":
        hands = ["left", "right"]
    else:
        hands = [args.hand]

    # Load frame counts from the animation config (not hardcoded).
    global ROW_FRAME_COUNTS
    ROW_FRAME_COUNTS = load_row_frame_counts(args.in_place_target)

    # Pre-load existing anchors so the picker can show the other hand as a
    # reference marker AND so the merge can preserve unmeasured data.
    existing = load_existing_anchors(args.in_place_target)

    picker = AnchorPicker(sheet, args.row, dir_indices, hands, existing)
    results = picker.run()

    if not results:
        print("No anchors recorded.")
        return

    # Pretty-print what was measured (for logging / --output).
    pretty = {"rows": {}}
    for row_idx in sorted(results.keys()):
        row_name = ROW_NAMES.get(row_idx, f"row{row_idx}")
        entry = {"_comment": f"{row_name} ({ROW_FRAME_COUNTS.get(row_idx, '?')} frames)"}
        for dir_name, hands_dict in results[row_idx].items():
            entry[dir_name] = hands_dict
        pretty["rows"][str(row_idx)] = entry
    formatted = json.dumps(pretty, indent=4)
    print("\n--- Anchor data ---")
    print(formatted)

    if args.in_place:
        merge_in_place(args.in_place_target, results, measured_dir_names, existing)
    elif args.output:
        with open(args.output, "w", encoding="utf-8") as f:
            f.write(formatted)
        print(f"\nSaved to {args.output}")
    else:
        print("\nPaste the 'rows' object into lpc_humanoid.json hand_anchors.rows")


if __name__ == "__main__":
    main()
