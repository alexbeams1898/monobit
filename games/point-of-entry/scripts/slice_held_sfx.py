#!/usr/bin/env python3
"""Cut a recording of a HELD sound into the three files a held sound needs.

A trigger you hold cannot be one clip. Played as a one-shot it ends while the
trigger is still down; looped whole, its attack transient repeats and the thing
machine-guns. So it is three:

  <prefix>_start.ogg   the trigger pull -- the transient, played once
  <prefix>_loop.ogg    the body -- seamless, played until release
  <prefix>_stop.ogg    the release -- the dribble after the trigger comes up

THE LOOP'S CUTS SIT ON ZERO CROSSINGS. A loop spliced mid-waveform steps the
signal discontinuously every cycle, and that step is a click -- audible, and
worse the more times a second it happens. The start and stop are faded instead,
since nothing repeats them.

Output is 22050 Hz mono ogg/vorbis, the engine's SFX standard (see
prison-escape-game/docs/PERFORMANCE.md).

THE THREE ARE NORMALISED AS ONE, by a single gain taken from the loudest of
them -- NOT per file. They are one sound cut in three, and their relative levels
carry it: a release is quiet BECAUSE it is a dribble. Normalising each to the
same peak makes the dribble as loud as the spray, which is not a mix, it is
three unrelated noises played in order.

Helpers are inlined rather than imported from another game's scripts directory,
the same call selva-oscura made: one game should not need another game's tools
to build.

Requires ffmpeg in PATH.
"""

import argparse
import math
import os
import struct
import subprocess
import sys
import tempfile

SFX_RATE = 22050


def decode_to_wav(src, wav, rate):
    subprocess.run(["ffmpeg", "-y", "-hide_banner", "-loglevel", "error", "-i", src,
                    "-ar", str(rate), "-ac", "1", wav], check=True)


def encode_to_ogg(wav, ogg, quality):
    subprocess.run(["ffmpeg", "-y", "-hide_banner", "-loglevel", "error", "-i", wav,
                    "-ar", str(SFX_RATE), "-ac", "1", "-acodec", "libvorbis",
                    "-q:a", str(quality), ogg], check=True)


def read_wav(path):
    with open(path, "rb") as f:
        data = f.read()
    if data[:4] != b"RIFF" or data[8:12] != b"WAVE":
        raise SystemExit("not a RIFF/WAVE file: " + path)
    pos, rate, samples = 12, SFX_RATE, []
    while pos + 8 <= len(data):
        cid, size = data[pos:pos + 4], struct.unpack("<I", data[pos + 4:pos + 8])[0]
        body = data[pos + 8:pos + 8 + size]
        if cid == b"fmt ":
            rate = struct.unpack("<I", body[4:8])[0]
        elif cid == b"data":
            samples = list(struct.unpack("<%dh" % (len(body) // 2), body[:len(body) // 2 * 2]))
        pos += 8 + size + (size & 1)
    return samples, rate


def write_wav(path, samples, rate):
    body = struct.pack("<%dh" % len(samples), *samples)
    with open(path, "wb") as f:
        f.write(b"RIFF" + struct.pack("<I", 36 + len(body)) + b"WAVE")
        f.write(b"fmt " + struct.pack("<IHHIIHH", 16, 1, 1, rate, rate * 2, 2, 16))
        f.write(b"data" + struct.pack("<I", len(body)) + body)


def nearest_zero_crossing(samples, frame, search):
    """The closest sample either side of `frame` where the signal crosses zero."""
    for offset in range(0, search):
        for i in (frame - offset, frame + offset):
            if 0 < i < len(samples) and (samples[i - 1] <= 0) != (samples[i] <= 0):
                return i
    return frame


def faded(samples, fade_in, fade_out):
    out = list(samples)
    for i in range(min(fade_in, len(out))):
        out[i] = int(out[i] * (i / fade_in))
    for i in range(min(fade_out, len(out))):
        out[-1 - i] = int(out[-1 - i] * (i / fade_out))
    return out


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--source", required=True)
    ap.add_argument("--out-dir", required=True)
    ap.add_argument("--prefix", required=True)
    ap.add_argument("--start", type=float, required=True, help="seconds: where the held take begins")
    ap.add_argument("--end", type=float, required=True, help="seconds: where it ends")
    ap.add_argument("--attack", type=float, default=0.18, help="seconds of transient to split off")
    ap.add_argument("--tail", type=float, default=0.30, help="seconds of release to split off")
    ap.add_argument("--quality", type=int, default=3)
    ap.add_argument("--target-dbfs", type=float, default=-1.0,
                    help="peak of the LOUDEST part; the others keep their relation to it")
    args = ap.parse_args()

    os.makedirs(args.out_dir, exist_ok=True)
    with tempfile.TemporaryDirectory() as tmp:
        wav = os.path.join(tmp, "src.wav")
        decode_to_wav(args.source, wav, SFX_RATE)
        samples, rate = read_wav(wav)

        f = lambda t: max(0, min(len(samples) - 1, int(t * rate)))
        s, e = f(args.start), f(args.end)
        a_end, t_start = f(args.start + args.attack), f(args.end - args.tail)
        # Only the loop's cuts are hunted to zero: nothing repeats the other two.
        search = int(0.01 * rate)
        a_end = nearest_zero_crossing(samples, a_end, search)
        t_start = nearest_zero_crossing(samples, t_start, search)

        fade = int(0.008 * rate)
        parts_raw = {
            "start": faded(samples[s:a_end], fade, 0),
            "loop": samples[a_end:t_start],
            "stop": faded(samples[t_start:e], 0, fade),
        }
        # One gain for the set, from the loudest sample in any of them.
        peak = max((max(abs(v) for v in b) if b else 0) for b in parts_raw.values())
        target = int(32767 * (10.0 ** (args.target_dbfs / 20.0)))
        gain = (target / peak) if peak else 1.0
        print("  set peak %.2f dBFS -> %.2f dBFS (gain x%.3f), balance kept"
              % (20 * math.log10(peak / 32767.0) if peak else -99.0, args.target_dbfs, gain))
        parts = {n: [max(-32768, min(32767, int(v * gain))) for v in b]
                 for n, b in parts_raw.items()}

        for name, body in parts.items():
            if not body:
                print("  %-6s EMPTY -- check --start/--end/--attack/--tail" % name)
                continue
            part_wav = os.path.join(tmp, name + ".wav")
            write_wav(part_wav, body, rate)
            out = os.path.join(args.out_dir, "%s_%s.ogg" % (args.prefix, name))
            encode_to_ogg(part_wav, out, args.quality)
            print("  %-6s %6.3fs  %s" % (name, len(body) / rate, out))
    return 0


if __name__ == "__main__":
    sys.exit(main())
