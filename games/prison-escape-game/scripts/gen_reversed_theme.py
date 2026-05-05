#!/usr/bin/env python3
"""Generate processed main menu theme variants.

Produces two files from the original main_menu.ogg:
  - main_menu_processed.ogg  (same DSP chain, NOT reversed)
  - main_menu_reversed.ogg   (reversed + same DSP chain)

Usage:
    python gen_reversed_theme.py
"""

import os
import sys
import math
import tempfile

# Add scripts dir to path so we can import audio_fx.
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import audio_fx


def reverse(samples, channels):
    """Reverse audio while preserving channel interleaving."""
    frames = len(samples) // channels
    out = []
    for i in range(frames - 1, -1, -1):
        for c in range(channels):
            out.append(samples[i * channels + c])
    return out


def feedback_delay(samples, channels, rate, delay_ms=400, feedback=0.45, mix=0.35):
    """Multi-tap feedback delay for a cavernous, eerie sound."""
    delay_samples = int(rate * delay_ms / 1000) * channels
    out = list(samples)  # copy

    # First tap -- long delay
    for i in range(delay_samples, len(out)):
        echo = out[i - delay_samples]
        out[i] = int(out[i] + echo * feedback)

    # Second tap at 2/3 delay for width
    delay2 = int(delay_samples * 2 // 3)
    for i in range(delay2, len(out)):
        echo = out[i - delay2]
        out[i] = int(out[i] + echo * feedback * 0.5)

    # Third tap at 1/3 delay for early reflections (lush density)
    delay3 = int(delay_samples // 3)
    for i in range(delay3, len(out)):
        echo = out[i - delay3]
        out[i] = int(out[i] + echo * feedback * 0.3)

    # Fourth tap at 1.5x delay for tail
    delay4 = int(delay_samples * 3 // 2)
    if delay4 < len(out):
        for i in range(delay4, len(out)):
            echo = out[i - delay4]
            out[i] = int(out[i] + echo * feedback * 0.25)

    # Mix with dry signal
    result = []
    for i in range(len(samples)):
        dry = samples[i]
        wet = out[i]
        result.append(int(dry * (1.0 - mix) + wet * mix))
    return result


def highpass(samples, channels, rate, cutoff_hz=200):
    """Single-pole high-pass filter to cut muddy lows/low-mids."""
    rc = 1.0 / (2.0 * math.pi * cutoff_hz)
    dt = 1.0 / rate
    alpha = rc / (rc + dt)
    out = list(samples)
    for c in range(channels):
        prev_in = 0.0
        prev_out = 0.0
        for i in range(c, len(samples), channels):
            cur_in = samples[i]
            prev_out = alpha * (prev_out + cur_in - prev_in)
            prev_in = cur_in
            out[i] = int(max(-32767, min(32767, prev_out)))
    return out


def apply_dsp(samples, rate, channels):
    """Apply the shared DSP chain (everything except reverse)."""
    # Downsample
    print("  Downsampling (2x)...")
    samples = audio_fx.downsample(samples, channels, factor=2)
    new_rate = rate // 2

    # Feedback delay
    print("  Adding delay (500ms, 0.55 feedback, 0.65 wet)...")
    samples = feedback_delay(samples, channels, new_rate,
                             delay_ms=500, feedback=0.55, mix=0.65)

    # High-pass filter
    print("  High-pass filtering (200 Hz)...")
    samples = highpass(samples, channels, new_rate, cutoff_hz=200)

    # Bitcrush
    print("  Bitcrushing (12 bits)...")
    samples = audio_fx.bit_crush(samples, bits=12)

    # Soft-clip
    print("  Soft clipping (drive=1.2)...")
    samples = audio_fx.soft_clip(samples, drive=1.2)

    # Compress + normalize
    print("  Compressing and normalizing...")
    samples = audio_fx.compress(samples, threshold=0.25, ratio=4.0)
    samples = audio_fx.normalize(samples, target=0.7)
    samples = audio_fx.clamp(samples)

    return samples, new_rate


def generate_variant(input_samples, rate, channels, sampwidth, output_path, do_reverse):
    """Generate one variant (reversed or not)."""
    samples = list(input_samples)

    if do_reverse:
        print("  Reversing...")
        samples = reverse(samples, channels)

    samples, new_rate = apply_dsp(samples, rate, channels)

    tmp_wav = tempfile.mktemp(suffix=".wav")
    try:
        audio_fx.write_wav(tmp_wav, samples, new_rate, channels, sampwidth)
        audio_fx.encode_to_ogg(tmp_wav, output_path, quality=2)
    finally:
        if os.path.exists(tmp_wav):
            os.remove(tmp_wav)

    final_frames = len(samples) // channels
    print(f"  -> {final_frames/new_rate:.1f}s output -> {output_path}")


def main():
    script_dir = os.path.dirname(os.path.abspath(__file__))
    repo_root = os.path.dirname(os.path.dirname(script_dir))
    music_dir = os.path.join(repo_root, "game", "assets", "music")
    input_path = os.path.join(music_dir, "main_menu.ogg")

    if not os.path.exists(input_path):
        print(f"ERROR: Input not found: {input_path}")
        sys.exit(1)

    # Decode source once
    tmp_wav = tempfile.mktemp(suffix=".wav")
    try:
        print(f"Decoding {input_path}...")
        audio_fx.decode_to_wav(input_path, tmp_wav)
        samples, rate, channels, sampwidth = audio_fx.read_wav(tmp_wav)
        frames = len(samples) // channels
        print(f"  {frames} frames, {rate} Hz, {channels} ch, {frames/rate:.1f}s")
    finally:
        if os.path.exists(tmp_wav):
            os.remove(tmp_wav)

    # Generate non-reversed variant
    print("\n--- Processed (forward) ---")
    generate_variant(samples, rate, channels, sampwidth,
                     os.path.join(music_dir, "main_menu_processed.ogg"), False)

    # Generate reversed variant
    print("\n--- Reversed ---")
    generate_variant(samples, rate, channels, sampwidth,
                     os.path.join(music_dir, "main_menu_reversed.ogg"), True)

    print("\nDone!")


if __name__ == "__main__":
    main()
