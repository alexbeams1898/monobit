// SDL2 audio backend — software 4-voice 1-bit XOR mixer.
//
// Mirrors the Arduboy ISR's mixer model: three phase accumulators, the
// high bit of each is the voice's square wave, XOR them to produce the
// pin output. Same math, different tick rate (44.1 kHz here vs 16 kHz
// on AVR). Listener cannot distinguish this from a real piezo driven
// by the same XOR pattern — the audible waveform is identical modulo
// reconstruction filtering on the speaker.
//
// The callback runs on SDL's audio thread; the game thread updates
// per-voice phase increments via set_voice_tone(). Relaxed atomics
// because a single torn u16 just causes one callback's worth of weird
// pitch — fractions of a millisecond.

#include "types.h"

#include <SDL.h>
#include <atomic>
#include <cerrno>
#include <cstdio>
#include <cstring>

namespace audio {
namespace platform {

namespace {

constexpr int SAMPLE_RATE  = 44100;
constexpr Sint16 AMPLITUDE = 12000;  // well clear of clipping, loud enough
constexpr int NUM_VOICES   = 4;

// Per-voice phase increment, written by the game thread via
// set_voice_tone(). Increment is computed as (hz * 65536) / SAMPLE_RATE
// so the high bit of `phase` flips at the desired rate. inc=0 silences
// that voice (phase still accumulates but stays at whatever bit-15 was;
// silenced voices contribute a constant to the XOR, which is fine).
//
// Actually that's wrong — a silenced voice with inc=0 holds bit-15 at
// its current value forever, contributing a DC offset to the XOR. We
// special-case inc=0 to mean "voice contributes 0" in the mix.
std::atomic<u16> voice_inc[NUM_VOICES]{};
// Callback-owned: phase accumulators. Same semantics as the AVR ISR.
u16 voice_phase[NUM_VOICES]{};

SDL_AudioDeviceID device = 0;

// Audio capture (PC-only debug aid). When enabled via --record-audio=PATH,
// every sample the callback emits is also written to a WAV file. The
// header is patched on close with the final sample count.
FILE* capture_file     = nullptr;
u32 capture_samples    = 0;
SDL_mutex* capture_mtx = nullptr;

void write_le_u32(FILE* f, u32 v) {
  u8 b[4] = {(u8)(v & 0xFF), (u8)((v >> 8) & 0xFF), (u8)((v >> 16) & 0xFF), (u8)((v >> 24) & 0xFF)};
  fwrite(b, 1, 4, f);
}
void write_le_u16(FILE* f, u16 v) {
  u8 b[2] = {(u8)(v & 0xFF), (u8)((v >> 8) & 0xFF)};
  fwrite(b, 1, 2, f);
}

void write_wav_header(FILE* f, u32 sample_count) {
  const u32 data_bytes = sample_count * 2;  // 16-bit mono
  fwrite("RIFF", 1, 4, f);
  write_le_u32(f, 36 + data_bytes);
  fwrite("WAVE", 1, 4, f);
  fwrite("fmt ", 1, 4, f);
  write_le_u32(f, 16);  // fmt chunk size
  write_le_u16(f, 1);   // PCM
  write_le_u16(f, 1);   // mono
  write_le_u32(f, SAMPLE_RATE);
  write_le_u32(f, SAMPLE_RATE * 2);  // byte rate
  write_le_u16(f, 2);                // block align
  write_le_u16(f, 16);               // bits per sample
  fwrite("data", 1, 4, f);
  write_le_u32(f, data_bytes);
}

void audio_callback(void* /*userdata*/, Uint8* stream, int len) {
  Sint16* out       = reinterpret_cast<Sint16*>(stream);
  const int samples = len / (int)sizeof(Sint16);
  // Re-read voice_inc[] per sample. SDL's callback chunk is ~23 ms
  // (1024 samples at 44.1 kHz); the game updates increments every ~16 ms.
  // Reading per-sample tracks every set_voice_tone() within ~22 us, the
  // same per-sample tracking the single-voice version had.
  for (int i = 0; i < samples; ++i) {
    u8 mix       = 0;
    u8 any_voice = 0;
    for (int v = 0; v < NUM_VOICES; ++v) {
      const u16 inc = voice_inc[v].load(std::memory_order_relaxed);
      if (inc == 0) continue;
      voice_phase[v] = (u16)(voice_phase[v] + inc);
      mix ^= (u8)(voice_phase[v] >> 15);
      any_voice = 1;
    }
    out[i] = any_voice ? ((mix & 1) ? +AMPLITUDE : -AMPLITUDE) : 0;
  }
  // Fork to capture file if recording. Lock so the close path can patch
  // the header without racing the callback. Flush every callback so the
  // file on disk is well-formed even on abrupt termination (timeout,
  // SIGTERM, crash). Header fields stay zero until capture_stop runs,
  // but most WAV readers recover gracefully.
  if (capture_file && capture_mtx) {
    SDL_LockMutex(capture_mtx);
    if (capture_file) {
      fwrite(out, sizeof(Sint16), (size_t)samples, capture_file);
      capture_samples += (u32)samples;
      fflush(capture_file);
    }
    SDL_UnlockMutex(capture_mtx);
  }
}

}  // namespace

// PC-only: start capturing every sample the SDL audio callback emits to
// a 16-bit mono WAV file. Call once at startup if the user passed
// --record-audio. Caller must invoke capture_stop() at exit to patch
// the header with the final sample count.
void capture_start(const char* path) {
  // Print to stderr too — SDL_Log routes through SDL's log system which
  // can be silenced when stderr isn't a tty (background launches).
  std::fprintf(stderr, "audio capture: opening '%s'\n", path);
  std::fflush(stderr);
  capture_mtx  = SDL_CreateMutex();
  capture_file = std::fopen(path, "wb");
  if (!capture_file) {
    std::fprintf(stderr, "audio capture: fopen failed for '%s' (errno=%d)\n", path, errno);
    std::fflush(stderr);
    SDL_Log("audio capture: failed to open %s", path);
    return;
  }
  // Placeholder header — patched on stop with real lengths.
  write_wav_header(capture_file, 0);
  capture_samples = 0;
  std::fprintf(stderr, "audio capture: started\n");
  std::fflush(stderr);
}

void capture_stop() {
  if (!capture_file) return;
  SDL_LockMutex(capture_mtx);
  // Patch header with final sample count.
  std::fseek(capture_file, 0, SEEK_SET);
  write_wav_header(capture_file, capture_samples);
  std::fclose(capture_file);
  capture_file = nullptr;
  SDL_UnlockMutex(capture_mtx);
  SDL_DestroyMutex(capture_mtx);
  capture_mtx = nullptr;
}

void init_hw() {
  SDL_AudioSpec want{};
  want.freq     = SAMPLE_RATE;
  want.format   = AUDIO_S16SYS;
  want.channels = 1;
  want.samples  = 1024;  // ~23 ms latency at 44.1kHz — fine for a beeper
  want.callback = audio_callback;
  device        = SDL_OpenAudioDevice(nullptr, 0, &want, nullptr, 0);
  if (!device) {
    SDL_Log("SDL_OpenAudioDevice failed: %s", SDL_GetError());
    return;
  }
  SDL_PauseAudioDevice(device, 0);  // start
}

void set_voice_tone(u8 voice_idx, u16 hz) {
  if (voice_idx >= NUM_VOICES) return;
  u16 inc;
  if (hz == 0) {
    inc = 0;
  } else {
    // Same formula as the AVR ISR: increment = (hz * 65536) / SAMPLE_RATE.
    u32 prod = (u32)hz * 65536u;
    inc      = (u16)(prod / SAMPLE_RATE);
    if (inc == 0) inc = 1;
  }
  voice_inc[voice_idx].store(inc, std::memory_order_relaxed);
}

// Back-compat: legacy single-voice API. Drives voice 0; voices 1 and 2
// retain whatever they were last set to.
void set_tone(u16 hz) {
  set_voice_tone(0, hz);
}

}  // namespace platform
}  // namespace audio
