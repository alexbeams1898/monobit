// Engine-side audio sequencer. Holds per-voice state for the 4-voice
// music stepper plus SFX preemption on voice 0. Steps the per-voice
// frequency once per game frame; the platform actually drives the
// speaker via a hardware timer at the requested frequencies.

#include "audio.h"

#include "progmem.h"

namespace audio {

// Forward-declared; the platform supplies these.
namespace platform {
void init_hw();
void set_tone(u16 hz);                      // legacy single-voice (== voice 0)
void set_voice_tone(u8 voice_idx, u16 hz);  // voice_idx in [0..3]
}  // namespace platform

namespace {

constexpr u8 NUM_VOICES = 4;

// Per-voice playback state. Used for both song notes (per-voice list
// walk) and the SFX path (voice 0 only). Each note is one `Sfx` —
// freq_start as the initial pitch, freq_slope per-frame delta, duration
// in frames.
struct VoiceState {
  Sfx active;          // currently playing note (copied from PROGMEM)
  u16 active_freq;     // current pitch with slope applied
  u8 frames_left;      // frames remaining at current note (incl. start frame)
  bool start_pending;  // first tick after a new note is a no-op (see below)
  bool playing;        // false = silent
};

VoiceState voice[NUM_VOICES];

// Song-stepper state. Each voice independently walks its own beep list
// — they don't have to align note-for-note. End of list loops back to 0.
const Song* song;           // PROGMEM song or nullptr
u8 song_index[NUM_VOICES];  // current note index per voice

// SFX preemption: when true, voice 0 plays SFX state instead of song
// state. song state for voice 0 still ticks (frames_left, freq) so
// resume picks up where the song would be.
bool sfx_active;
VoiceState sfx_state;
const Sfx* sfx_src;  // PROGMEM source — used to drop same-sound retriggers

// Read one byte from a PROGMEM pointer. The Sfx struct fields are
// 16-bit / 16-bit / 8-bit; we copy byte-by-byte to stay portable.
void copy_sfx_pgm(const Sfx* pgm_src, Sfx* dst) {
  const u8* s = (const u8*)pgm_src;
  u8* d       = (u8*)dst;
  for (u8 i = 0; i < sizeof(Sfx); ++i) {
    d[i] = pgm_read_byte(&s[i]);
  }
}

// Resolve a voice's PROGMEM beep list pointer for index. Returns nullptr
// if the voice has no list (count == 0).
const Sfx* voice_list(u8 voice_idx) {
  if (!song) return nullptr;
  // pgm_read_word reads a 16-bit value (the pointer field). On AVR
  // pointers are 16-bit; on PC the engine/progmem.h shim handles the
  // wider case.
  switch (voice_idx) {
  case 0: return (const Sfx*)pgm_read_word(&song->voice0);
  case 1: return (const Sfx*)pgm_read_word(&song->voice1);
  case 2: return (const Sfx*)pgm_read_word(&song->voice2);
  case 3: return (const Sfx*)pgm_read_word(&song->voice3);
  }
  return nullptr;
}

u8 voice_count(u8 voice_idx) {
  if (!song) return 0;
  switch (voice_idx) {
  case 0: return pgm_read_byte(&song->count0);
  case 1: return pgm_read_byte(&song->count1);
  case 2: return pgm_read_byte(&song->count2);
  case 3: return pgm_read_byte(&song->count3);
  }
  return 0;
}

// Load the note at `song_index[v]` into voice[v] for song-driven voices.
void load_song_note(u8 v) {
  const Sfx* list = voice_list(v);
  const u8 count  = voice_count(v);
  if (!list || count == 0) {
    voice[v].playing = false;
    return;
  }
  if (song_index[v] >= count) song_index[v] = 0;  // loop
  copy_sfx_pgm(&list[song_index[v]], &voice[v].active);
  voice[v].active_freq   = voice[v].active.freq_start;
  voice[v].frames_left   = voice[v].active.duration;
  voice[v].start_pending = true;
  voice[v].playing       = (voice[v].active.freq_start != 0);  // 0 = rest
}

// Apply current voice state to the platform mixer. Voice 0 routes
// through sfx_state when SFX is preempting; otherwise song state.
void emit_voice(u8 v) {
  if (v == 0 && sfx_active) {
    platform::set_voice_tone(0, sfx_state.playing ? sfx_state.active_freq : 0);
    return;
  }
  platform::set_voice_tone(v, voice[v].playing ? voice[v].active_freq : 0);
}

// Step a single VoiceState through one frame. Returns true if the
// state's note finished this frame (caller advances to next note).
// Mirrors the original SFX stepper: first tick after note-start is a
// no-op (gives the start freq a full frame of audible time), then the
// remaining (duration-1) frames apply the slope, then silence/end.
bool step_voice_state(VoiceState& s) {
  if (!s.playing) return false;
  if (s.start_pending) {
    s.start_pending = false;
    return false;
  }
  if (s.frames_left == 0) {
    // Final tick — note done.
    s.playing = false;
    return true;
  }
  --s.frames_left;
  if (s.frames_left == 0) {
    s.playing = false;
    return true;
  }
  // Apply slope. Clamp at sensible bounds (50 Hz floor, 8 kHz ceiling).
  // 32bit-ok: active_freq is u16, freq_slope is i16; their sum can go
  // negative (descending wails) or overflow u16. Widen, clamp, narrow.
  i32 next = (i32)s.active_freq + (i32)s.active.freq_slope;
  if (next < 50) next = 50;
  if (next > 8000) next = 8000;
  s.active_freq = (u16)next;
  return false;
}

}  // namespace

void init() {
  for (u8 v = 0; v < NUM_VOICES; ++v) {
    voice[v].playing = false;
    song_index[v]    = 0;
  }
  song              = nullptr;
  sfx_active        = false;
  sfx_state.playing = false;
  sfx_src           = nullptr;
  platform::init_hw();
  for (u8 v = 0; v < NUM_VOICES; ++v)
    platform::set_voice_tone(v, 0);
}

void play(const Sfx* pgm_sfx) {
  // Drop re-triggers of THE SAME sound while it's still playing — but
  // let DIFFERENT sounds preempt. Rationale: rapid UI scrolls re-fire
  // SFX_MENU within ~80 ms; the previous tail bleeding into the new
  // start makes the arpeggio read as "only the low note plays" on
  // every 2nd-3rd press. Suppressing same-sound retriggers gives every
  // beep a clean fresh start.
  if (sfx_active && sfx_src == pgm_sfx) return;
  copy_sfx_pgm(pgm_sfx, &sfx_state.active);
  sfx_state.active_freq   = sfx_state.active.freq_start;
  sfx_state.frames_left   = sfx_state.active.duration;
  sfx_state.start_pending = true;
  sfx_state.playing       = true;
  sfx_src                 = pgm_sfx;
  sfx_active              = true;
  emit_voice(0);
}

void play_song(const Song* pgm_song) {
  song = pgm_song;
  for (u8 v = 0; v < NUM_VOICES; ++v) {
    song_index[v] = 0;
    if (song) load_song_note(v);
  }
  // All voices emit immediately; voice 0 yields to SFX if active.
  for (u8 v = 0; v < NUM_VOICES; ++v)
    emit_voice(v);
}

void stop_song() {
  song = nullptr;
  for (u8 v = 0; v < NUM_VOICES; ++v) {
    voice[v].playing = false;
    if (!(v == 0 && sfx_active)) platform::set_voice_tone(v, 0);
  }
}

void tick() {
  // SFX path on voice 0 (preempts song).
  if (sfx_active) {
    if (step_voice_state(sfx_state)) {
      // SFX ended — release voice 0 back to the song.
      sfx_active = false;
      sfx_src    = nullptr;
    }
    emit_voice(0);
  }
  // Song stepping for all voices that aren't currently SFX-preempted.
  // (Voice 0 song state still advances even while SFX preempts, so the
  // song "keeps playing in the background" and resumes from the right
  // place. We just don't emit voice 0's song freq while SFX is active.)
  if (song) {
    for (u8 v = 0; v < NUM_VOICES; ++v) {
      const bool note_done = step_voice_state(voice[v]);
      if (note_done) {
        ++song_index[v];
        load_song_note(v);
      }
    }
    // Emit voices 1+ always; voice 0 only if SFX isn't preempting.
    if (!sfx_active) emit_voice(0);
    for (u8 v = 1; v < NUM_VOICES; ++v)
      emit_voice(v);
  }
}

void mute() {
  sfx_active        = false;
  sfx_state.playing = false;
  sfx_src           = nullptr;
  song              = nullptr;
  for (u8 v = 0; v < NUM_VOICES; ++v) {
    voice[v].playing = false;
    platform::set_voice_tone(v, 0);
  }
}

void silence_pins() {
  for (u8 v = 0; v < NUM_VOICES; ++v) {
    platform::set_voice_tone(v, 0);
  }
}

}  // namespace audio
