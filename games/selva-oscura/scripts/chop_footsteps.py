#!/usr/bin/env python3
"""Chop a source audio file containing N footsteps into N variation
files for the in-game footstep SFX rotation.

Inputs:
  --source PATH    source audio (any ffmpeg-readable format)
  --out-dir PATH   output directory for the chopped variations
  --prefix NAME    output filename prefix (e.g. footstep_walk -> footstep_walk_1.ogg, ...)
  --max-variations N  cap on how many slices to emit (default: 10)

The chopper runs simple amplitude-based onset detection: it computes
RMS over a short rolling window, finds local maxima above a noise
threshold, and slices ~window_seconds around each peak. Slices are
fade-in/fade-out applied to suppress clicks at the cut points.

Output is ogg/vorbis (the engine's preferred SFX format).

Requires ffmpeg in PATH. Reuses the same pattern as
games/prison-escape-game/scripts/audio_fx.py (decode_to_wav,
read_wav, write_wav, encode_to_ogg) - prison-escape's helpers are
inlined here rather than imported because selva-oscura should not
take a runtime dependency on prison-escape's scripts directory.
"""

import argparse
import math
import os
import struct
import subprocess
import sys
import tempfile
import wave
from pathlib import Path


# ---------------------------------------------------------------------------
# I/O helpers (inlined from prison-escape/scripts/audio_fx.py)
# ---------------------------------------------------------------------------


def decode_to_wav(input_path, wav_path):
    """Decode any ffmpeg-supported format to 16-bit PCM WAV."""
    subprocess.run(
        ["ffmpeg", "-y", "-i", input_path, "-acodec", "pcm_s16le", wav_path],
        check=True,
        capture_output=True,
    )


