"""Extract Dante boss sprites from art/image.png with hand-tuned crops."""
from PIL import Image

src = Image.open("art/image.png").convert("L")
W, H = src.size  # 1024 x 1536
print(f"source: {W}x{H}")

# Hand-tuned (x1, y1, x2, y2) per boss based on visual inspection of the sheet.
# Each row is ~170px tall. Label area is ~270px on the left.
# Coordinates eyeballed from the chat thumbnail at 1024x1536.
crops = {
    "rebel":     (610, 30,  870, 200),    # row 0 - the gates/wraiths (skull-shape, top-right)
    "minos":     (290, 200, 550, 380),    # row 1 - judge with whip
    "cerberus":  (290, 380, 580, 560),    # row 2 - 3-headed dog
    "plutus":    (290, 560, 530, 720),    # row 3 - hoarder
    "phlegyas":  (290, 720, 540, 880),    # row 4 - boatman
    "minotaur":  (290, 880, 600, 1050),   # row 5 - bull
    "geryon":    (320, 1050, 620, 1240),  # row 6 - winged demon
    "lucifer":   (290, 1240, 700, 1500),  # row 8 - 3-faced winged
}

for name, (x1, y1, x2, y2) in crops.items():
    box = src.crop((x1, y1, x2, y2))
    # Auto-trim any all-black margins.
    bw = box.point(lambda p: 255 if p > 60 else 0)
    bbox = bw.getbbox()
    if bbox:
        box = box.crop(bbox)
    out = f"art/boss_{name}.png"
    box.save(out)
    print(f"{out:30s} {box.size[0]:>4d}x{box.size[1]:>4d}")
