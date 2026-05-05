// Arduboy audio backend — software-driven 1-bit speaker, 4-voice XOR mixer.
//
// Hardware: passive piezo on PC6 (= OC3A, Timer3 compare-A output).
//
// Mixer: phase-accumulator squares, XOR'd to one pin. Each voice has a
// 16-bit phase accumulator and a 16-bit phase increment. The ISR adds
// the increment to the accumulator each tick (16 kHz); the high bit of
// the accumulator IS the voice's square wave. XOR three voices' high
// bits and write to PC6.
//
// Increment math:
//   phase wraps every (65536 / increment) ticks
//   wrap rate = TICK_HZ * (increment / 65536)
//   want wrap rate = 2 * hz (full period = up + down)
//   => increment = (2 * hz * 65536) / TICK_HZ
//             = (hz * 65536) / 8000   (TICK_HZ=16000)
//             ≈ hz * 8.192
// For musical notes (50-4000 Hz), increment is 410-32768 — fits u16.
//
// Timer3: CTC mode, prescaler /1, OCR3A=999 → 16 MHz / 1000 = 16 kHz ISR.
//
// Cycle budget per ISR (4 voices):
//   prologue (save R0, R1, SREG, R18-R27, R30, R31): ~24 cyc
//   4× phase add (16-bit add + store): 4 × 8 = 32 cyc
//   4× extract MSB: 4 × 3 = 12 cyc
//   XOR + pin write: 5 cyc
//   epilogue: ~16 cyc
//   total: ~89 cyc / ISR
// At 16 kHz: 89 * 16000 = 1.42M cyc/s = 8.9% of 16 MHz CPU.

#include "types.h"

#include <avr/io.h>
#include <avr/interrupt.h>

// File-scope statics for ISR access. Anonymous namespaces inside
// audio::platform can't be reached from a TU-level ISR(...) function,
// so the audio voice state lives here.
//
// Volatile: ISR writes phase[]; main-thread set_voice_tone() writes
// phase_inc[]. Multi-byte writes guarded with cli/sti.
namespace {
constexpr u16 TICK_HZ   = 16000;
constexpr u8 NUM_VOICES = 4;

volatile u16 audio_phase[NUM_VOICES];
volatile u16 audio_phase_inc[NUM_VOICES];  // 0 = silent
}  // namespace

namespace audio {
namespace platform {

void init_hw() {
  // PC6 as output. Speaker driven by software pin writes from here on.
  DDRC |= (1 << 6);
  PORTC &= ~(1 << 6);

  // Timer3 in CTC mode, prescaler /1. WGM mode 4 (WGM33=0, WGM32=1,
  // WGM31=0, WGM30=0). Compare-output disconnected — we drive PC6 from
  // the ISR, not the timer hardware.
  TCCR3A = 0;
  TCCR3B = (1 << WGM32) | (1 << CS30);  // CTC, no prescale (16 MHz tick)
  TCCR3C = 0;
  // F_CPU / TICK_HZ - 1 = 16_000_000 / 16_000 - 1 = 999.
  OCR3A = 999;
  TCNT3 = 0;
  // Enable compare-A interrupt.
  TIMSK3 = (1 << OCIE3A);

  for (u8 v = 0; v < NUM_VOICES; ++v) {
    audio_phase[v]     = 0;
    audio_phase_inc[v] = 0;
  }
}

// Set one voice's frequency. voice_idx must be < NUM_VOICES (3).
// hz=0 silences that voice. Other voices are unaffected.
void set_voice_tone(u8 voice_idx, u16 hz) {
  if (voice_idx >= NUM_VOICES) return;
  u16 inc;
  if (hz == 0) {
    inc = 0;
  } else {
    // increment = (hz * 65536) / 16000 = (hz * 8192) / 2000
    // 32bit-ok: hz up to 8000, * 65536 = 524M — needs u32 intermediate.
    // Cost is a single 32-bit multiply + divide, only on tone change
    // (not in the ISR hot path).
    u32 prod = (u32)hz * 65536u;
    inc      = (u16)(prod / 16000u);
    if (inc == 0) inc = 1;
  }
  // Atomic 16-bit write under cli/sti so the ISR can't read a torn value.
  u8 sreg = SREG;
  cli();
  audio_phase_inc[voice_idx] = inc;
  // Don't reset phase[] — letting the accumulator continue avoids an
  // audible click on every pitch change.
  SREG = sreg;
}

// Back-compat: legacy single-voice API. Drives voice 0; voices 1 and 2
// stay at whatever they were last set to (silence at boot).
void set_tone(u16 hz) {
  set_voice_tone(0, hz);
}

}  // namespace platform
}  // namespace audio

// Timer3 compare-A ISR. Runs at 16 kHz. Adds each voice's phase
// increment to its accumulator; the high bit of each accumulator IS
// the voice's square wave. XOR the four high bits and write to PC6.
//
// Hand-tight: unrolled 4 iterations because the compiler often
// struggles to keep the inc/phase pairs in registers across an
// indexed loop on AVR.
ISR(TIMER3_COMPA_vect) {
  u16 p0         = audio_phase[0] + audio_phase_inc[0];
  u16 p1         = audio_phase[1] + audio_phase_inc[1];
  u16 p2         = audio_phase[2] + audio_phase_inc[2];
  u16 p3         = audio_phase[3] + audio_phase_inc[3];
  audio_phase[0] = p0;
  audio_phase[1] = p1;
  audio_phase[2] = p2;
  audio_phase[3] = p3;
  // High bit of each phase = current state of that voice's square wave.
  // XOR them together (addition mod 2). Bit 15 is the high bit of u16.
  u8 mix = ((p0 >> 15) ^ (p1 >> 15) ^ (p2 >> 15) ^ (p3 >> 15)) & 1;
  if (mix) {
    PORTC |= (1 << 6);
  } else {
    PORTC &= ~(1 << 6);
  }
}
