"""Generate placeholder .wav files for game sound effects.

DSP effects applied per-sound for polished game-ready audio:
  - Soft-clip distortion (tanh waveshaping)
  - Comb-filter reverb (delay + feedback)
  - Dynamics compression (soft knee)
  - Lowpass / highpass filtering
"""

import struct
import math
import os
import random

SAMPLE_RATE = 22050
OUTPUT_DIR = os.path.join(os.path.dirname(__file__), "..", "assets", "sfx")


# ---------------------------------------------------------------------------
# WAV writer
# ---------------------------------------------------------------------------

def normalize(samples, target=0.9):
    """Scale samples so peak amplitude hits target. All WAVs get the same
    headroom, and relative loudness is controlled entirely by playback volume."""
    peak = max(abs(s) for s in samples) if samples else 1.0
    if peak <= 0:
        return samples
    gain = target / peak
    return [s * gain for s in samples]


def write_wav(filename, samples):
    """Normalize then write 16-bit mono PCM .wav file."""
    samples = normalize(samples)
    path = os.path.join(OUTPUT_DIR, filename)
    num = len(samples)
    data_size = num * 2
    with open(path, "wb") as f:
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
    print(f"  {filename} ({num} samples, {num / SAMPLE_RATE:.2f}s)")


# ---------------------------------------------------------------------------
# DSP effects
# ---------------------------------------------------------------------------

def distort(samples, drive=2.0):
    """Soft-clip distortion via tanh waveshaping. drive=1 is subtle, 4+ is heavy."""
    return [math.tanh(s * drive) / math.tanh(drive) for s in samples]


def reverb(samples, delay_ms=40, feedback=0.3, mix=0.25):
    """Simple comb-filter reverb. Short delays = room feel, long = hall."""
    delay_n = int(SAMPLE_RATE * delay_ms / 1000.0)
    out = list(samples)
    for i in range(delay_n, len(out)):
        out[i] += out[i - delay_n] * feedback
    # Mix wet/dry
    return [d * (1 - mix) + w * mix for d, w in zip(samples, out)]


def compress(samples, threshold=0.3, ratio=4.0):
    """Soft-knee dynamics compression. Makes loud parts quieter, then normalizes."""
    out = []
    for s in samples:
        sign = 1 if s >= 0 else -1
        level = abs(s)
        if level > threshold:
            over = level - threshold
            level = threshold + over / ratio
        out.append(sign * level)
    # Normalize to original peak
    peak_in = max(abs(s) for s in samples) if samples else 1.0
    peak_out = max(abs(s) for s in out) if out else 1.0
    if peak_out > 0 and peak_in > 0:
        gain = peak_in / peak_out
        out = [s * gain for s in out]
    return out


def lowpass(samples, cutoff):
    """Single-pole RC lowpass filter."""
    rc = 1.0 / (2.0 * math.pi * cutoff)
    dt = 1.0 / SAMPLE_RATE
    alpha = dt / (rc + dt)
    state = 0.0
    out = []
    for s in samples:
        state += alpha * (s - state)
        out.append(state)
    return out


def highpass(samples, cutoff):
    """Single-pole RC highpass filter (signal minus lowpass)."""
    lp = lowpass(samples, cutoff)
    return [s - l for s, l in zip(samples, lp)]


# ---------------------------------------------------------------------------
# Generators
# ---------------------------------------------------------------------------

def tone(freq, duration, volume=0.5, fade_out=True):
    """Generate a sine tone with optional fade-out."""
    n = int(SAMPLE_RATE * duration)
    samples = []
    for i in range(n):
        t = i / SAMPLE_RATE
        env = 1.0 - (i / n) if fade_out else 1.0
        samples.append(math.sin(2 * math.pi * freq * t) * volume * env)
    return samples


def noise_burst(duration, volume=0.3, seed=42):
    """Pseudo-noise burst for impact sounds."""
    random.seed(seed)
    n = int(SAMPLE_RATE * duration)
    samples = []
    for i in range(n):
        env = 1.0 - (i / n)
        samples.append((random.random() * 2 - 1) * volume * env)
    return samples


# ---------------------------------------------------------------------------
# Sound generation
# ---------------------------------------------------------------------------

print("Generating placeholder sounds...")

