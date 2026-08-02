#!/usr/bin/env python3
"""Peak-normalize a set of audio files in place. Each file is rescaled
independently so its loudest sample hits a configurable target dBFS.

Usage:
  python normalize_audio.py --files PATH [PATH ...] --target-dbfs -1.0

Why per-file peak normalize and not RMS / LUFS:
  * Footstep slices are short impulses, not sustained content - RMS
    normalize would push the impulse peaks into hard clip on samples
    that have any pre/post silence trimmed differently.
  * Peak normalize makes every slice "as loud as it can be without
    clipping" at the file level, then the audio.json volume entry
    scales the whole bank. That gives one place to tune (audio.json)
    and a clean per-file headroom guarantee.

Why -1 dBFS default (not 0):
  * Lossy codecs (vorbis, mp3) introduce inter-sample peaks that can
    exceed the original PCM peak after decoding. -1 dBFS headroom
    prevents post-decode intersample clipping in the engine.

Requires ffmpeg in PATH.
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


def decode_to_wav(input_path, wav_path):
    subprocess.run(
        ["ffmpeg", "-y", "-i", input_path, "-acodec", "pcm_s16le", wav_path],
        check=True,
        capture_output=True,
    )


def encode_to_ogg(wav_path, ogg_path, quality=5):
    subprocess.run(
        ["ffmpeg", "-y", "-i", wav_path, "-acodec", "libvorbis",
         "-q:a", str(quality), ogg_path],
        check=True,
        capture_output=True,
    )


def read_wav(path):
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
    with wave.open(path, "wb") as wf:
        wf.setnchannels(channels)
        wf.setsampwidth(sampwidth)
        wf.setframerate(rate)
        wf.writeframes(struct.pack("<" + "h" * len(samples), *samples))


def normalize_file(file_path, target_dbfs):
    """Peak-normalize a single audio file in place to target_dbfs."""
    path = Path(file_path)
    if not path.exists():
        print(f"[normalize] skip (not found): {path}")
        return

    with tempfile.TemporaryDirectory() as tmp:
        tmp_wav = os.path.join(tmp, "in.wav")
        out_wav = os.path.join(tmp, "out.wav")
        decode_to_wav(str(path), tmp_wav)
        samples, rate, channels, sampwidth = read_wav(tmp_wav)

        peak = max(abs(s) for s in samples) if samples else 0
        if peak == 0:
            print(f"[normalize] skip (silent): {path.name}")
            return

        target_peak = int(32767 * math.pow(10.0, target_dbfs / 20.0))
        gain = target_peak / peak
        normalized = [max(-32768, min(32767, int(s * gain))) for s in samples]

        write_wav(out_wav, normalized, rate, channels, sampwidth)

        ext = path.suffix.lower()
        if ext == ".ogg":
            encode_to_ogg(out_wav, str(path))
        elif ext == ".wav":
            import shutil
            shutil.copy(out_wav, path)
        else:
            sys.exit(f"[normalize] unsupported extension: {ext}")

        peak_db_before = 20.0 * math.log10(peak / 32767.0) if peak > 0 else -120.0
        print(f"[normalize] {path.name}: peak {peak_db_before:+.2f} dBFS "
              f"-> {target_dbfs:+.2f} dBFS (gain x{gain:.3f})")


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--files", nargs="+", required=True,
                    help="audio files to normalize in place")
    ap.add_argument("--target-dbfs", type=float, default=-1.0,
                    help="target peak in dBFS (negative; default -1)")
    args = ap.parse_args()
    for f in args.files:
        normalize_file(f, args.target_dbfs)


if __name__ == "__main__":
    main()
