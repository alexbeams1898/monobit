"""How this game turns a recording into a sound effect.

One home for the encode rule, shared by every script here. The rule that
matters and the reason it exists:

VORBIS OVERSHOOTS. Encoding is lossy in amplitude as well as in detail, and the
decoder reconstructs intersample peaks ABOVE the original -- so PCM normalised
to -1 dBFS routinely comes back at 0.0 dBFS with samples sitting on the rail,
which is a crackle you hear on every play, forever. So nothing here trusts the
number it asked for: every part is encoded, decoded back, and MEASURED, and the
default target leaves real headroom for exactly that reason.

Self-contained, taking no dependency on another game's tools -- the same call
selva-oscura already made. Requires ffmpeg in PATH.
"""

import math
import struct
import subprocess

SFX_RATE = 22050  # this game's sound effects: 22050 Hz, mono
TARGET_DBFS = -3.0  # peak AFTER encoding, with headroom for the overshoot above


def decode_to_wav(src, wav, rate=SFX_RATE):
    subprocess.run(["ffmpeg", "-y", "-hide_banner", "-loglevel", "error", "-i", src,
                    "-ar", str(rate), "-ac", "1", wav], check=True)


def encode_to_ogg(wav, ogg, quality=3):
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


def dbfs(peak):
    return 20 * math.log10(peak / 32767.0) if peak else -99.0


def one_gain(parts, target_dbfs=TARGET_DBFS):
    """The single gain that puts the LOUDEST of `parts` at the target.

    One gain for the set, never one per part: parts of a set are one sound, and
    their relative levels carry it. A release is quiet BECAUSE it is a dribble,
    and a footfall is soft BECAUSE it was a soft step -- normalising each to the
    same peak flattens exactly the difference that made them variations.
    """
    peak = max((max((abs(v) for v in b), default=0) for b in parts), default=0)
    target = int(32767 * (10.0 ** (target_dbfs / 20.0)))
    return (target / peak) if peak else 1.0, peak


def applied(samples, gain):
    return [max(-32768, min(32767, int(v * gain))) for v in samples]


def encoded_peak(ogg, tmp_wav):
    """What the file ACTUALLY peaks at once a decoder has been through it."""
    decode_to_wav(ogg, tmp_wav, SFX_RATE)
    decoded, _ = read_wav(tmp_wav)
    return max((abs(v) for v in decoded), default=0)