# --- swing_miss.wav --- comical whistle whoosh for swings that miss
# Descending cartoon whistle with breathy noise. Quick and funny.
random.seed(101)
miss_dur = 0.3
miss_n = int(SAMPLE_RATE * miss_dur)
miss_noise = [(random.random() * 2 - 1) for _ in range(miss_n)]
miss_lp = 0.0
miss_samples = []
for i in range(miss_n):
    t = i / SAMPLE_RATE
    progress = i / miss_n
    # Fast attack, smooth tail
    if progress < 0.05:
        env = progress / 0.05
    else:
        env = (1.0 - progress) / 0.95
    env = env ** 0.7
    # Ascending whistle -- low to high, cartoon style
    whistle_freq = 600 + 1600 * progress
    whistle = math.sin(2 * math.pi * whistle_freq * t) * 0.4 * env
    # Second harmonic for richness
    whistle += math.sin(2 * math.pi * whistle_freq * 2 * t) * 0.08 * env
    # Breathy noise underneath for air feel
    cutoff = 4000 - 2000 * progress
    rc = 1.0 / (2.0 * math.pi * cutoff)
    dt_sample = 1.0 / SAMPLE_RATE
    alpha = dt_sample / (rc + dt_sample)
    miss_lp += alpha * (miss_noise[i] - miss_lp)
    breath = miss_lp * 0.15 * env
    miss_samples.append(whistle + breath)
miss_samples = reverb(miss_samples, delay_ms=30, feedback=0.2, mix=0.15)
write_wav("swing_miss.wav", miss_samples)

# --- attack.wav --- sword swing whoosh
# Band-passed noise sweep (high->low) with metallic ring, distortion, and reverb.
random.seed(99)
attack_dur = 0.18
attack_n = int(SAMPLE_RATE * attack_dur)
raw_noise = [(random.random() * 2 - 1) for _ in range(attack_n)]
lp_state = 0.0
hp_state = 0.0
attack_samples = []
for i in range(attack_n):
    progress = i / attack_n
    if progress < 0.15:
        env = progress / 0.15
    else:
        env = (1.0 - progress) / 0.85
    env = env ** 0.7
    cutoff = 4000 - 3200 * progress
    rc = 1.0 / (2.0 * math.pi * cutoff)
    dt_sample = 1.0 / SAMPLE_RATE
    alpha = dt_sample / (rc + dt_sample)
    lp_state += alpha * (raw_noise[i] - lp_state)
    hp_cutoff = max(200, cutoff * 0.3)
    rc2 = 1.0 / (2.0 * math.pi * hp_cutoff)
    alpha2 = dt_sample / (rc2 + dt_sample)
    hp_state += alpha2 * (lp_state - hp_state)
    bandpassed = lp_state - hp_state
    ring = math.sin(2 * math.pi * cutoff * 0.5 * (i / SAMPLE_RATE)) * 0.08 * env
    attack_samples.append((bandpassed * 0.6 + ring) * env)
attack_samples = distort(attack_samples, 1.5)
attack_samples = reverb(attack_samples, delay_ms=25, feedback=0.2, mix=0.15)
write_wav("attack.wav", attack_samples)

# --- skill.wav --- rising power chord + distorted impact
skill_samples = []
windup_n = int(SAMPLE_RATE * 0.15)
for i in range(windup_n):
    t = i / SAMPLE_RATE
    progress = i / windup_n
    env = progress * 0.5
    freq = 120 + 200 * progress
    s = math.sin(2 * math.pi * freq * t) * 0.3
    s += math.sin(2 * math.pi * freq * 1.5 * t) * 0.15
    s += math.sin(2 * math.pi * freq * 2.0 * t) * 0.1
    skill_samples.append(s * env)
random.seed(77)
impact_n = int(SAMPLE_RATE * 0.25)
for i in range(impact_n):
    t = i / SAMPLE_RATE
    env = 1.0 - (i / impact_n)
    boom = math.sin(2 * math.pi * 80 * t) * 0.5 * env
    noise = (random.random() * 2 - 1) * 0.3 * (env ** 2)
    ring = math.sin(2 * math.pi * 320 * t) * 0.2 * (env ** 1.5)
    skill_samples.append(boom + noise + ring)
