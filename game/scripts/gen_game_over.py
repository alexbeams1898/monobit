"""Generate a dark, ominous game-over sound.

Sound design: deep C2 fundamental + minor third (Eb2) for dissonance +
sub-bass rumble (32 Hz). Slow exponential decay. Hell aesthetic.
"""

import math
import os
import struct

SAMPLE_RATE = 22050
DURATION = 2.5  # seconds
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


def generate_game_over():
    n = int(SAMPLE_RATE * DURATION)
    samples = []

    fund_freq = 65.41  # C2
    minor_third = 77.78  # Eb2 (slightly detuned for unease)
    sub_bass = 32.0  # sub-bass rumble

    two_pi = 2.0 * math.pi

    for i in range(n):
        t = i / SAMPLE_RATE
        sample = 0.0

        # Soft attack -- 80ms ramp to avoid click
        attack = min(t / 0.08, 1.0)

        # Layer 1: Deep fundamental (C2) + 2nd harmonic, slow decay
        env1 = attack * math.exp(-t * 1.2)
        sample += math.sin(two_pi * fund_freq * t) * 0.40 * env1
        sample += math.sin(two_pi * fund_freq * 2 * t) * 0.12 * env1

        # Layer 2: Minor third (Eb2) -- delayed 150ms, creates dissonance
        if t > 0.15:
            t2 = t - 0.15
            attack2 = min(t2 / 0.08, 1.0)
            env2 = attack2 * math.exp(-t2 * 1.5)
            sample += math.sin(two_pi * minor_third * t) * 0.25 * env2
            # Slight detune on the 2nd harmonic for beating/warble
            sample += math.sin(two_pi * minor_third * 2.03 * t) * 0.06 * env2

        # Layer 3: Sub-bass rumble -- fade in over 200ms, slow decay
        sub_attack = min(t / 0.2, 1.0)
        env3 = sub_attack * math.exp(-t * 0.8)
        sample += math.sin(two_pi * sub_bass * t) * 0.20 * env3

        samples.append(sample)

    return samples


if __name__ == "__main__":
    os.makedirs(OUTPUT_DIR, exist_ok=True)
    out_path = os.path.join(OUTPUT_DIR, "game_over.wav")
    samples = generate_game_over()
    write_wav(out_path, samples)
    duration_ms = len(samples) / SAMPLE_RATE * 1000
    print(f"Generated {out_path} ({duration_ms:.0f}ms, {len(samples)} samples)")
