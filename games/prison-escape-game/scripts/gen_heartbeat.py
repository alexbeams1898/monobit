"""Generate a low heartbeat sound for the low-stamina warning.

Sound design: double-pulse (lub-dub) at ~60 Hz with sharp attack and
fast decay. Short duration so it can repeat at variable intervals.
"""

import math
import os
import struct

SAMPLE_RATE = 22050
DURATION = 0.6  # seconds -- one lub-dub cycle
OUTPUT_DIR = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "assets", "sfx")


def write_wav(filepath, samples):
    """Write 16-bit mono PCM .ogg file."""
    num = len(samples)
    data_size = num * 2
    with open(filepath, "wb") as f:
        f.write(b"RIFF")
        f.write(struct.pack("<I", 36 + data_size))
        f.write(b"WAVE")
        f.write(b"fmt ")
        f.write(struct.pack("<IHHIIHH", 16, 1, 1, SAMPLE_RATE, SAMPLE_RATE * 2, 2, 16))
        f.write(b"data")
        f.write(struct.pack("<I", data_size))
        for s in samples:
            clamped = max(-1.0, min(1.0, s))
            f.write(struct.pack("<h", int(clamped * 32767)))


def generate_heartbeat():
    n = int(SAMPLE_RATE * DURATION)
    samples = []
    two_pi = 2.0 * math.pi
    freq = 55.0  # A1 -- deep chest thump

    # Lub at t=0, dub at t=0.18s (typical lub-dub spacing).
    beats = [0.0, 0.18]
    beat_duration = 0.12  # each pulse lasts ~120ms

    for i in range(n):
        t = i / SAMPLE_RATE
        sample = 0.0

        for onset in beats:
            dt = t - onset
            if dt < 0.0 or dt > beat_duration:
                continue

            # Sharp attack (10ms), fast exponential decay.
            attack = min(dt / 0.01, 1.0)
            env = attack * math.exp(-dt * 25.0)

            # Fundamental + slight 2nd harmonic for body.
            sample += math.sin(two_pi * freq * dt) * 0.9 * env
            sample += math.sin(two_pi * freq * 2 * dt) * 0.25 * env

            # Sub-bass thump (30 Hz) for chest feel.
            sample += math.sin(two_pi * 30.0 * dt) * 0.4 * env

        samples.append(sample)

    return samples


if __name__ == "__main__":
    os.makedirs(OUTPUT_DIR, exist_ok=True)
    out_path = os.path.join(OUTPUT_DIR, "heartbeat.ogg")
    samples = generate_heartbeat()
    write_wav(out_path, samples)
    duration_ms = len(samples) / SAMPLE_RATE * 1000
    print(f"Generated {out_path} ({duration_ms:.0f}ms, {len(samples)} samples)")