skill_samples = distort(skill_samples, 2.0)
skill_samples = reverb(skill_samples, delay_ms=60, feedback=0.35, mix=0.3)
skill_samples = compress(skill_samples, threshold=0.35, ratio=3.0)
write_wav("skill.wav", skill_samples)

# --- hit.wav --- punchy noise impact with distortion
hit_samples = noise_burst(0.12, 0.5, seed=42)
hit_samples = distort(hit_samples, 3.0)
hit_samples = lowpass(hit_samples, 3000)
hit_samples = compress(hit_samples, threshold=0.3, ratio=4.0)
hit_samples = reverb(hit_samples, delay_ms=20, feedback=0.15, mix=0.1)
write_wav("hit.wav", hit_samples)

# --- dodge.wav --- rising sweep with subtle reverb
sweep = []
for i in range(int(SAMPLE_RATE * 0.15)):
    t = i / SAMPLE_RATE
    progress = i / (SAMPLE_RATE * 0.15)
    freq = 300 + 600 * progress
    env = 1.0 - progress
    sweep.append(math.sin(2 * math.pi * freq * t) * 0.3 * env)
sweep = reverb(sweep, delay_ms=30, feedback=0.2, mix=0.2)
write_wav("dodge.wav", sweep)

# --- parry.wav --- metallic ping with ring reverb
parry = tone(880, 0.08, 0.5) + tone(1760, 0.06, 0.3)
parry = reverb(parry, delay_ms=35, feedback=0.4, mix=0.3)
parry = highpass(parry, 400)
write_wav("parry.wav", parry)

# --- death.wav --- descending tone with distortion + reverb
death = []
for i in range(int(SAMPLE_RATE * 0.3)):
    t = i / SAMPLE_RATE
    progress = i / (SAMPLE_RATE * 0.3)
    freq = 400 - 300 * progress
    env = 1.0 - progress
    death.append(math.sin(2 * math.pi * freq * t) * 0.4 * env)
death = distort(death, 2.0)
death = reverb(death, delay_ms=50, feedback=0.3, mix=0.25)
write_wav("death.wav", death)

# --- pickup.wav --- rising chime with sparkle reverb
pickup = tone(660, 0.06, 0.3) + tone(880, 0.08, 0.3)
pickup = reverb(pickup, delay_ms=45, feedback=0.3, mix=0.25)
write_wav("pickup.wav", pickup)

# --- levelup.wav --- triumphant two-note with overdrive + reverb
levelup = tone(440, 0.15, 0.4) + tone(660, 0.2, 0.4)
levelup = distort(levelup, 1.3)
levelup = reverb(levelup, delay_ms=80, feedback=0.35, mix=0.3)
write_wav("levelup.wav", levelup)

# --- stat_allocate.wav --- crisp UI confirmation click
random.seed(88)
alloc_n = int(SAMPLE_RATE * 0.06)
alloc_samples = []
for i in range(alloc_n):
    t = i / SAMPLE_RATE
    progress = i / alloc_n
    env = (1.0 - progress) ** 1.5
    click = math.sin(2 * math.pi * 1200 * t) * 0.25 * env
    click += math.sin(2 * math.pi * 2400 * t) * 0.1 * env
    pop = (random.random() * 2 - 1) * 0.08 * (env ** 3)
    alloc_samples.append(click + pop)
alloc_samples = highpass(alloc_samples, 600)
write_wav("stat_allocate.wav", alloc_samples)

# --- wall_bump.wav --- dull thud with distortion
random.seed(55)
bump_n = int(SAMPLE_RATE * 0.08)
bump_samples = []
for i in range(bump_n):
    t = i / SAMPLE_RATE
    env = 1.0 - (i / bump_n)
    thud = math.sin(2 * math.pi * 80 * t) * 0.4 * env
    grit = (random.random() * 2 - 1) * 0.15 * (env ** 2)
    bump_samples.append(thud + grit)
bump_samples = distort(bump_samples, 2.0)
bump_samples = lowpass(bump_samples, 800)
write_wav("wall_bump.wav", bump_samples)

# --- footstep_walk.wav --- soft tap (minimal effects to stay subtle)
random.seed(33)
walk_n = int(SAMPLE_RATE * 0.04)
walk_samples = []
for i in range(walk_n):
    t = i / SAMPLE_RATE
    env = 1.0 - (i / walk_n)
    tap = math.sin(2 * math.pi * 200 * t) * 0.15 * (env ** 2)
    grit = (random.random() * 2 - 1) * 0.1 * (env ** 2)
    walk_samples.append(tap + grit)
