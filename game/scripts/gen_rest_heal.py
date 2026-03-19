"""Generate a warm campfire-heal chime WAV for rest spots.

Sound design: layered open fifth (C4-G4-C5) with staggered entry
and exponential decay. Medieval/solemn feel without being harsh.
"""

import math
import os
import struct

SAMPLE_RATE = 22050
DURATION = 1.5  # seconds
OUTPUT_DIR = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "assets", "sfx")


def write_wav(filepath, samples):
    """Write 16-bit mono PCM .wav file."""
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


def generate_rest_heal():
    n = int(SAMPLE_RATE * DURATION)
    samples = []

    fund_freq = 261.63  # C4
    fifth_freq = 392.0  # G4
    octave_freq = 523.25  # C5

    two_pi = 2.0 * math.pi

    for i in range(n):
        t = i / SAMPLE_RATE
        sample = 0.0

        # Soft attack envelope -- 50ms ramp
        attack = min(t / 0.05, 1.0)

        # Layer 1: Warm fundamental (C4) + 2nd harmonic, slow decay
        env1 = attack * math.exp(-t * 2.0)
        sample += math.sin(two_pi * fund_freq * t) * 0.35 * env1
        sample += math.sin(two_pi * fund_freq * 2 * t) * 0.10 * env1

        # Layer 2: Shimmering fifth (G4) + 2nd harmonic, delayed 100ms
        if t > 0.1:
            t2 = t - 0.1
            attack2 = min(t2 / 0.05, 1.0)
            env2 = attack2 * math.exp(-t2 * 2.5)
            sample += math.sin(two_pi * fifth_freq * t) * 0.20 * env2
            sample += math.sin(two_pi * fifth_freq * 2 * t) * 0.06 * env2

        # Layer 3: Soft octave sparkle (C5), delayed 200ms, fast decay
        if t > 0.2:
            t3 = t - 0.2
            attack3 = min(t3 / 0.04, 1.0)
            env3 = attack3 * math.exp(-t3 * 3.5)
            sample += math.sin(two_pi * octave_freq * t) * 0.12 * env3

        samples.append(sample)

    return samples


if __name__ == "__main__":
    os.makedirs(OUTPUT_DIR, exist_ok=True)
    out_path = os.path.join(OUTPUT_DIR, "rest_heal.wav")
    samples = generate_rest_heal()
    write_wav(out_path, samples)
    duration_ms = len(samples) / SAMPLE_RATE * 1000
    print(f"Generated {out_path} ({duration_ms:.0f}ms, {len(samples)} samples)")
