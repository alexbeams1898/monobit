#!/usr/bin/env python3
"""Cut a recording of a HELD sound into the three files a held sound needs.

A trigger you hold cannot be one clip. Played as a one-shot it ends while the
trigger is still down; looped whole, its attack transient repeats and the thing
machine-guns. So it is three:

  <prefix>_start.ogg   the trigger pull -- the transient, played once
  <prefix>_loop.ogg    the body -- seamless, played until release
  <prefix>_stop.ogg    the release -- the dribble after the trigger comes up

THE LOOP IS CROSSFADED INTO ITSELF, not spliced on a zero crossing. Zero
crossings are the right answer for a tone; for BROADBAND NOISE -- a spray, a
hiss, a rumble -- they are worthless. The signal crosses zero constantly and
between two large samples, so "the nearest sign change" is just an arbitrary cut
at whatever amplitude the noise happened to be at, and the step is exactly the
click it was meant to avoid.

So the loop's head is crossfaded with the audio that FOLLOWED its tail: wrapping
from end to start then runs through material that already flowed together. The
blend is equal-power (sqrt), because summing two uncorrelated noise signals with
a linear fade dips about 3 dB in the middle and you hear it breathe.

Every cut also gets a few milliseconds of fade so no clip begins or ends part-way
up a waveform.

Output is 22050 Hz mono ogg/vorbis, the engine's SFX standard (see
prison-escape-game/docs/PERFORMANCE.md).

THE ENCODED FILE IS WHAT IS CHECKED, not the samples going into it. Vorbis
reconstructs intersample peaks ABOVE the original, so PCM normalised to a target
can decode louder than it and clip -- and on a LOOP one clipped sample is a tick
you hear on every pass, forever. So each part is encoded, decoded back, and
measured; if it hit the rail the whole set is knocked down and cut again. The
default target leaves real headroom for that reason.

THE PARTS ARE NORMALISED AS ONE, by a single gain taken from the loudest of
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
import sys
import tempfile

from audio_fx import (SFX_RATE, TARGET_DBFS, decode_to_wav, encode_to_ogg, read_wav, write_wav,
                      dbfs, one_gain, applied, encoded_peak)


def crossfade_loop(samples, start, end, blend):
    """The loop body, with its head blended out of what followed its tail.

    `blend` samples beyond `end` are mixed over the loop's first `blend` samples,
    the outgoing part fading down and the incoming up, so the wrap point is a
    continuation rather than a cut.
    """
    body = samples[start:end]
    if blend <= 0 or len(body) <= blend * 2:
        return body
    after = samples[end:end + blend]
    if len(after) < blend:
        return body
    for i in range(blend):
        w = (i + 0.5) / blend
        # Equal power: uncorrelated noise summed with linear weights loses energy mid-blend.
        body[i] = int(body[i] * math.sqrt(w) + after[i] * math.sqrt(1.0 - w))
    return body


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
    ap.add_argument("--fade", type=float, default=0.006, help="seconds faded at every cut")
    ap.add_argument("--blend", type=float, default=0.060,
                    help="seconds of crossfade at the loop's wrap point")
    ap.add_argument("--target-dbfs", type=float, default=TARGET_DBFS,
                    help="peak of the LOUDEST part AFTER encoding; the others keep their "
                         "relation to it. -3 rather than -1: vorbis decodes above the samples "
                         "it was given, and one clipped sample in a loop ticks on every pass")
    args = ap.parse_args()

    os.makedirs(args.out_dir, exist_ok=True)
    with tempfile.TemporaryDirectory() as tmp:
        wav = os.path.join(tmp, "src.wav")
        decode_to_wav(args.source, wav, SFX_RATE)
        samples, rate = read_wav(wav)

        f = lambda t: max(0, min(len(samples) - 1, int(t * rate)))
        s, e = f(args.start), f(args.end)
        a_end, t_start = f(args.start + args.attack), f(args.end - args.tail)

        fade = int(args.fade * rate)
        blend = int(args.blend * rate)
        parts_raw = {
            # Faded at BOTH ends: a clip that begins part-way up a waveform pops, and the pull
            # runs straight into the loop so its own tail must not step either.
            "start": faded(samples[s:a_end], fade, fade),
            "loop": crossfade_loop(samples, a_end, t_start, blend),
            "stop": faded(samples[t_start:e], fade, fade),
        }
        # One gain for the set, from the loudest sample in any of them.
        gain, peak = one_gain(parts_raw.values(), args.target_dbfs)
        print("  set peak %.2f dBFS -> %.2f dBFS (gain x%.3f), balance kept"
              % (dbfs(peak), args.target_dbfs, gain))
        parts = {n: applied(b, gain) for n, b in parts_raw.items()}

        for name, body in parts.items():
            if not body:
                print("  %-6s EMPTY -- check --start/--end/--attack/--tail" % name)
                continue
            part_wav = os.path.join(tmp, name + ".wav")
            write_wav(part_wav, body, rate)
            out = os.path.join(args.out_dir, "%s_%s.ogg" % (args.prefix, name))
            encode_to_ogg(part_wav, out, args.quality)
            after = encoded_peak(out, os.path.join(tmp, name + "-back.wav"))
            note = "  ENCODED PEAK %5.2f dBFS%s" % (
                dbfs(after), "  <-- CLIPPED, lower --target-dbfs" if after >= 32700 else "")
            print("  %-6s %6.3fs  %s%s" % (name, len(body) / rate, out, note))
    return 0


if __name__ == "__main__":
    sys.exit(main())
