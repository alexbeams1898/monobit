"""
Author the hurtbox shared by every entity using the LPC humanoid animation.

Loads the idle-south frame from `body_master.png` and lets you draw shapes
directly on top of it. Writes the shapes into `lpc_humanoid.json` under a
top-level `"hurtbox"` key. All entities that use this animation sheet
(player, cops, skeletons, any future LPC humanoid) inherit these shapes via
ConfigLoader.

Controls:
  1  -- select Circle tool
  2  -- select AABB tool
  3  -- select Capsule tool
  Click + drag -- place a shape (Circle: center -> edge; AABB: center -> corner; Capsule: endpoint 1 -> 2, then 'r' to set radius)
  l -- set label for the selected shape (prompts in terminal)
  m -- set dmg_mult for the selected shape (prompts in terminal)
  Del / Backspace -- delete selected shape
  Tab -- cycle selected shape
  s -- save to lpc_humanoid.json
  q / Esc -- quit

Coordinates written to JSON are in frame-local pixels, with (0, 0) = frame
center (32, 32). Positive x = right, positive y = down.

Usage:
  python scripts/measure_hurtbox.py
  # Defaults:
  #   --sheet game/assets/sprites/lpc/assembled/body/body_master.png
  #   --anim  game/config/animations/lpc_humanoid.json
"""

import argparse
import json
import os
import sys

try:
    from PIL import Image, ImageDraw, ImageTk
    import tkinter as tk
except ImportError as e:
    print(f"ERROR: Missing dependency: {e}")
    print("Install with: pip install Pillow")
    sys.exit(1)

FRAME_SIZE = 64
CENTER_X = FRAME_SIZE // 2  # 32
CENTER_Y = FRAME_SIZE // 2  # 32

SHEET_IDLE_ROW = 0  # idle row in LPC sheet layout
SHEET_S_COL = 0     # south is first direction column
ZOOM = 8            # on-screen zoom for authoring precision