def encode_to_ogg(wav_path, ogg_path, quality=5):
    """Encode WAV to OGG Vorbis. quality 0 = lowest, 10 = highest."""
    subprocess.run(
        [
            "ffmpeg",
            "-y",
            "-i",
            wav_path,
            "-acodec",
            "libvorbis",
            "-q:a",
            str(quality),
            ogg_path,
        ],
        check=True,
        capture_output=True,
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
        wf.writeframes(struct.pack("<" + "h" * len(samples), *samples))


# ---------------------------------------------------------------------------
# Onset detection
# ---------------------------------------------------------------------------


def rms_window(samples, channels, window_size):
    """Compute RMS over rolling windows. Returns one value per window.

    samples is interleaved mono or stereo PCM ints.
    window_size is in frames (not samples - one frame = `channels` samples).
    """
    rms = []
    frames = len(samples) // channels
    for start in range(0, frames - window_size, window_size):
        s = 0.0
        for i in range(window_size):
            for c in range(channels):
                v = samples[(start + i) * channels + c]
                s += v * v
        rms.append(math.sqrt(s / (window_size * channels)))
    return rms


def find_onsets(rms, threshold_ratio, min_gap_windows):
    """Find indices into rms[] where a peak rises above threshold_ratio*
    the running median, AND is local-maximum within +/- min_gap_windows.

    Returns a sorted list of window indices.
    """
    if not rms:
        return []
    sorted_rms = sorted(rms)
    median = sorted_rms[len(sorted_rms) // 2]
    threshold = median * threshold_ratio

    onsets = []
    last_onset = -min_gap_windows
    for i in range(1, len(rms) - 1):
        if rms[i] < threshold:
            continue
        # Local maximum: higher than both neighbors.
        if rms[i] <= rms[i - 1] or rms[i] <= rms[i + 1]:
            continue
        # Respect minimum gap from the previous accepted onset.
        if i - last_onset < min_gap_windows:
            # Replace previous if this one is louder.
            if onsets and rms[i] > rms[onsets[-1]]:
                onsets[-1] = i
                last_onset = i
            continue
        onsets.append(i)
        last_onset = i
    return onsets


# ---------------------------------------------------------------------------
# Slice extraction + fade
# ---------------------------------------------------------------------------


def slice_with_fade(samples, channels, start_frame, end_frame, fade_frames):
    """Extract samples[start_frame*channels : end_frame*channels] and
    apply a linear fade-in over the first fade_frames and a linear
    fade-out over the last fade_frames. Returns a new list of ints.
    """
    out = []
    length = end_frame - start_frame
    for i in range(length):
        gain = 1.0
        if i < fade_frames:
            gain = i / float(fade_frames)
        elif i > length - fade_frames:
            gain = (length - i) / float(fade_frames)
        for c in range(channels):
            idx = (start_frame + i) * channels + c
            v = int(samples[idx] * gain)
            # Clamp to int16 range.
            if v > 32767:
                v = 32767
            elif v < -32768:
                v = -32768
            out.append(v)
    return out


# ---------------------------------------------------------------------------
# Main
# ---------------------------------------------------------------------------


def chop(source_path, out_dir, prefix, max_variations, slice_seconds,
         pre_onset_seconds, threshold_ratio, min_gap_seconds):
    """Detect onsets in source_path and emit individual ogg variations."""
    src = Path(source_path)
    if not src.exists():
        sys.exit(f"[chop] source not found: {src}")

    out_dir = Path(out_dir)
    out_dir.mkdir(parents=True, exist_ok=True)

    with tempfile.TemporaryDirectory() as tmp:
        tmp_wav = os.path.join(tmp, "source.wav")
        decode_to_wav(str(src), tmp_wav)
        samples, rate, channels, sampwidth = read_wav(tmp_wav)
        frames = len(samples) // channels
        duration = frames / rate
        print(f"[chop] loaded {src.name}: {duration:.2f}s, {rate}Hz, {channels}ch")

        # Onset detection: ~20ms windows, configurable threshold + min gap.
        window_size = max(1, rate // 50)  # 20ms windows
        rms = rms_window(samples, channels, window_size)
        windows_per_second = rate / window_size
        min_gap_windows = max(1, int(min_gap_seconds * windows_per_second))
        onsets = find_onsets(rms, threshold_ratio, min_gap_windows)
        print(f"[chop] detected {len(onsets)} onsets "
              f"(threshold_ratio={threshold_ratio}, min_gap={min_gap_seconds}s)")

        if not onsets:
            sys.exit("[chop] no onsets detected; adjust threshold_ratio or min_gap_seconds")

        # Emit variations.
        slice_frames = int(slice_seconds * rate)
        pre_frames = int(pre_onset_seconds * rate)
        fade_frames = max(1, int(0.01 * rate))  # 10ms fade

        emitted = 0
        for i, onset_window in enumerate(onsets):
            if emitted >= max_variations:
                break
            onset_frame = onset_window * window_size
            start_frame = max(0, onset_frame - pre_frames)
            end_frame = min(frames, start_frame + slice_frames)
            if end_frame - start_frame < fade_frames * 2:
                continue
            sliced = slice_with_fade(samples, channels, start_frame, end_frame, fade_frames)
            slice_wav = os.path.join(tmp, f"slice_{i}.wav")
            write_wav(slice_wav, sliced, rate, channels, sampwidth)
            ogg_out = out_dir / f"{prefix}_{emitted + 1}.ogg"
            encode_to_ogg(slice_wav, str(ogg_out))
            t = onset_frame / rate
            print(f"[chop] {ogg_out.name}: onset@{t:.2f}s, "
                  f"slice={start_frame / rate:.2f}-{end_frame / rate:.2f}s")
            emitted += 1

        print(f"[chop] wrote {emitted} variations to {out_dir}")

    # Peak-normalize the emitted slices so every SFX bank ends up at
    # the same headroom regardless of source clip loudness. Without
    # this, banks chopped from quieter sources play visibly softer
    # than louder banks even at the same audio.json `volume` setting.
    if emitted > 0:
        normalize_script = os.path.join(os.path.dirname(__file__), "normalize_audio.py")
        slice_paths = [str(out_dir / f"{prefix}_{i + 1}.ogg") for i in range(emitted)]
        subprocess.run(
            [sys.executable, normalize_script, "--files", *slice_paths, "--target-dbfs", "-1.0"],
            check=True,
        )


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--source", required=True, help="source audio file")
    ap.add_argument("--out-dir", required=True, help="output directory for variations")
    ap.add_argument("--prefix", required=True,
                    help="output filename prefix (e.g. footstep_walk)")
    ap.add_argument("--max-variations", type=int, default=10)
    ap.add_argument("--slice-seconds", type=float, default=0.35,
                    help="length of each emitted slice")
    ap.add_argument("--pre-onset-seconds", type=float, default=0.03,
                    help="seconds before the detected onset to include in the slice")
    ap.add_argument("--threshold-ratio", type=float, default=2.5,
                    help="onset detection threshold relative to median RMS")
    ap.add_argument("--min-gap-seconds", type=float, default=0.25,
                    help="minimum seconds between detected onsets")
    args = ap.parse_args()
    chop(args.source, args.out_dir, args.prefix, args.max_variations,
         args.slice_seconds, args.pre_onset_seconds, args.threshold_ratio,
         args.min_gap_seconds)


if __name__ == "__main__":
    main()
