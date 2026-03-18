"""Generate a short SNES-style stat allocation chime WAV.

Two quick ascending square-wave notes with a soft attack/decay envelope.
Sounds like a cute confirmation beep -- similar in vibe to SNES menu confirms.
"""

import struct
import math
import os

SAMPLE_RATE = 44100
AMPLITUDE = 0.35  # keep it gentle

# Two ascending notes: C5 (523 Hz) -> E5 (659 Hz), ~80ms each with 20ms gap
NOTES = [
    (523.25, 0.07),  # C5, 70ms
    (659.25, 0.09),  # E5, 90ms (slightly longer for a satisfying tail)
]
GAP = 0.015  # 15ms silence between notes


def square_wave(freq, t):
    """Square wave with slight rounding (mix of 1st + 3rd harmonic)."""
    fundamental = math.sin(2 * math.pi * freq * t)
    third = math.sin(2 * math.pi * freq * 3 * t) / 3
    return 1.0 if (fundamental + third) > 0 else -1.0


def envelope(t, duration):
    """Soft attack (5ms) and decay (last 40% of note)."""
    attack = min(t / 0.005, 1.0)  # 5ms attack
    decay_start = duration * 0.6
    if t > decay_start:
        decay = 1.0 - (t - decay_start) / (duration - decay_start)
    else:
        decay = 1.0
    return attack * decay


def generate_chime():
    samples = []

    for i, (freq, dur) in enumerate(NOTES):
        num_samples = int(SAMPLE_RATE * dur)
        for s in range(num_samples):
            t = s / SAMPLE_RATE
            val = square_wave(freq, t) * envelope(t, dur) * AMPLITUDE
            samples.append(val)

        # Add gap between notes (not after the last)
        if i < len(NOTES) - 1:
            gap_samples = int(SAMPLE_RATE * GAP)
            samples.extend([0.0] * gap_samples)

    return samples


def write_wav(filepath, samples):
    num_samples = len(samples)
    data_size = num_samples * 2  # 16-bit = 2 bytes per sample
    file_size = 36 + data_size

    with open(filepath, "wb") as f:
        # RIFF header
        f.write(b"RIFF")
        f.write(struct.pack("<I", file_size))
        f.write(b"WAVE")

        # fmt chunk
        f.write(b"fmt ")
        f.write(struct.pack("<I", 16))       # chunk size
        f.write(struct.pack("<H", 1))        # PCM
        f.write(struct.pack("<H", 1))        # mono
        f.write(struct.pack("<I", SAMPLE_RATE))
        f.write(struct.pack("<I", SAMPLE_RATE * 2))  # byte rate
        f.write(struct.pack("<H", 2))        # block align
        f.write(struct.pack("<H", 16))       # bits per sample

        # data chunk
        f.write(b"data")
        f.write(struct.pack("<I", data_size))
        for s in samples:
            clamped = max(-1.0, min(1.0, s))
            f.write(struct.pack("<h", int(clamped * 32767)))


if __name__ == "__main__":
    script_dir = os.path.dirname(os.path.abspath(__file__))
    project_dir = os.path.dirname(script_dir)
    out_path = os.path.join(project_dir, "assets", "sfx", "stat_allocate.wav")

    samples = generate_chime()
    write_wav(out_path, samples)
    duration_ms = len(samples) / SAMPLE_RATE * 1000
    print(f"Generated {out_path} ({duration_ms:.0f}ms, {len(samples)} samples)")