class HurtboxEditor:
    def __init__(self, sheet_path, anim_path, existing):
        # Load the idle-south frame from the assembled sheet.
        sheet = Image.open(sheet_path).convert("RGBA")
        # Assembled sheet is rows x (13 * 4 dir) of 64x64 frames.
        sx = SHEET_S_COL * FRAME_SIZE
        sy = SHEET_IDLE_ROW * FRAME_SIZE
        self.frame = sheet.crop((sx, sy, sx + FRAME_SIZE, sy + FRAME_SIZE))
        self.anim_path = anim_path

        # shapes = list of dicts mirroring the JSON format.
        self.shapes = list(existing) if existing else []
        self.selected = len(self.shapes) - 1 if self.shapes else -1
        self.tool = "circle"  # circle / aabb / capsule
        self.drag_start = None
        self.capsule_stage = 0  # 0 = waiting for p1, 1 = waiting for p2, 2 = waiting for radius

        # Tk setup.
        self.root = tk.Tk()
        self.root.title("Hurtbox Editor")

        # Pre-scale the frame for display.
        self.display_w = FRAME_SIZE * ZOOM
        self.display_h = FRAME_SIZE * ZOOM
        self.display_frame = self.frame.resize(
            (self.display_w, self.display_h), Image.NEAREST
        )

        self.canvas = tk.Canvas(
            self.root, width=self.display_w, height=self.display_h, bg="#222"
        )
        self.canvas.pack(side="left")

        self.status = tk.Label(self.root, text="", justify="left", anchor="nw")
        self.status.pack(side="right", fill="both", padx=6, pady=6)

        self.canvas.bind("<Button-1>", self.on_click)
        self.canvas.bind("<B1-Motion>", self.on_drag)
        self.canvas.bind("<ButtonRelease-1>", self.on_release)
        self.root.bind("<Key>", self.on_key)

        self.photo = None
        self.redraw()

    # Convert canvas pixel -> local frame coord (centered at 32, 32).
    def canvas_to_local(self, cx, cy):
        fx = cx / ZOOM
        fy = cy / ZOOM
        return fx - CENTER_X, fy - CENTER_Y

    # Convert local coord -> canvas pixel.
    def local_to_canvas(self, lx, ly):
        return (lx + CENTER_X) * ZOOM, (ly + CENTER_Y) * ZOOM

    def redraw(self):
        # Draw the scaled sprite frame.
        overlay = self.display_frame.copy()
        draw = ImageDraw.Draw(overlay)

        # Center crosshair.
        draw.line(
            (self.display_w // 2, 0, self.display_w // 2, self.display_h),
            fill=(60, 60, 60, 255),
        )
        draw.line(
            (0, self.display_h // 2, self.display_w, self.display_h // 2),
            fill=(60, 60, 60, 255),
        )

        for i, s in enumerate(self.shapes):
            color = (0, 255, 0, 255) if i == self.selected else (0, 180, 0, 255)
            if s["shape"] == "circle":
                cx, cy = self.local_to_canvas(s["x"], s["y"])
                r = s["r"] * ZOOM
                draw.ellipse(
                    (cx - r, cy - r, cx + r, cy + r), outline=color, width=2
                )
            elif s["shape"] == "aabb":
                cx, cy = self.local_to_canvas(s["x"], s["y"])
                hw = s["w"] * ZOOM * 0.5
                hh = s["h"] * ZOOM * 0.5
                draw.rectangle(
                    (cx - hw, cy - hh, cx + hw, cy + hh), outline=color, width=2
                )
            elif s["shape"] == "capsule":
                x0, y0 = self.local_to_canvas(s["x"], s["y"])
                x1, y1 = self.local_to_canvas(s["x2"], s["y2"])
                r = s["r"] * ZOOM
                draw.ellipse((x0 - r, y0 - r, x0 + r, y0 + r), outline=color, width=2)
                draw.ellipse((x1 - r, y1 - r, x1 + r, y1 + r), outline=color, width=2)
                draw.line((x0, y0, x1, y1), fill=color, width=2)

        self.photo = ImageTk.PhotoImage(overlay)
        self.canvas.delete("all")
        self.canvas.create_image(0, 0, image=self.photo, anchor="nw")

        lines = [f"Tool: {self.tool}", f"Shapes: {len(self.shapes)}"]
        for i, s in enumerate(self.shapes):
            marker = "*" if i == self.selected else " "
            lines.append(
                f"{marker} {i}: {s['shape']} label={s.get('label','')} "
                f"dmg_mult={s.get('dmg_mult', 1.0)}"
            )
        lines.append("")
        lines.append("Keys: 1=circle 2=aabb 3=capsule")
        lines.append("      Tab=cycle sel   Del=delete")
        lines.append("      l=label   m=dmg_mult")
        lines.append("      s=save    q=quit")
        self.status.config(text="\n".join(lines))

    def on_click(self, event):
        lx, ly = self.canvas_to_local(event.x, event.y)
        if self.tool == "capsule":
            if self.capsule_stage == 0:
                # Start a new capsule at endpoint 1.
                self.shapes.append(
                    {
                        "shape": "capsule",
                        "x": lx,
                        "y": ly,
                        "x2": lx,
                        "y2": ly,
                        "r": 4.0,
                        "label": "",
                        "dmg_mult": 1.0,
                    }
                )
                self.selected = len(self.shapes) - 1
                self.capsule_stage = 1
            elif self.capsule_stage == 1:
                s = self.shapes[self.selected]
                s["x2"] = lx
                s["y2"] = ly
                self.capsule_stage = 0
        else:
            self.drag_start = (lx, ly)
            self.shapes.append(
                {
                    "shape": self.tool,
                    "x": lx,
                    "y": ly,
                    "w": 4.0,
                    "h": 4.0,
                    "r": 4.0,
                    "label": "",
                    "dmg_mult": 1.0,
                }
            )
            self.selected = len(self.shapes) - 1
        self.redraw()

    def on_drag(self, event):
        if self.drag_start is None or self.selected < 0:
            return
        lx, ly = self.canvas_to_local(event.x, event.y)
        sx, sy = self.drag_start
        dx = lx - sx
        dy = ly - sy
        s = self.shapes[self.selected]
        if s["shape"] == "circle":
            s["r"] = max(1.0, (dx * dx + dy * dy) ** 0.5)
        elif s["shape"] == "aabb":
            s["w"] = max(1.0, abs(dx) * 2.0)
            s["h"] = max(1.0, abs(dy) * 2.0)
        self.redraw()

    def on_release(self, event):
        self.drag_start = None

    def prompt(self, label, default=""):
        sys.stdout.write(f"{label} [{default}]: ")
        sys.stdout.flush()
        line = sys.stdin.readline().strip()
        return line if line else default

    def on_key(self, event):
        key = event.keysym.lower()
        if key == "1":
            self.tool = "circle"
            self.capsule_stage = 0
        elif key == "2":
            self.tool = "aabb"
            self.capsule_stage = 0
        elif key == "3":
            self.tool = "capsule"
            self.capsule_stage = 0
        elif key == "tab":
            if self.shapes:
                self.selected = (self.selected + 1) % len(self.shapes)
        elif key in ("delete", "backspace"):
            if 0 <= self.selected < len(self.shapes):
                self.shapes.pop(self.selected)
                self.selected = min(self.selected, len(self.shapes) - 1)
        elif key == "l":
            if 0 <= self.selected < len(self.shapes):
                cur = self.shapes[self.selected].get("label", "")
                self.shapes[self.selected]["label"] = self.prompt("Label", cur)
        elif key == "m":
            if 0 <= self.selected < len(self.shapes):
                cur = str(self.shapes[self.selected].get("dmg_mult", 1.0))
                try:
                    self.shapes[self.selected]["dmg_mult"] = float(
                        self.prompt("dmg_mult", cur)
                    )
                except ValueError:
                    print("invalid number; unchanged")
        elif key == "r" and self.tool == "capsule" and self.selected >= 0:
            s = self.shapes[self.selected]
            if s["shape"] == "capsule":
                try:
                    s["r"] = float(self.prompt("capsule radius", str(s.get("r", 4.0))))
                except ValueError:
                    print("invalid number; unchanged")
        elif key == "s":
            self.save()
        elif key in ("q", "escape"):
            self.root.destroy()
            return
        self.redraw()

    def save(self):
        out = []
        for s in self.shapes:
            entry = {"shape": s["shape"], "x": round(s["x"], 2), "y": round(s["y"], 2)}
            if s["shape"] == "circle":
                entry["r"] = round(s["r"], 2)
            elif s["shape"] == "aabb":
                entry["w"] = round(s["w"], 2)
                entry["h"] = round(s["h"], 2)
            elif s["shape"] == "capsule":
                entry["x2"] = round(s["x2"], 2)
                entry["y2"] = round(s["y2"], 2)
                entry["r"] = round(s["r"], 2)
            if s.get("label"):
                entry["label"] = s["label"]
            if s.get("dmg_mult", 1.0) != 1.0:
                entry["dmg_mult"] = round(s["dmg_mult"], 2)
            out.append(entry)

        # Load the animation JSON, merge our shapes under the top-level
        # "hurtbox" key, and write back preserving everything else.
        with open(self.anim_path) as f:
            data = json.load(f)
        data["hurtbox"] = out
        with open(self.anim_path, "w") as f:
            json.dump(data, f, indent=4)
        print(f"Saved {len(out)} shapes -> {self.anim_path} (key: hurtbox)")

    def run(self):
        self.root.mainloop()


DEFAULT_SHEET = "game/assets/sprites/lpc/assembled/body/body_master.png"
DEFAULT_ANIM = "game/config/animations/lpc_humanoid.json"


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--sheet", default=DEFAULT_SHEET, help="path to assembled humanoid body sheet")
    ap.add_argument("--anim", default=DEFAULT_ANIM, help="path to lpc_humanoid.json (edited in place)")
    args = ap.parse_args()

    existing = []
    if os.path.exists(args.anim):
        with open(args.anim) as f:
            data = json.load(f)
        existing = data.get("hurtbox", []) or []

    editor = HurtboxEditor(args.sheet, args.anim, existing)
    editor.run()


if __name__ == "__main__":
    main()
