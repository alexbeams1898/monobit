// Engine audio API — 1-bit speaker, sfxr-style SFX synthesis + 4-voice
// XOR-mixed music engine.
//
// One pin, four voices: the platform layer runs an ISR that maintains
// four phase accumulators and XORs their high bits onto the speaker
// pin. Each voice independently steps through a list of `Sfx` beep
// descriptors — same struct, same semantics as one-shot SFX. A "song"
// is four parallel beep lists (one per voice).
//
// Voice roles by convention:
//   V0 — bass / root
//   V1 — harmony / fifth
//   V2 — lead / second melody
//   V3 — drums (LFSR noise mode; auto-ducks V0/V1/V2 when firing)
//
// SFX preempts voice 0 (the lead). The remaining voices keep playing
// the song. When the SFX ends, the song stepper resumes voice 0 from
// where it would be.

#pragma once

#include "types.h"

namespace audio {

// Sound effect / music note descriptor. The same struct describes both
// a one-shot SFX (`play()`) and one note in a song's voice list. Stored
// in PROGMEM; the engine copies bytes into RAM-resident voice slots
// via pgm_read_byte.
struct Sfx {
  u16 freq_start;  // starting frequency (Hz). 0 = rest (silence for `duration` frames).
  i16 freq_slope;  // Hz per frame; +ve = chirp up, -ve = chirp down
  u8 duration;     // frames at 60Hz
};

// A song is four parallel beep lists, one per voice. Each list runs
// independently — voices don't have to align note-for-note. Lists in
// PROGMEM. count = number of beeps in each list.
//
// The song loops automatically when all voices reach end-of-list.
struct Song {
  const Sfx* voice0;  // bass / root
  const Sfx* voice1;  // harmony / fifth
  const Sfx* voice2;  // lead / second melody (yields to SFX)
  const Sfx* voice3;  // drums (LFSR noise — see Sfx::is_noise once that lands)
  u8 count0;
  u8 count1;
  u8 count2;
  u8 count3;
};

void init();  // platform setup. Call once at boot.
// Start playing this effect; preempts any running effect on voice 0.
// While a song is playing, voice 0 yields to the SFX until the SFX ends.
// pgm_sfx is a PROGMEM pointer — pass `&SFX_*` declared `PROGMEM`.
void play(const Sfx* pgm_sfx);
// Start playing a song. Preempts any current song. SFX still preempts
// voice 0. Pass nullptr or call stop_song() to silence.
void play_song(const Song* pgm_song);
void stop_song();
void tick();  // engine-side per-frame step. Called from main loop.
void mute();  // silence everything immediately (SFX + song).
// Drive the speaker pin to zero on every voice without touching song or
// SFX state. Used to silence the audio during scene-bank SPM swaps so
// the DAC pin doesn't hold a stale PWM value across the ~400 ms window.
// The song resumes from where it left off on the next tick().
void silence_pins();

}  // namespace audio
