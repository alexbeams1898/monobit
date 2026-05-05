"""Throwaway preview generator for the unmeasured Pilgrim placeholder.

Builds a 44x46 1-bit silhouette and composites it next to the existing
3-up L1 compare image so the size relationship is visible at 4x scale.
"""
from PIL import Image, ImageDraw

W, H = 44, 46
img = Image.new("L", (W, H), 0)
d = ImageDraw.Draw(img)

cx = W // 2

# head
d.ellipse([cx - 4, 0, cx + 4, 9], fill=255)
# neck
d.rectangle([cx - 2, 8, cx + 2, 11], fill=255)

# torso (tapered)
shoulder_y, waist_y = 11, 26
for y in range(shoulder_y, waist_y + 1):
    t = (y - shoulder_y) / (waist_y - shoulder_y)
    half_w = 6 - t * 1.5
    d.rectangle([cx - int(half_w), y, cx + int(half_w), y], fill=255)

# right arm (gun side)
for y in range(12, 31):
    t = (y - 12) / 18
    x_off = 5 + t * 1.2
    d.rectangle([cx + int(x_off), y, cx + int(x_off) + 2, y], fill=255)

# left arm (empty, slack)
for y in range(12, 30):
    t = (y - 12) / 17
    x_off = -5 - t * 0.8
    d.rectangle([cx + int(x_off) - 2, y, cx + int(x_off), y], fill=255)

# right hand (gun side)
d.rectangle([cx + 6, 30, cx + 9, 33], fill=255)
# left hand
d.rectangle([cx - 8, 29, cx - 5, 32], fill=255)

# flintlock pistol
gun_grip_x = cx + 7
gun_grip_y_top = 32
d.rectangle([gun_grip_x, gun_grip_y_top, gun_grip_x + 2, gun_grip_y_top + 4], fill=255)
for i in range(7):
    bx = cx + 9 + i
    by = gun_grip_y_top + (i // 4)
    d.rectangle([bx, by, bx, by + 1], fill=255)
d.rectangle([gun_grip_x, gun_grip_y_top + 4, gun_grip_x + 2, gun_grip_y_top + 5], fill=255)
d.rectangle([gun_grip_x + 2, gun_grip_y_top - 1, gun_grip_x + 3, gun_grip_y_top], fill=255)

# hips
d.rectangle([cx - 4, 27, cx + 4, 29], fill=255)

# right leg (weight-bearing, straight)
for y in range(29, 46):
    d.rectangle([cx - 5, y, cx - 1, y], fill=255)

# left leg (about-to-step, raised foot)
for y in range(29, 45):
    x_drift = (y - 29) * 0.05
    d.rectangle([cx + 1 + int(x_drift), y, cx + 4 + int(x_drift), y], fill=255)

# right foot (flat)
d.rectangle([cx - 6, 44, cx - 1, 45], fill=255)
# left foot (raised, forward)
d.rectangle([cx + 2, 42, cx + 6, 43], fill=255)

img_1bit = img.point(lambda v: 255 if v > 127 else 0)
img_1bit.save("art/_unmeasured_v1.png")

# Compose with existing L1 compare strip
SCALE = 4
unmeasured_4x = img_1bit.resize((W * SCALE, H * SCALE), Image.NEAREST)
unmeasured_inv = Image.eval(unmeasured_4x, lambda v: 255 - v)

existing = Image.open("art/_l1_compare.png").convert("L")
ew, eh = existing.size

spacing = 40
new_w = ew + spacing + unmeasured_inv.width + 20
new_h = max(eh, unmeasured_inv.height) + 20
canvas = Image.new("L", (new_w, new_h), 255)
canvas.paste(existing, (10, 10))
unm_y = new_h - unmeasured_inv.height - 10
canvas.paste(unmeasured_inv, (ew + spacing, unm_y))
canvas.save("art/_unmeasured_compare.png")
print("wrote art/_unmeasured_v1.png and art/_unmeasured_compare.png")
