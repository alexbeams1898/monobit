#!/usr/bin/env python3
"""Cut a recording of someone walking into one clip per FOOTFALL.

THE CUTS GO ON THE STEPS, NOT ON A GRID. Slicing a walk take into equal
lengths puts each footfall at a different offset inside its clip, so the sound
lands late or early depending on which variation was drawn -- the rhythm smears
and the pool stops reading as one man walking. Onsets are found instead (rolling
RMS, local maxima over a threshold relative to the median), and every slice
starts the same short moment BEFORE its step, so whichever one plays, the
footfall arrives when the foot lands.

Slices are faded at both ends, since a cut mid-waveform steps the signal and
that step is a click; and the set takes ONE gain rather than one each, because a
soft step is a variation and normalising it up to match the others deletes it.

Requires ffmpeg in PATH.
"""

import argparse
import os
import sys
import tempfile

import audio_fx


def rolling_rms(samples, window):
    """RMS per window, walked one window at a time."""
    out = []
    for start in range(0, len(samples) - window, window):
        total = 0
        for i in range(start, start + window):
            total += samples[i] * samples[i]
        out.append((total / window) ** 0.5)
    return out


def find_onsets(rms, threshold_ratio, min_gap_windows):
    """Window indices where the level jumps -- a foot landing.

    Threshold is relative to the take's own MEDIAN level rather than absolute,
    so a quiet recording and a loud one are read the same way. A minimum gap
    keeps the two halves of one footfall from counting twice.
    """
    if not rms:
        return []
    ordered = sorted(rms)
    median = ordered[len(ordered) // 2]
    floor = median * threshold_ratio
    onsets = []
    for i in range(1, len(rms) - 1):
        if rms[i] < floor:
            continue
        if rms[i] < rms[i - 1] or rms[i] < rms[i + 1]:
            continue  # not the peak of this rise
        if onsets and i - onsets[-1] < min_gap_windows:
            continue
        onsets.append(i)
    return onsets


def chop(args):
    with tempfile.TemporaryDirectory() as tmp:
        wav = os.path.join(tmp, "source.wav")
        audio_fx.decode_to_wav(args.source, wav, audio_fx.SFX_RATE)
        samples, rate = audio_fx.read_wav(wav)
        if not samples:
            print("nothing decoded from " + args.source)
            return 1

        window = max(1, int(0.01 * rate))  # 10ms
        onsets = find_onsets(rolling_rms(samples, window), args.threshold_ratio,
                             max(1, int(args.min_gap_seconds * rate / window)))
        print("%s: %.2fs, %d footfall(s) found" % (args.source, len(samples) / rate, len(onsets)))
        if not onsets:
            print("none over the threshold -- lower --threshold-ratio")
            return 1

        # SPREAD ACROSS THE TAKE. The first N footfalls in a recording are N consecutive steps
        # of one crossing -- the same shoe on the same ground a second apart, which is the least
        # varied set the take can give. Picked evenly across the whole thing instead, so the pool
        # holds a slow step and a hard one and a scuff, which is what a pool is for.
        if len(onsets) > args.count:
            step = len(onsets) / float(args.count)
            onsets = [onsets[int(i * step)] for i in range(args.count)]

        lead = int(args.pre_onset_seconds * rate)
        length = int(args.slice_seconds * rate)
        fade = max(1, int(0.008 * rate))
        cut = []
        for o in onsets:
            start = max(0, o * window - lead)
            body = samples[start:start + length]
            if len(body) < fade * 2:
                continue
            faded = []
            for i, v in enumerate(body):
                if i < fade:
                    faded.append(int(v * i / fade))
                elif i > len(body) - fade:
                    faded.append(int(v * (len(body) - i) / fade))
                else:
                    faded.append(v)
            cut.append(faded)

        gain, peak = audio_fx.one_gain(cut, args.target_dbfs)
        print("  set peak %.2f dBFS -> %.2f dBFS (gain x%.3f), balance kept"
              % (audio_fx.dbfs(peak), args.target_dbfs, gain))

        os.makedirs(args.out_dir, exist_ok=True)
        for i, body in enumerate(cut):
            part = os.path.join(tmp, "cut.wav")
            audio_fx.write_wav(part, audio_fx.applied(body, gain), rate)
            out = os.path.join(args.out_dir, "%s_%d.ogg" % (args.prefix, i + 1))
            audio_fx.encode_to_ogg(part, out, args.quality)
            after = audio_fx.encoded_peak(out, os.path.join(tmp, "back.wav"))
            print("  %-34s %6.3fs  ENCODED PEAK %5.2f dBFS%s"
                  % (os.path.basename(out), len(body) / rate, audio_fx.dbfs(after),
                     "  <-- CLIPPED, lower --target-dbfs" if after >= 32700 else ""))
    return 0


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--source", required=True, help="a recording of someone walking")
    ap.add_argument("--out-dir", required=True)
    ap.add_argument("--prefix", required=True, help="e.g. footstep_walk -> footstep_walk_1.ogg")
    ap.add_argument("--count", type=int, default=8, help="how many footfalls to keep")
    ap.add_argument("--slice-seconds", type=float, default=0.30)
    ap.add_argument("--pre-onset-seconds", type=float, default=0.02,
                    help="how much of the moment BEFORE the step each slice keeps")
    ap.add_argument("--threshold-ratio", type=float, default=2.5,
                    help="how far over the take's median level a rise must go to be a step")
    ap.add_argument("--min-gap-seconds", type=float, default=0.25)
    ap.add_argument("--target-dbfs", type=float, default=audio_fx.TARGET_DBFS)
    ap.add_argument("--quality", type=int, default=3)
    return chop(ap.parse_args())


if __name__ == "__main__":
    sys.exit(main())
