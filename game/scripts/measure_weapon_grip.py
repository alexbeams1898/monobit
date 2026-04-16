"""
Measure the grip pixel(s) on a weapon icon.

Opens a weapon icon zoomed in and lets you click the exact pixel where the
character's primary hand (trigger hand) should hold it, writing grip_x/grip_y
back to the weapon's JSON.

If the weapon is flagged `two_handed: true`, the script advances to a second
stage where you click the FORE GRIP -- the forward/support hand position --
and writes fore_grip_x/fore_grip_y. The primary grip remains visible as a
cyan cross while you aim the fore click.

Usage:
  python scripts/measure_weapon_grip.py config/items/weapons/colt_45.json
  python scripts/measure_weapon_grip.py config/items/weapons/ak_47.json
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

ZOOM = 12  # display scale
PRIMARY_COLOR = (0, 255, 255, 255)     # cyan -- main grip
FORE_COLOR = (255, 200, 0, 255)        # amber -- fore grip
PRIMARY_FADED = (0, 200, 220, 180)     # shown while aiming fore grip


def draw_marker(draw, cx, cy, color, size=10):
    draw.line([(cx - size, cy), (cx + size, cy)], fill=color, width=2)
    draw.line([(cx, cy - size), (cx, cy + size)], fill=color, width=2)
    draw.ellipse([cx - 5, cy - 5, cx + 5, cy + 5], outline=color, width=2)


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument(
        "weapon_json",
        help="Path to weapon JSON (e.g. config/items/weapons/colt_45.json)",
    )
    args = parser.parse_args()

    repo_root = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))
    json_path = (
        args.weapon_json
        if os.path.isabs(args.weapon_json)
        else os.path.abspath(args.weapon_json)
    )

    if not os.path.exists(json_path):
        print(f"ERROR: weapon JSON not found: {json_path}")
        sys.exit(1)

    with open(json_path, "r", encoding="utf-8") as f:
        weapon = json.load(f)

    icon_rel = weapon.get("icon", "")
    if not icon_rel:
        print(f"ERROR: weapon JSON has no 'icon' field: {json_path}")
        sys.exit(1)

    icon_path = icon_rel
    if not os.path.isabs(icon_path) and not os.path.exists(icon_path):
        icon_path = os.path.join(repo_root, "game", icon_rel)
    if not os.path.exists(icon_path):
        print(f"ERROR: icon not found: {icon_path}")
        sys.exit(1)

    two_handed = bool(weapon.get("two_handed", False))

    icon = Image.open(icon_path).convert("RGBA")
    w, h = icon.size
    print(f"Loaded {icon_rel} ({w}x{h})  two_handed={two_handed}")
    print(f"Current primary grip: ({weapon.get('grip_x', '?')}, {weapon.get('grip_y', '?')})")
    if two_handed:
        print(
            "Current fore grip:    "
            f"({weapon.get('fore_grip_x', '?')}, {weapon.get('fore_grip_y', '?')})"
        )

    display = icon.resize((w * ZOOM, h * ZOOM), Image.NEAREST)

    root = tk.Tk()
    root.title(f"Grip: {weapon.get('name', icon_rel)}")

    # Stage 0 = primary grip, stage 1 = fore grip (two-handed only).
    state = {
        "stage": 0,
        "primary_x": weapon.get("grip_x"),
        "primary_y": weapon.get("grip_y"),
        "fore_x": weapon.get("fore_grip_x") if two_handed else None,
        "fore_y": weapon.get("fore_grip_y") if two_handed else None,
        "tk_img": None,
    }

    def redraw():
        img = display.copy()
        draw = ImageDraw.Draw(img)

        # Always draw the primary grip if it exists.
        if state["primary_x"] is not None and state["primary_y"] is not None:
            cx = int(state["primary_x"] * ZOOM + ZOOM // 2)
            cy = int(state["primary_y"] * ZOOM + ZOOM // 2)
            color = PRIMARY_COLOR if state["stage"] == 0 else PRIMARY_FADED
            draw_marker(draw, cx, cy, color)

        # Draw the fore grip if set and we're in stage 1 (or already saved).
        if two_handed and state["fore_x"] is not None and state["fore_y"] is not None:
            cx = int(state["fore_x"] * ZOOM + ZOOM // 2)
            cy = int(state["fore_y"] * ZOOM + ZOOM // 2)
            draw_marker(draw, cx, cy, FORE_COLOR)

        state["tk_img"] = ImageTk.PhotoImage(img)
        canvas.delete("all")
        canvas.create_image(0, 0, anchor=tk.NW, image=state["tk_img"])

        if state["stage"] == 0:
            stage_label = "PRIMARY grip (trigger hand) -- cyan"
            if two_handed:
                stage_label += "\nNext: FORE grip (support hand)"
        else:
            stage_label = "FORE grip (support hand) -- amber"

        if state["stage"] == 0:
            cur_x = state["primary_x"]
            cur_y = state["primary_y"]
        else:
            cur_x = state["fore_x"]
            cur_y = state["fore_y"]
        pos_text = (
            f"({cur_x}, {cur_y})" if cur_x is not None else "Click to place"
        )
        info_label.config(text=f"{stage_label}\n{pos_text}")

    def on_click(event):
        px = event.x // ZOOM
        py = event.y // ZOOM
        if not (0 <= px < w and 0 <= py < h):
            return
        if state["stage"] == 0:
            state["primary_x"] = px
            state["primary_y"] = py
        else:
            state["fore_x"] = px
            state["fore_y"] = py
        redraw()

    def advance_or_save():
        """If we're on primary and this is a 2H weapon, advance to fore.
        Otherwise save and quit."""
        if state["stage"] == 0 and state["primary_x"] is None:
            print("Click the primary grip first.")
            return
        if state["stage"] == 0 and two_handed:
            state["stage"] = 1
            redraw()
            return
        # Save and quit.
        if state["primary_x"] is None:
            print("No primary grip selected. Nothing saved.")
            return
        weapon["grip_x"] = float(state["primary_x"])
        weapon["grip_y"] = float(state["primary_y"])
        if two_handed and state["fore_x"] is not None:
            weapon["fore_grip_x"] = float(state["fore_x"])
            weapon["fore_grip_y"] = float(state["fore_y"])
        with open(json_path, "w", encoding="utf-8") as f:
            json.dump(weapon, f, indent=4)
            f.write("\n")
        print(
            f"Saved primary=({state['primary_x']},{state['primary_y']})"
            + (
                f" fore=({state['fore_x']},{state['fore_y']})"
                if two_handed and state["fore_x"] is not None
                else ""
            )
            + f" -> {json_path}"
        )
        root.destroy()

    def on_cancel():
        print("Cancelled, no changes saved.")
        root.destroy()

    frame = tk.Frame(root)
    frame.pack(padx=10, pady=10)

    canvas = tk.Canvas(
        frame, width=w * ZOOM, height=h * ZOOM, bg="#222", highlightthickness=0
    )
    canvas.pack()
    canvas.bind("<Button-1>", on_click)

    info_label = tk.Label(frame, text="", font=("Consolas", 11), justify="left")
    info_label.pack(pady=(8, 4))

    btns = tk.Frame(frame)
    btns.pack()
    next_text = "Next (Enter)" if two_handed else "Save (Enter)"
    tk.Button(btns, text=next_text, command=advance_or_save, width=14).pack(
        side=tk.LEFT, padx=4
    )
    tk.Button(btns, text="Cancel (Esc)", command=on_cancel, width=14).pack(
        side=tk.LEFT, padx=4
    )

    root.bind("<Return>", lambda e: advance_or_save())
    root.bind("<s>", lambda e: advance_or_save())
    root.bind("<Escape>", lambda e: on_cancel())

    redraw()
    root.mainloop()


if __name__ == "__main__":
    main()
