"""
Live-tune the humanoid hurtbox overlaid on a character sprite.

Shows the idle-south frame from body_master (+ optional head layer) at high
zoom, overlays the circles currently in lpc_humanoid.json's `hurtbox` field,
and lets you drag/resize them until they match the visible silhouette.
Saves back to the JSON on request.

This is the authoring tool for the shared hurtbox that every LPC humanoid
(player, cop, skeleton, prisoner, etc.) inherits at runtime.

Controls:
  Click a shape to select it
  Drag selected -- move center (circle) or drag either endpoint (capsule)
  Scroll / +/- -- adjust radius of selected
  [ / ] -- stretch capsule length (shorter / longer)
  H -- rotate capsule 90 degrees (toggle vertical <-> horizontal orientation)
  Shift+H -- snap selected capsule to perfectly horizontal (level both ends)
  Shift+V -- snap selected capsule to perfectly vertical (align both ends on X)
  Arrow keys -- nudge selected 1 pixel (shift = 5)
  C -- convert selected shape to CIRCLE
  V -- convert selected shape to CAPSULE (vertical oval)
  Tab -- cycle selected
  N -- add a new circle at frame center
  Del / Backspace -- delete selected
  L -- relabel selected (prompts)
  M -- set dmg_mult (prompts)
  , / . -- shift origin_y DOWN/UP 1 pixel (re-anchor all shapes visually)
  S -- save to lpc_humanoid.json (also writes origin_y into the JSON)
  R -- reload from disk (discard unsaved tweaks)
  Q / Esc -- quit

Coordinate convention: shapes are stored as offsets from the entity's
Transform position. For LPC humanoids the sprite is 64x64 with a 48-tall
collider, so pixel y=40 in the frame corresponds to the Transform origin.
The overlay preview uses this pixel mapping so what you see matches what
renders in-game.
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
ZOOM = 10

# Transform origin in sprite-local pixel coords. Calibrated empirically by
# visual tuning against the in-game debug overlay.
DEFAULT_ORIGIN_Y = 50.0


def load_idle_frame(sheets):
    base = None
    for path in sheets:
        img = Image.open(path).convert("RGBA").crop((0, 0, FRAME_SIZE, FRAME_SIZE))
        if base is None:
            base = img
        else:
            base = Image.alpha_composite(base, img)
    return base


class HurtboxTuner:
    def __init__(self, frame, anim_path, origin_y):
        self.frame = frame
        self.anim_path = anim_path
        self.origin_y = origin_y
        self.shapes = []
        self.selected = -1
        self.dragging = False
        self.drag_dx = 0.0
        self.drag_dy = 0.0

        self.display_w = FRAME_SIZE * ZOOM
        self.display_h = FRAME_SIZE * ZOOM
        self.scaled = frame.resize((self.display_w, self.display_h), Image.NEAREST)

        self.root = tk.Tk()
        self.root.title("Humanoid Hurtbox Tuner")

        self.canvas = tk.Canvas(
            self.root, width=self.display_w, height=self.display_h, bg="#181818"
        )
        self.canvas.pack(side="left")

        self.info = tk.Label(self.root, text="", justify="left", anchor="nw",
                             font=("Consolas", 10))
        self.info.pack(side="right", fill="both", padx=8, pady=8)

        self.canvas.bind("<Button-1>", self.on_click)
        self.canvas.bind("<B1-Motion>", self.on_drag)
        self.canvas.bind("<ButtonRelease-1>", self.on_release)
        self.canvas.bind("<MouseWheel>", self.on_scroll)
        self.root.bind("<Key>", self.on_key)

        self.photo = None
        self.load_from_disk()

    # -- coord helpers --
    # shape.x/.y are in world-offset space (transform-relative pixels).
    # Convert to sprite-local pixel (where 0,0 is sprite top-left):
    #   px = shape.x + FRAME_SIZE/2
    #   py = shape.y + origin_y
    def shape_to_sprite(self, s):
        return (s["x"] + FRAME_SIZE / 2, s["y"] + self.origin_y)

    def sprite_to_shape(self, px, py):
        return (px - FRAME_SIZE / 2, py - self.origin_y)

    def sprite_to_canvas(self, px, py):
        return (px * ZOOM, py * ZOOM)

    def canvas_to_sprite(self, cx, cy):
        return (cx / ZOOM, cy / ZOOM)

    # -- disk IO --
    def load_from_disk(self):
        with open(self.anim_path) as f:
            data = json.load(f)
        raw = data.get("hurtbox", [])
        self.shapes = []
        for s in raw:
            kind = s.get("shape")
            if kind not in ("circle", "capsule"):
                # Skip unsupported (aabb). Preserved on save via the un-editable
                # "other" list below.
                continue
            self.shapes.append(dict(s))
        self.selected = 0 if self.shapes else -1
        self.redraw()

    def save_to_disk(self):
        with open(self.anim_path) as f:
            data = json.load(f)
        # Preserve any non-circle/capsule shapes that were in the file already.
        preserved = [
            s
            for s in data.get("hurtbox", [])
            if s.get("shape") not in ("circle", "capsule")
        ]
        out = []
        for s in self.shapes:
            if s["shape"] == "circle":
                entry = {
                    "shape": "circle",
                    "x": round(s["x"], 2),
                    "y": round(s["y"], 2),
                    "r": round(s["r"], 2),
                }
            else:
                entry = {
                    "shape": "capsule",
                    "x": round(s["x"], 2),
                    "y": round(s["y"], 2),
                    "x2": round(s["x2"], 2),
                    "y2": round(s["y2"], 2),
                    "r": round(s["r"], 2),
                }
            if s.get("label"):
                entry["label"] = s["label"]
            if s.get("dmg_mult", 1.0) != 1.0:
                entry["dmg_mult"] = round(s["dmg_mult"], 2)
            out.append(entry)
        data["hurtbox"] = preserved + out
        with open(self.anim_path, "w") as f:
            json.dump(data, f, indent=4)
        print(f"Saved {len(out)} shape(s) -> {self.anim_path}")

    # -- rendering --
    def redraw(self):
        overlay = self.scaled.copy()
        draw = ImageDraw.Draw(overlay)

        # Crosshair at frame center (sprite pixel 32, 32).
        draw.line(
            (self.display_w // 2, 0, self.display_w // 2, self.display_h),
            fill=(60, 60, 60, 255),
        )
        draw.line(
            (0, self.display_h // 2, self.display_w, self.display_h // 2),
            fill=(60, 60, 60, 255),
        )

        # Horizontal line at the transform origin (origin_y).
        oy_c = int(self.origin_y * ZOOM)
        draw.line((0, oy_c, self.display_w, oy_c), fill=(200, 80, 80, 180))
        draw.text((6, oy_c - 12), "transform origin", fill=(200, 80, 80))

        for i, s in enumerate(self.shapes):
            col = (0, 255, 0) if i == self.selected else (0, 180, 0)
            r = s["r"] * ZOOM
            if s["shape"] == "circle":
                px, py = self.shape_to_sprite(s)
                cx, cy = self.sprite_to_canvas(px, py)
                draw.ellipse((cx - r, cy - r, cx + r, cy + r), outline=col, width=2)
                draw.text((cx + r + 3, cy - 6), s.get("label", ""), fill=col)
            else:
                # Capsule: two end circles + two connecting lines offset by r.
                p1x, p1y = self.shape_to_sprite(s)
                p2x, p2y = (s["x2"] + FRAME_SIZE / 2, s["y2"] + self.origin_y)
                c1x, c1y = self.sprite_to_canvas(p1x, p1y)
                c2x, c2y = self.sprite_to_canvas(p2x, p2y)
                draw.ellipse((c1x - r, c1y - r, c1x + r, c1y + r), outline=col, width=2)
                draw.ellipse((c2x - r, c2y - r, c2x + r, c2y + r), outline=col, width=2)
                dx = c2x - c1x
                dy = c2y - c1y
                L = (dx * dx + dy * dy) ** 0.5
                if L > 0:
                    nx = -dy / L * r
                    ny = dx / L * r
                    draw.line((c1x + nx, c1y + ny, c2x + nx, c2y + ny), fill=col, width=2)
                    draw.line((c1x - nx, c1y - ny, c2x - nx, c2y - ny), fill=col, width=2)
                draw.text((max(c1x, c2x) + r + 3, (c1y + c2y) / 2 - 6),
                          s.get("label", ""), fill=col)

        self.photo = ImageTk.PhotoImage(overlay)
        self.canvas.delete("all")
        self.canvas.create_image(0, 0, image=self.photo, anchor="nw")

        # Info pane
        lines = [
            f"origin_y = {self.origin_y}",
            f"shapes: {len(self.shapes)}  selected: {self.selected}",
            "",
        ]
        for i, s in enumerate(self.shapes):
            marker = "*" if i == self.selected else " "
            lines.append(
                f"{marker} {i}: {s.get('label','')} "
                f"x={s['x']:.1f} y={s['y']:.1f} r={s['r']:.1f} "
                f"dmg={s.get('dmg_mult',1.0)}"
            )
        lines += [
            "",
            "click: select  drag: move",
            "scroll / +-: radius",
            "arrows: nudge (shift=5px)",
            "N: new   Del: delete",
            "L: label  M: dmg_mult",
            "S: save   R: reload",
            "Q/Esc: quit",
        ]
        self.info.config(text="\n".join(lines))

    # -- input --
    def find_shape_at(self, sx, sy):
        # For capsules, test both endpoints + the connecting segment (within r).
        best = -1
        best_d = 1e9
        for i, s in enumerate(self.shapes):
            if s["shape"] == "circle":
                px, py = self.shape_to_sprite(s)
                d = ((sx - px) ** 2 + (sy - py) ** 2) ** 0.5
                if d <= s["r"] and d < best_d:
                    best = i
                    best_d = d
            else:
                # Distance from point to capsule segment.
                p1x, p1y = self.shape_to_sprite(s)
                p2x, p2y = (s["x2"] + FRAME_SIZE / 2, s["y2"] + self.origin_y)
                vx = p2x - p1x
                vy = p2y - p1y
                L2 = vx * vx + vy * vy
                if L2 < 1e-6:
                    t = 0
                else:
                    t = max(0.0, min(1.0, ((sx - p1x) * vx + (sy - p1y) * vy) / L2))
                qx = p1x + t * vx
                qy = p1y + t * vy
                d = ((sx - qx) ** 2 + (sy - qy) ** 2) ** 0.5
                if d <= s["r"] and d < best_d:
                    best = i
                    best_d = d
        return best

    def on_click(self, event):
        sx, sy = self.canvas_to_sprite(event.x, event.y)
        idx = self.find_shape_at(sx, sy)
        if idx >= 0:
            self.selected = idx
            self.dragging = True
            # Record drag offset so the shape doesn't jump to the cursor.
            s = self.shapes[idx]
            px, py = self.shape_to_sprite(s)
            self.drag_dx = px - sx
            self.drag_dy = py - sy
        self.redraw()

    def on_drag(self, event):
        if not self.dragging or self.selected < 0:
            return
        sx, sy = self.canvas_to_sprite(event.x, event.y)
        new_px = sx + self.drag_dx
        new_py = sy + self.drag_dy
        new_x, new_y = self.sprite_to_shape(new_px, new_py)
        s = self.shapes[self.selected]
        if s["shape"] == "capsule":
            dx = new_x - s["x"]
            dy = new_y - s["y"]
            s["x"] = new_x
            s["y"] = new_y
            s["x2"] += dx
            s["y2"] += dy
        else:
            s["x"] = new_x
            s["y"] = new_y
        self.redraw()

    def on_release(self, event):
        self.dragging = False

    def on_scroll(self, event):
        if self.selected < 0:
            return
        delta = 1 if event.delta > 0 else -1
        s = self.shapes[self.selected]
        s["r"] = max(1.0, s["r"] + delta * 0.5)
        self.redraw()

    def prompt(self, label, default=""):
        sys.stdout.write(f"{label} [{default}]: ")
        sys.stdout.flush()
        return sys.stdin.readline().strip() or default

    def on_key(self, event):
        k = event.keysym.lower()
        shift = (event.state & 0x1) != 0
        nudge = 5.0 if shift else 1.0

        if k == "tab" and self.shapes:
            self.selected = (self.selected + 1) % len(self.shapes)
        elif k == "c" and self.selected >= 0:
            # Convert to circle.
            s = self.shapes[self.selected]
            if s["shape"] != "circle":
                s["shape"] = "circle"
                # Keep center; drop capsule endpoints.
                s.pop("x2", None)
                s.pop("y2", None)
        elif k == "v" and self.selected >= 0:
            # Convert to capsule; preserve the visual height but shrink width.
            # Segment length = 2x old radius, new radius = half old (taller
            # and narrower than the original circle -- clearly oval).
            s = self.shapes[self.selected]
            if s["shape"] != "capsule":
                old_r = s.get("r", 8.0)
                segment_half = old_r
                s["shape"] = "capsule"
                s["x2"] = s["x"]
                s["y2"] = s["y"] + segment_half * 2.0
                s["x"] = s["x"]
                s["y"] = s["y"]
                s["r"] = max(3.0, old_r * 0.5)
                # Recenter by moving both endpoints so midpoint is at original x,y.
                s["y"] = s["y"] - segment_half
                s["y2"] = s["y"] + segment_half * 2.0
                print(f"converted #{self.selected} to capsule; shape={s}")
        elif k == "h" and self.selected >= 0:
            s = self.shapes[self.selected]
            if s["shape"] == "capsule":
                if shift:
                    # Snap to horizontal: both endpoints get the avg y.
                    avg_y = (s["y"] + s["y2"]) / 2.0
                    s["y"] = avg_y
                    s["y2"] = avg_y
                else:
                    # Rotate 90 degrees around its midpoint.
                    midx = (s["x"] + s["x2"]) / 2.0
                    midy = (s["y"] + s["y2"]) / 2.0
                    dx = s["x"] - midx
                    dy = s["y"] - midy
                    s["x"] = midx - dy
                    s["y"] = midy + dx
                    s["x2"] = midx + dy
                    s["y2"] = midy - dx
        elif k == "bracketleft" and self.selected >= 0:
            s = self.shapes[self.selected]
            if s["shape"] == "capsule":
                # Shrink segment by moving both ends toward center.
                midx = (s["x"] + s["x2"]) / 2.0
                midy = (s["y"] + s["y2"]) / 2.0
                s["x"] = midx + (s["x"] - midx) * 0.9
                s["y"] = midy + (s["y"] - midy) * 0.9
                s["x2"] = midx + (s["x2"] - midx) * 0.9
                s["y2"] = midy + (s["y2"] - midy) * 0.9
        elif k == "bracketright" and self.selected >= 0:
            s = self.shapes[self.selected]
            if s["shape"] == "capsule":
                midx = (s["x"] + s["x2"]) / 2.0
                midy = (s["y"] + s["y2"]) / 2.0
                s["x"] = midx + (s["x"] - midx) * 1.1
                s["y"] = midy + (s["y"] - midy) * 1.1
                s["x2"] = midx + (s["x2"] - midx) * 1.1
                s["y2"] = midy + (s["y2"] - midy) * 1.1
        elif k in ("delete", "backspace") and self.selected >= 0:
            self.shapes.pop(self.selected)
            self.selected = min(self.selected, len(self.shapes) - 1)
        elif k == "n":
            self.shapes.append({"shape": "circle", "x": 0, "y": 0, "r": 8.0,
                                "label": "", "dmg_mult": 1.0})
            self.selected = len(self.shapes) - 1
        elif k in ("plus", "equal") and self.selected >= 0:
            self.shapes[self.selected]["r"] += 0.5
        elif k == "minus" and self.selected >= 0:
            self.shapes[self.selected]["r"] = max(1.0, self.shapes[self.selected]["r"] - 0.5)
        elif k == "left" and self.selected >= 0:
            self.shapes[self.selected]["x"] -= nudge
        elif k == "right" and self.selected >= 0:
            self.shapes[self.selected]["x"] += nudge
        elif k == "up" and self.selected >= 0:
            self.shapes[self.selected]["y"] -= nudge
        elif k == "down" and self.selected >= 0:
            self.shapes[self.selected]["y"] += nudge
        elif k == "l" and self.selected >= 0:
            self.shapes[self.selected]["label"] = self.prompt(
                "label", self.shapes[self.selected].get("label", "")
            )
        elif k == "m" and self.selected >= 0:
            cur = str(self.shapes[self.selected].get("dmg_mult", 1.0))
            try:
                self.shapes[self.selected]["dmg_mult"] = float(self.prompt("dmg_mult", cur))
            except ValueError:
                print("invalid number; unchanged")
        elif k == "s":
            self.save_to_disk()
        elif k == "r":
            self.load_from_disk()
            return
        elif k in ("q", "escape"):
            self.root.destroy()
            return
        self.redraw()

    def run(self):
        self.root.mainloop()


DEFAULT_BODY = "game/assets/sprites/lpc/assembled/body/body_master.png"
DEFAULT_HEAD = "game/assets/sprites/lpc/assembled/head/base_master.png"
DEFAULT_ANIM = "game/config/animations/lpc_humanoid.json"


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--body", default=DEFAULT_BODY)
    ap.add_argument("--head", default=DEFAULT_HEAD, help="optional head layer to composite")
    ap.add_argument("--anim", default=DEFAULT_ANIM)
    ap.add_argument("--origin-y", type=float, default=DEFAULT_ORIGIN_Y,
                    help="sprite pixel y that maps to world Transform.y (default 40 for LPC)")
    args = ap.parse_args()

    sheets = [args.body]
    if args.head and os.path.exists(args.head):
        sheets.append(args.head)

    frame = load_idle_frame(sheets)
    tuner = HurtboxTuner(frame, args.anim, args.origin_y)
    tuner.run()


if __name__ == "__main__":
    main()
