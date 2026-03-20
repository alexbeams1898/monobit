"""Generate simple placeholder .wav files for game sound effects."""

import struct
import math
import os

SAMPLE_RATE = 22050
OUTPUT_DIR = os.path.join(os.path.dirname(__file__), "..", "assets", "sfx")


def write_wav(filename, samples):
    """Write 16-bit mono PCM .wav file."""
    path = os.path.join(OUTPUT_DIR, filename)
    num = len(samples)
    data_size = num * 2
    with open(path, "wb") as f:
        # RIFF header
        f.write(b"RIFF")
        f.write(struct.pack("<I", 36 + data_size))
        f.write(b"WAVE")
        # fmt chunk
        f.write(b"fmt ")
        f.write(struct.pack("<IHHIIHH", 16, 1, 1, SAMPLE_RATE, SAMPLE_RATE * 2, 2, 16))
        # data chunk
        f.write(b"data")
        f.write(struct.pack("<I", data_size))
        for s in samples:
            clamped = max(-1.0, min(1.0, s))
            f.write(struct.pack("<h", int(clamped * 32767)))
    print(f"  {filename} ({num} samples, {num / SAMPLE_RATE:.2f}s)")


def tone(freq, duration, volume=0.5, fade_out=True):
    """Generate a sine tone with optional fade-out."""
    n = int(SAMPLE_RATE * duration)
    samples = []
    for i in range(n):
        t = i / SAMPLE_RATE
        env = 1.0 - (i / n) if fade_out else 1.0
        samples.append(math.sin(2 * math.pi * freq * t) * volume * env)
    return samples


def noise_burst(duration, volume=0.3):
    """Simple pseudo-noise for impact sounds."""
    import random
    random.seed(42)
    n = int(SAMPLE_RATE * duration)
    samples = []
    for i in range(n):
        env = 1.0 - (i / n)
        samples.append((random.random() * 2 - 1) * volume * env)
    return samples


print("Generating placeholder sounds...")

# attack.wav — sword swing whoosh
# Technique: band-passed noise with a frequency sweep simulating air being cut.
# Fast attack, medium decay. The "center" of the noise band sweeps from high
# to low (like a blade passing by) using a simple single-pole bandpass filter.
import random
random.seed(99)
attack_dur = 0.18
attack_n = int(SAMPLE_RATE * attack_dur)
attack_samples = []
# Two-pass: generate raw noise, then apply a time-varying bandpass.
raw_noise = [(random.random() * 2 - 1) for _ in range(attack_n)]

# Simple single-pole lowpass state for cheap bandpass approximation.
lp_state = 0.0
hp_state = 0.0
prev_lp = 0.0

for i in range(attack_n):
    progress = i / attack_n
    # Envelope: fast attack (peaks at ~15%), smooth decay
    if progress < 0.15:
        env = progress / 0.15
    else:
        env = (1.0 - progress) / 0.85
    env = env ** 0.7  # slightly sharper peak

    # Sweep cutoff from ~4000 Hz down to ~800 Hz (blade passing)
    cutoff = 4000 - 3200 * progress
    # RC lowpass coefficient
    rc = 1.0 / (2.0 * math.pi * cutoff)
    dt_sample = 1.0 / SAMPLE_RATE
    alpha = dt_sample / (rc + dt_sample)

    # Lowpass
    lp_state += alpha * (raw_noise[i] - lp_state)
    # Highpass (subtract lowpass from a lower cutoff to form bandpass)
    hp_cutoff = max(200, cutoff * 0.3)
    rc2 = 1.0 / (2.0 * math.pi * hp_cutoff)
    alpha2 = dt_sample / (rc2 + dt_sample)
    hp_state += alpha2 * (lp_state - hp_state)
    bandpassed = lp_state - hp_state

    # Add a subtle tonal "ring" at the sweep frequency for metallic character
    ring = math.sin(2 * math.pi * cutoff * 0.5 * (i / SAMPLE_RATE)) * 0.08 * env

    attack_samples.append((bandpassed * 0.6 + ring) * env)

write_wav("attack.wav", attack_samples)

