"""Generate placeholder .ogg sound effects for the game.

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
import subprocess
import tempfile

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
    """Normalize, write temp WAV, encode to OGG Vorbis, delete temp WAV.
    Accepts .ogg or .ogg filename -- always outputs .ogg."""
    ogg_name = os.path.splitext(filename)[0] + ".ogg"
    samples = normalize(samples)
    num = len(samples)
    data_size = num * 2

    fd, tmp_wav = tempfile.mkstemp(suffix=".ogg")
    os.close(fd)
    try:
        with open(tmp_wav, "wb") as f:
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
        ogg_path = os.path.join(OUTPUT_DIR, ogg_name)
        subprocess.run(
            ["ffmpeg", "-y", "-i", tmp_wav, "-acodec", "libvorbis", "-q:a", "2", ogg_path],
            check=True, capture_output=True,
        )
    finally:
        if os.path.exists(tmp_wav):
            os.remove(tmp_wav)
    print(f"  {ogg_name} ({num} samples, {num / SAMPLE_RATE:.2f}s)")


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


def bitcrush(samples, bits=8, downsample=1, mix=0.3):
    """Lo-fi effect: reduce bit depth and/or sample rate.
    bits=16 is CD, 8=retro, 4=heavy. downsample=N holds every Nth sample."""
    levels = 2 ** (bits - 1)
    held = 0.0
    crushed = []
    for i, s in enumerate(samples):
        if i % downsample == 0:
            held = math.floor(s * levels) / levels
        crushed.append(held)
    return [d * (1 - mix) + c * mix for d, c in zip(samples, crushed)]


def chorus(samples, voices=3, depth_ms=5.0, rate_hz=1.5, mix=0.5):
    """Digital chorus: multiple delayed copies with LFO-modulated delay times.
    depth_ms = max delay offset, rate_hz = LFO speed, mix = wet/dry blend."""
    max_delay = int(SAMPLE_RATE * depth_ms / 1000.0)
    base_delay = max_delay + 1
    n = len(samples)
    wet = [0.0] * n
    for v in range(voices):
        phase_offset = v * (2 * math.pi / voices)
        for i in range(n):
            t = i / SAMPLE_RATE
            lfo = math.sin(2 * math.pi * rate_hz * t + phase_offset)
            delay = base_delay + int(lfo * max_delay)
            src = i - delay
            if src >= 0:
                wet[i] += samples[src]
    voice_gain = 1.0 / voices
    return [d * (1 - mix) + w * voice_gain * mix for d, w in zip(samples, wet)]


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

# --- swing_miss.ogg --- comical whistle whoosh for swings that miss
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
write_wav("swing_miss.ogg", miss_samples)

# --- attack.ogg --- sword swing whoosh
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
write_wav("attack.ogg", attack_samples)

# --- skill.ogg --- rising power chord + distorted impact
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
write_wav("skill.ogg", skill_samples)

# --- hit_N.ogg --- punchy noise impact with distortion (4 variations to avoid phasing)
for vi in range(4):
    h = noise_burst(0.12, 0.5, seed=42 + vi)
    h = distort(h, 3.0)
    h = lowpass(h, 3000)
    h = compress(h, threshold=0.3, ratio=4.0)
    h = reverb(h, delay_ms=20, feedback=0.15, mix=0.1)
    write_wav(f"hit_{vi + 1}.ogg", h)

# --- dodge.ogg --- rising sweep with subtle reverb
sweep = []
for i in range(int(SAMPLE_RATE * 0.15)):
    t = i / SAMPLE_RATE
    progress = i / (SAMPLE_RATE * 0.15)
    freq = 300 + 600 * progress
    env = 1.0 - progress
    sweep.append(math.sin(2 * math.pi * freq * t) * 0.3 * env)
sweep = reverb(sweep, delay_ms=30, feedback=0.2, mix=0.2)
write_wav("dodge.ogg", sweep)

# --- parry.ogg --- metallic ping with ring reverb
parry = tone(880, 0.08, 0.5) + tone(1760, 0.06, 0.3)
parry = reverb(parry, delay_ms=35, feedback=0.4, mix=0.3)
parry = highpass(parry, 400)
write_wav("parry.ogg", parry)

# --- death.ogg --- descending tone with lowpass + gentle reverb
death = []
for i in range(int(SAMPLE_RATE * 0.3)):
    t = i / SAMPLE_RATE
    progress = i / (SAMPLE_RATE * 0.3)
    freq = 400 - 300 * progress
    env = 1.0 - progress
    death.append(math.sin(2 * math.pi * freq * t) * 0.4 * env)
death = lowpass(death, 2000)
death = reverb(death, delay_ms=30, feedback=0.15, mix=0.15)
write_wav("death.ogg", death)

# --- pickup.ogg --- rising chime with sparkle reverb
pickup = tone(660, 0.06, 0.3) + tone(880, 0.08, 0.3)
pickup = reverb(pickup, delay_ms=45, feedback=0.3, mix=0.25)
write_wav("pickup.ogg", pickup)

# --- levelup.ogg --- triumphant two-note with overdrive + reverb
levelup = tone(440, 0.15, 0.4) + tone(660, 0.2, 0.4)
levelup = distort(levelup, 1.3)
levelup = reverb(levelup, delay_ms=80, feedback=0.35, mix=0.3)
write_wav("levelup.ogg", levelup)

# --- stat_allocate.ogg --- crisp UI confirmation click
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
write_wav("stat_allocate.ogg", alloc_samples)

# --- wall_bump.ogg --- dull thud with distortion
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
write_wav("wall_bump.ogg", bump_samples)

# --- footstep_walk.ogg --- soft tap (minimal effects to stay subtle)
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
write_wav("footstep_walk.ogg", walk_samples)

# --- footstep_run.ogg --- quick bright tap
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
write_wav("footstep_run.ogg", run_samples)

# --- rest_heal variations --- choir pad with melancholic chord variations
# Each rest heals plays a randomly selected variation for variety.

def resonator(samples, freq_hz, decay=0.985, mix=0.15):
    delay_n = max(1, int(SAMPLE_RATE / freq_hz))
    buf = [0.0] * delay_n
    out = list(samples)
    for i in range(len(out)):
        idx = i % delay_n
        buf[idx] = buf[idx] * decay + out[i] * (1.0 - decay)
        out[i] = out[i] * (1.0 - mix) + buf[idx] * mix
    return out

def gen_heal_variation(filename, voices, res_freqs, seed):
    random.seed(seed)
    dur = 2.5
    n = int(SAMPLE_RATE * dur)
    samples = [0.0] * n
    for base, num_v, amp in voices:
        for _ in range(num_v):
            offset = (random.random() - 0.5) * 4.0
            freq = base + offset
            phase0 = random.random() * 2 * math.pi
            for i in range(n):
                t = i / SAMPLE_RATE
                if t < 0.8:
                    env = (t / 0.8) ** 0.5
                elif t > dur - 1.0:
                    env = ((dur - t) / 1.0) ** 0.7
                else:
                    env = 1.0
                s = math.sin(2 * math.pi * freq * t + phase0) * amp
                s += math.sin(2 * math.pi * freq * 2 * t + phase0) * amp * 0.3
                samples[i] += s * env
    samples = highpass(samples, 120)
    samples = lowpass(samples, 5500)
    samples = lowpass(samples, 5500)
    samples = normalize(samples, target=0.25)
    for rf, decay, mix in res_freqs:
        samples = resonator(samples, rf, decay=decay, mix=mix)
    samples = bitcrush(samples, bits=10, downsample=3, mix=0.25)
    samples = reverb(samples, delay_ms=150, feedback=0.55, mix=0.7)
    samples = reverb(samples, delay_ms=63, feedback=0.4, mix=0.5)
    samples = normalize(samples, target=0.30)
    write_wav(filename, samples)

# All variations are clean major 7th or minor 7th chords (+ 9th) with no
# augmented intervals, tritones, or tension tones.  Resonator frequencies
# are octave-up chord tones to reinforce, not fight, the harmony.

# Variation 1: C#maj9 -- root position (warm, bright)
gen_heal_variation("rest_heal_1.ogg", [
    (138.59, 8, 0.025),  # C#3 root
    (174.61, 8, 0.025),  # F3 (E#3) major 3rd
    (207.65, 8, 0.025),  # G#3 perfect 5th
    (261.63, 6, 0.020),  # C4 (B#3) major 7th
    (311.13, 4, 0.014),  # D#4 9th
], [(277.18, 0.993, 0.25), (349.23, 0.991, 0.20), (415.30, 0.989, 0.15)], seed=42)

# Variation 2: F#m9 -- root position (mellow, smooth)
gen_heal_variation("rest_heal_2.ogg", [
    (185.00, 8, 0.025),  # F#3 root
    (220.00, 8, 0.025),  # A3 minor 3rd
    (277.18, 8, 0.025),  # C#4 perfect 5th
    (329.63, 6, 0.020),  # E4 minor 7th
    (415.30, 4, 0.014),  # G#4 9th
], [(369.99, 0.993, 0.25), (440.00, 0.991, 0.20), (523.25, 0.989, 0.15)], seed=43)

# Variation 3: Abmaj9 -- root position (rich, deep)
gen_heal_variation("rest_heal_3.ogg", [
    (103.83, 8, 0.025),  # Ab2 root
    (130.81, 8, 0.025),  # C3 major 3rd
    (155.56, 8, 0.025),  # Eb3 perfect 5th
    (196.00, 6, 0.020),  # G3 major 7th
    (233.08, 4, 0.014),  # Bb3 9th
], [(207.65, 0.993, 0.25), (261.63, 0.991, 0.20), (311.13, 0.989, 0.15)], seed=44)

# Variation 4: Ebm9 -- root position (dark, gentle)
gen_heal_variation("rest_heal_4.ogg", [
    (155.56, 8, 0.025),  # Eb3 root
    (185.00, 8, 0.025),  # Gb3 minor 3rd
    (233.08, 8, 0.025),  # Bb3 perfect 5th
    (277.18, 6, 0.020),  # Db4 minor 7th
    (349.23, 4, 0.014),  # F4 9th
], [(311.13, 0.993, 0.25), (369.99, 0.991, 0.20), (466.16, 0.989, 0.15)], seed=45)

# Variation 5: Bmaj9 -- root position (shimmery, warm)
gen_heal_variation("rest_heal_5.ogg", [
    (123.47, 8, 0.025),  # B2 root
    (155.56, 8, 0.025),  # D#3 major 3rd
    (185.00, 8, 0.025),  # F#3 perfect 5th
    (233.08, 6, 0.020),  # A#3 major 7th
    (277.18, 4, 0.014),  # C#4 9th
], [(246.94, 0.993, 0.25), (311.13, 0.991, 0.20), (369.99, 0.989, 0.15)], seed=46)

# Variation 6: Bbm9 -- root position (warm, dark)
gen_heal_variation("rest_heal_6.ogg", [
    (116.54, 8, 0.025),  # Bb2 root
    (138.59, 8, 0.025),  # Db3 minor 3rd
    (174.61, 8, 0.025),  # F3 perfect 5th
    (207.65, 6, 0.020),  # Ab3 minor 7th
    (261.63, 4, 0.014),  # C4 9th
], [(233.08, 0.993, 0.25), (277.18, 0.991, 0.20), (349.23, 0.989, 0.15)], seed=47)

# --- game_over.ogg --- dramatic low descending dissonance with heavy distortion
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
write_wav("game_over.ogg", go_samples)

# --- heartbeat.ogg --- two-pulse lub-dub pattern, low and thumpy
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
write_wav("heartbeat.ogg", hb_samples)

# --- ui_click.ogg --- soft rounded pop for menu interactions
click_n = int(SAMPLE_RATE * 0.06)
click_samples = []
for i in range(click_n):
    t = i / SAMPLE_RATE
    progress = i / click_n
    # Soft attack + gentle decay for a warm pop feel
    if progress < 0.1:
        env = progress / 0.1
    else:
        env = (1.0 - progress) ** 1.5
    # Lower fundamental (warmer), soft harmonic
    s = math.sin(2 * math.pi * 900 * t) * 0.25 * env
    s += math.sin(2 * math.pi * 1350 * t) * 0.08 * env
    click_samples.append(s)
click_samples = lowpass(click_samples, 3000)
click_samples = reverb(click_samples, delay_ms=20, feedback=0.15, mix=0.1)
write_wav("ui_click.ogg", click_samples)

# --- wave_clear.ogg --- warbling teleport whoosh with rising shimmer
wc_dur = 1.2
wc_n = int(SAMPLE_RATE * wc_dur)
wc_samples = []
for i in range(wc_n):
    t = i / SAMPLE_RATE
    progress = i / wc_n
    # Bell curve envelope: swell up then fade
    env = math.sin(math.pi * progress) ** 0.7
    # Rising warble: base frequency sweeps up with vibrato
    base_freq = 300 + 800 * progress
    vibrato = math.sin(2 * math.pi * 6 * t) * 40
    freq = base_freq + vibrato
    s = math.sin(2 * math.pi * freq * t) * 0.3 * env
    # Shimmery octave harmonic
    s += math.sin(2 * math.pi * freq * 2 * t) * 0.12 * env
    # High sparkle overtone
    s += math.sin(2 * math.pi * freq * 3 * t) * 0.05 * env
    wc_samples.append(s)
wc_samples = chorus(wc_samples, voices=4, depth_ms=8.0, rate_hz=1.5, mix=0.85)
wc_samples = reverb(wc_samples, delay_ms=80, feedback=0.45, mix=0.4)
wc_samples = compress(wc_samples, threshold=0.3, ratio=3.0)
write_wav("wave_clear.ogg", wc_samples)

# --- ladder_appear.ogg --- low stone rumble with rising tone (something emerging)
la_dur = 0.8
la_n = int(SAMPLE_RATE * la_dur)
random.seed(777)
la_noise = [(random.random() * 2 - 1) for _ in range(la_n)]
la_samples = []
la_lp = 0.0
for i in range(la_n):
    t = i / SAMPLE_RATE
    progress = i / la_n
    # Envelope: quick attack, sustain, fade
    if progress < 0.05:
        env = progress / 0.05
    elif progress < 0.6:
        env = 1.0
    else:
        env = (1.0 - progress) / 0.4
    # Low rumble: filtered noise
    cutoff = 150 + 200 * progress
    rc = 1.0 / (2.0 * math.pi * cutoff)
    dt_sample = 1.0 / SAMPLE_RATE
    alpha = dt_sample / (rc + dt_sample)
    la_lp += alpha * (la_noise[i] - la_lp)
    s = la_lp * 0.6 * env
    # Rising sub-bass tone (stone grinding)
    sub_freq = 60 + 40 * progress
    s += math.sin(2 * math.pi * sub_freq * t) * 0.3 * env
    # Mid-tone accent rising (something materializing)
    mid_freq = 200 + 300 * progress
    s += math.sin(2 * math.pi * mid_freq * t) * 0.1 * env * progress
    la_samples.append(s)
la_samples = distort(la_samples, drive=1.5)
la_samples = reverb(la_samples, delay_ms=60, feedback=0.35, mix=0.3)
la_samples = compress(la_samples, threshold=0.3, ratio=3.0)
write_wav("ladder_appear.ogg", la_samples)

# --- gunshot.ogg --- sharp percussive crack with low-end thump
random.seed(200)
gun_dur = 0.15
gun_n = int(SAMPLE_RATE * gun_dur)
gun_noise = [(random.random() * 2 - 1) for _ in range(gun_n)]
gun_samples = []
gun_lp = 0.0
for i in range(gun_n):
    t = i / SAMPLE_RATE
    progress = i / gun_n
    # Sharp attack, fast decay
    if progress < 0.02:
        env = progress / 0.02
    else:
        env = (1.0 - progress) ** 2.5
    # Sub-bass thump
    thump = math.sin(2 * math.pi * 60 * t) * 0.4 * env
    # Crack: bright filtered noise
    cutoff = 6000 - 4000 * progress
    rc = 1.0 / (2.0 * math.pi * cutoff)
    dt_sample = 1.0 / SAMPLE_RATE
    alpha = dt_sample / (rc + dt_sample)
    gun_lp += alpha * (gun_noise[i] - gun_lp)
    crack = gun_lp * 0.7 * env
    # Metallic ring
    ring = math.sin(2 * math.pi * 2200 * t) * 0.1 * (env ** 2)
    gun_samples.append(thump + crack + ring)
gun_samples = distort(gun_samples, 2.5)
gun_samples = compress(gun_samples, threshold=0.25, ratio=4.0)
gun_samples = reverb(gun_samples, delay_ms=30, feedback=0.2, mix=0.2)
write_wav("weapons/gunshot.ogg", gun_samples)

# --- gunshot_semi.ogg --- lighter, snappier semi-auto shot
random.seed(210)
semi_dur = 0.12
semi_n = int(SAMPLE_RATE * semi_dur)
semi_noise = [(random.random() * 2 - 1) for _ in range(semi_n)]
semi_samples = []
semi_lp = 0.0
for i in range(semi_n):
    t = i / SAMPLE_RATE
    progress = i / semi_n
    if progress < 0.015:
        env = progress / 0.015
    else:
        env = (1.0 - progress) ** 3.0
    thump = math.sin(2 * math.pi * 80 * t) * 0.3 * env
    cutoff = 7000 - 5000 * progress
    rc = 1.0 / (2.0 * math.pi * cutoff)
    dt_sample = 1.0 / SAMPLE_RATE
    alpha = dt_sample / (rc + dt_sample)
    semi_lp += alpha * (semi_noise[i] - semi_lp)
    snap = semi_lp * 0.6 * env
    ping = math.sin(2 * math.pi * 3000 * t) * 0.08 * (env ** 2)
    semi_samples.append(thump + snap + ping)
semi_samples = distort(semi_samples, 2.0)
semi_samples = compress(semi_samples, threshold=0.25, ratio=4.0)
semi_samples = reverb(semi_samples, delay_ms=20, feedback=0.15, mix=0.15)
write_wav("weapons/gunshot_semi.ogg", semi_samples)

# --- reload.ogg --- NOT generated here. Uses a real recorded sound.

# --- bow_release.ogg --- taut string snap with whooshing arrow
random.seed(230)
bow_dur = 0.2
bow_n = int(SAMPLE_RATE * bow_dur)
bow_noise = [(random.random() * 2 - 1) for _ in range(bow_n)]
bow_samples = []
bow_lp = 0.0
for i in range(bow_n):
    t = i / SAMPLE_RATE
    progress = i / bow_n
    # Instant attack, smooth decay
    if progress < 0.01:
        env = progress / 0.01
    else:
        env = (1.0 - progress) ** 1.5
    # String twang: descending resonance (bowstring vibrating)
    twang_freq = 350 - 150 * progress
    twang = math.sin(2 * math.pi * twang_freq * t) * 0.35 * env
    twang += math.sin(2 * math.pi * twang_freq * 2.3 * t) * 0.12 * env
    # Breathy whoosh: filtered noise fading in then out
    whoosh_env = math.sin(math.pi * progress) ** 0.7 * 0.3
    cutoff = 2000 + 1500 * progress
    rc = 1.0 / (2.0 * math.pi * cutoff)
    dt_sample = 1.0 / SAMPLE_RATE
    alpha = dt_sample / (rc + dt_sample)
    bow_lp += alpha * (bow_noise[i] - bow_lp)
    whoosh = bow_lp * whoosh_env
    bow_samples.append(twang + whoosh)
bow_samples = highpass(bow_samples, 200)
bow_samples = compress(bow_samples, threshold=0.3, ratio=3.0)
bow_samples = reverb(bow_samples, delay_ms=35, feedback=0.2, mix=0.15)
write_wav("weapons/bow_release.ogg", bow_samples)

print("Done!")
