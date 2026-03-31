"""Reusable audio DSP effects for processing game sound files.

All functions operate on lists of 16-bit signed integer samples.
Multi-channel audio is interleaved: [L0, R0, L1, R1, ...].
"""

import math
import struct
import subprocess
import tempfile
import os
import wave


# ---------------------------------------------------------------------------
# I/O helpers
# ---------------------------------------------------------------------------

def decode_to_wav(input_path, wav_path):
    """Decode any ffmpeg-supported format to 16-bit PCM WAV."""
    subprocess.run(
        ["ffmpeg", "-y", "-i", input_path, "-acodec", "pcm_s16le", wav_path],
        check=True, capture_output=True
    )


def encode_to_ogg(wav_path, ogg_path, quality=0):
    """Encode WAV to OGG Vorbis. quality 0 = lowest, 10 = highest."""
    subprocess.run(
        ["ffmpeg", "-y", "-i", wav_path, "-acodec", "libvorbis",
         "-q:a", str(quality), ogg_path],
        check=True, capture_output=True
    )


def read_wav(path):
    """Read WAV file, return (samples, rate, channels, sampwidth)."""
    with wave.open(path, "rb") as wf:
        rate = wf.getframerate()
        channels = wf.getnchannels()
        sampwidth = wf.getsampwidth()
        n = wf.getnframes()
        raw = wf.readframes(n)
    fmt = "<" + "h" * (n * channels)
    samples = list(struct.unpack(fmt, raw))
    return samples, rate, channels, sampwidth


def write_wav(path, samples, rate, channels, sampwidth):
    """Write samples to a WAV file."""
    with wave.open(path, "wb") as wf:
        wf.setnchannels(channels)
        wf.setsampwidth(sampwidth)
        wf.setframerate(rate)
        fmt = "<" + "h" * len(samples)
        wf.writeframes(struct.pack(fmt, *samples))


# ---------------------------------------------------------------------------
# DSP effects
# ---------------------------------------------------------------------------

def bit_crush(samples, bits=6):
    """Reduce bit depth for a gritty lo-fi sound."""
    step = 65536 // (1 << bits)
    return [(s // step) * step for s in samples]


def downsample(samples, channels, factor=3):
    """Decimate sample rate by an integer factor (creates aliasing = grit)."""
    in_frames = len(samples) // channels
    out = []
    for i in range(0, in_frames, factor):
        for c in range(channels):
            out.append(samples[i * channels + c])
    return out


def soft_clip(samples, drive=2.0):
    """Soft-clip distortion using tanh."""
    out = []
    for s in samples:
        x = (s / 32768.0) * drive
        y = math.tanh(x)
        out.append(int(y * 32767))
    return out


def compress(samples, threshold=0.3, ratio=4.0):
    """Simple dynamic range compression -- squash peaks, bring up body."""
    out = []
    for s in samples:
        x = s / 32768.0
        sign = 1.0 if x >= 0 else -1.0
        level = abs(x)
        if level > threshold:
            over = level - threshold
            level = threshold + over / ratio
        out.append(int(sign * level * 32768))
    return out


def pitch_down(samples, channels, factor=0.7):
    """Pitch down by resampling (stretch in time, keeping sample rate).
    factor < 1.0 = lower pitch."""
    in_frames = len(samples) // channels
    out_frames = int(in_frames / factor)
    out = []
    for i in range(out_frames):
        src = i * factor
        idx = int(src)
        frac = src - idx
        for c in range(channels):
            s0 = samples[min(idx, in_frames - 1) * channels + c]
            s1 = samples[min(idx + 1, in_frames - 1) * channels + c]
            out.append(int(s0 + (s1 - s0) * frac))
    return out


def normalize(samples, target=0.9):
    """Normalize peak to target (0-1)."""
    peak = max(abs(s) for s in samples) if samples else 1
    if peak == 0:
        return samples
    scale = (32767 * target) / peak
    return [int(s * scale) for s in samples]


def clamp(samples):
    """Clamp all samples to valid 16-bit range."""
    return [max(-32767, min(32767, s)) for s in samples]


# ---------------------------------------------------------------------------
# High-level processing
# ---------------------------------------------------------------------------

def process_file(input_path, output_path=None, bits=6, downsample_factor=3,
                 drive=2.5, comp_threshold=0.3, comp_ratio=4.0,
                 norm_target=0.9, ogg_quality=0):
    """Apply the full grit chain to an audio file.

    Pipeline: downsample -> bit-crush -> compress -> soft-clip -> normalize.
    Input can be any ffmpeg-supported format. Output is OGG.
    If output_path is None, overwrites the input file.
    Returns (duration, output_rate).
    """
    if output_path is None:
        output_path = input_path

    with tempfile.NamedTemporaryFile(suffix=".wav", delete=False) as tmp:
        tmp_wav = tmp.name

    try:
        decode_to_wav(input_path, tmp_wav)
        samples, rate, channels, sampwidth = read_wav(tmp_wav)

        if downsample_factor > 1:
            samples = downsample(samples, channels, factor=downsample_factor)
            rate = rate // downsample_factor

        if bits < 16:
            samples = bit_crush(samples, bits=bits)

        samples = compress(samples, threshold=comp_threshold, ratio=comp_ratio)
        samples = soft_clip(samples, drive=drive)
        samples = normalize(samples, target=norm_target)
        samples = clamp(samples)

        write_wav(tmp_wav, samples, rate, channels, sampwidth)
        encode_to_ogg(tmp_wav, output_path, quality=ogg_quality)

        duration = len(samples) / channels / rate
        return duration, rate

    finally:
        if os.path.exists(tmp_wav):
            os.remove(tmp_wav)