# skill.wav — epic: rising power chord with layered harmonics and impact
skill_samples = []
# Wind-up phase (0.15s): rising sweep with harmonics
windup_n = int(SAMPLE_RATE * 0.15)
for i in range(windup_n):
    t = i / SAMPLE_RATE
    progress = i / windup_n
    env = progress * 0.5  # fade in
    freq = 120 + 200 * progress
    s = math.sin(2 * math.pi * freq * t) * 0.3
    s += math.sin(2 * math.pi * freq * 1.5 * t) * 0.15  # fifth
    s += math.sin(2 * math.pi * freq * 2.0 * t) * 0.1   # octave
    skill_samples.append(s * env)
# Impact phase (0.25s): low boom + noise burst + decaying resonance
random.seed(77)
impact_n = int(SAMPLE_RATE * 0.25)
for i in range(impact_n):
    t = i / SAMPLE_RATE
    env = 1.0 - (i / impact_n)
    boom = math.sin(2 * math.pi * 80 * t) * 0.5 * env
    noise = (random.random() * 2 - 1) * 0.3 * (env ** 2)
    ring = math.sin(2 * math.pi * 320 * t) * 0.2 * (env ** 1.5)
    skill_samples.append(boom + noise + ring)
write_wav("skill.wav", skill_samples)

# hit.wav — noise burst (impact)
write_wav("hit.wav", noise_burst(0.12, 0.5))

# dodge.wav — rising sweep
sweep = []
for i in range(int(SAMPLE_RATE * 0.15)):
    t = i / SAMPLE_RATE
    freq = 300 + 600 * (i / (SAMPLE_RATE * 0.15))
    env = 1.0 - (i / (SAMPLE_RATE * 0.15))
    sweep.append(math.sin(2 * math.pi * freq * t) * 0.3 * env)
write_wav("dodge.wav", sweep)

# parry.wav — high metallic ping
write_wav("parry.wav", tone(880, 0.08, 0.5) + tone(1760, 0.06, 0.3))

# death.wav — descending tone
death = []
for i in range(int(SAMPLE_RATE * 0.3)):
    t = i / SAMPLE_RATE
    freq = 400 - 300 * (i / (SAMPLE_RATE * 0.3))
    env = 1.0 - (i / (SAMPLE_RATE * 0.3))
    death.append(math.sin(2 * math.pi * freq * t) * 0.4 * env)
write_wav("death.wav", death)

# pickup.wav — rising chime
write_wav("pickup.wav", tone(660, 0.06, 0.3) + tone(880, 0.08, 0.3))

# levelup.wav — triumphant two-note
write_wav("levelup.wav", tone(440, 0.15, 0.4) + tone(660, 0.2, 0.4))

# wall_bump.wav — dull thud (very low tone + noise, short)
random.seed(55)
bump_n = int(SAMPLE_RATE * 0.08)
bump_samples = []
for i in range(bump_n):
    t = i / SAMPLE_RATE
    env = 1.0 - (i / bump_n)
    thud = math.sin(2 * math.pi * 80 * t) * 0.4 * env
    grit = (random.random() * 2 - 1) * 0.15 * (env ** 2)
    bump_samples.append(thud + grit)
write_wav("wall_bump.wav", bump_samples)

# footstep_walk.wav — soft tap (very short, quiet)
random.seed(33)
walk_n = int(SAMPLE_RATE * 0.04)
walk_samples = []
for i in range(walk_n):
    t = i / SAMPLE_RATE
    env = 1.0 - (i / walk_n)
    tap = math.sin(2 * math.pi * 200 * t) * 0.15 * (env ** 2)
    grit = (random.random() * 2 - 1) * 0.1 * (env ** 2)
    walk_samples.append(tap + grit)
write_wav("footstep_walk.wav", walk_samples)

# footstep_run.wav — quick, higher-pitched tap (thinner version of walk)
random.seed(44)
run_n = int(SAMPLE_RATE * 0.035)
run_samples = []
for i in range(run_n):
    t = i / SAMPLE_RATE
    env = 1.0 - (i / run_n)
    # Higher fundamental (320 Hz) + light overtone for brightness.
    tap = math.sin(2 * math.pi * 320 * t) * 0.18 * (env ** 2)
    tap += math.sin(2 * math.pi * 640 * t) * 0.06 * (env ** 3)
    # Minimal grit for a cleaner, thinner feel.
    grit = (random.random() * 2 - 1) * 0.05 * (env ** 3)
    run_samples.append(tap + grit)
write_wav("footstep_run.wav", run_samples)

print("Done!")