walk_samples = lowpass(walk_samples, 2000)
write_wav("footstep_walk.wav", walk_samples)

# --- footstep_run.wav --- quick bright tap
random.seed(44)
run_n = int(SAMPLE_RATE * 0.035)
run_samples = []
for i in range(run_n):
    t = i / SAMPLE_RATE
    env = 1.0 - (i / run_n)
    tap = math.sin(2 * math.pi * 320 * t) * 0.18 * (env ** 2)
    tap += math.sin(2 * math.pi * 640 * t) * 0.06 * (env ** 3)
    grit = (random.random() * 2 - 1) * 0.05 * (env ** 3)
    run_samples.append(tap + grit)
run_samples = lowpass(run_samples, 3000)
write_wav("footstep_run.wav", run_samples)

# --- rest_heal.wav --- gentle ascending crystalline chime
heal_samples = []
# Three ascending notes with shimmer overtones
for note_i, (freq, dur) in enumerate([(523, 0.12), (659, 0.12), (784, 0.16)]):
    n = int(SAMPLE_RATE * dur)
    for i in range(n):
        t = i / SAMPLE_RATE
        env = 1.0 - (i / n) ** 0.5
        s = math.sin(2 * math.pi * freq * t) * 0.25 * env
        s += math.sin(2 * math.pi * freq * 2 * t) * 0.08 * env  # octave shimmer
        s += math.sin(2 * math.pi * freq * 3 * t) * 0.03 * env  # bright overtone
        heal_samples.append(s)
heal_samples = reverb(heal_samples, delay_ms=70, feedback=0.4, mix=0.35)
write_wav("rest_heal.wav", heal_samples)

# --- game_over.wav --- dramatic low descending dissonance with heavy distortion
random.seed(66)
go_dur = 0.8
go_n = int(SAMPLE_RATE * go_dur)
go_samples = []
for i in range(go_n):
    t = i / SAMPLE_RATE
    progress = i / go_n
    env = (1.0 - progress) ** 0.6
    # Low descending fundamental
    freq1 = 200 - 140 * progress
    s = math.sin(2 * math.pi * freq1 * t) * 0.35 * env
    # Dissonant minor second interval
    freq2 = freq1 * 1.06
    s += math.sin(2 * math.pi * freq2 * t) * 0.2 * env
    # Sub-bass rumble
    s += math.sin(2 * math.pi * 40 * t) * 0.15 * env
    # Noise texture
    s += (random.random() * 2 - 1) * 0.08 * (env ** 2)
    go_samples.append(s)
go_samples = distort(go_samples, 3.0)
go_samples = lowpass(go_samples, 2000)
go_samples = reverb(go_samples, delay_ms=100, feedback=0.4, mix=0.35)
go_samples = compress(go_samples, threshold=0.3, ratio=3.0)
write_wav("game_over.wav", go_samples)

# --- heartbeat.wav --- two-pulse lub-dub pattern, low and thumpy
hb_samples = []
# Lub (stronger, lower)
lub_n = int(SAMPLE_RATE * 0.08)
for i in range(lub_n):
    t = i / SAMPLE_RATE
    env = (1.0 - i / lub_n) ** 1.5
    hb_samples.append(math.sin(2 * math.pi * 50 * t) * 0.5 * env)
# Brief silence between lub and dub
hb_samples.extend([0.0] * int(SAMPLE_RATE * 0.06))
# Dub (softer, slightly higher)
dub_n = int(SAMPLE_RATE * 0.06)
for i in range(dub_n):
    t = i / SAMPLE_RATE
    env = (1.0 - i / dub_n) ** 2.0
    hb_samples.append(math.sin(2 * math.pi * 65 * t) * 0.35 * env)
# Tail silence for looping
hb_samples.extend([0.0] * int(SAMPLE_RATE * 0.1))
hb_samples = distort(hb_samples, 1.5)
hb_samples = lowpass(hb_samples, 200)
hb_samples = compress(hb_samples, threshold=0.2, ratio=3.0)
write_wav("heartbeat.wav", hb_samples)

print("Done!")
