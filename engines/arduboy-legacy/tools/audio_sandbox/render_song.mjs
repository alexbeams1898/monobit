#!/usr/bin/env node
// Offline renderer for sandbox songs. Mirrors the AudioWorklet's
// 4-voice XOR mixer + the SONG-mode scheduler exactly, takes a song
// definition (hardcoded below — copy from the sandbox seed list),
// produces a 44.1 kHz mono WAV.
//
// Usage:
//   node render_song.mjs > /dev/null   # writes to ./out.wav
//   start out.wav                       # play it
//
// Why this exists: the browser sandbox's audio is opaque to debugging
// from outside the browser. This script renders the same math offline
// so we can listen, look at the waveform, and confirm whether bad-
// sounding output is a math bug (renderer disagrees with what we
// expect) or a config bug (output matches the math, but the song
// design is wrong for 1-bit XOR).

import fs from 'node:fs';

const SAMPLE_RATE = 44100;
const FRAME_MS = 1000 / 60;
const FRAME_SEC = FRAME_MS / 1000;

// ---- NOTE_HZ (mirror of sandbox) ----
const NOTE_HZ = (() => {
  const sharp = ['C','C#','D','D#','E','F','F#','G','G#','A','A#','B'];
  const flat  = ['C','Db','D','Eb','E','F','Gb','G','Ab','A','Bb','B'];
  const out = {};
  for (let oct = 1; oct <= 6; oct++) {
    for (let semi = 0; semi < 12; semi++) {
      const midi = 12 * (oct + 1) + semi;
      const hz = Math.round(440 * Math.pow(2, (midi - 69) / 12));
      out[`${sharp[semi]}${oct}`] = hz;
      if (sharp[semi] !== flat[semi]) out[`${flat[semi]}${oct}`] = hz;
    }
  }
  return out;
})();

const DRUMS = {
  KICK:  { freq_start: 200,  freq_slope: -40,  duration: 4 },
  SNARE: { freq_start: 1400, freq_slope: -250, duration: 3 },  // thuddy snap
  HAT:   { freq_start: 5000, freq_slope: 0,    duration: 2 },
  BLAST: { freq_start: 3000, freq_slope: -20,  duration: 30 },
};

// Snare variants for A/B testing — pass --snare=B|C|D|E to override.
const SNARE_VARIANTS = {
  A: { freq_start: 1800, freq_slope: -100, duration: 4 },   // current: bright transient
  B: { freq_start: 1200, freq_slope: -80,  duration: 5 },   // lower body, less pierce
  C: { freq_start: 800,  freq_slope: -50,  duration: 6 },   // tom-snare hybrid, thuddy
  D: { freq_start: 2000, freq_slope: -300, duration: 2 },   // short tap, hard sweep
  E: { freq_start: 4000, freq_slope: -50,  duration: 2 },   // bright hat-style tick
};

function hzToInc(hz) {
  if (!hz || hz <= 0) return 0;
  let inc = Math.round((hz * 65536) / SAMPLE_RATE);
  if (inc < 1) inc = 1;
  if (inc > 65535) inc = 65535;
  return inc;
}

// ---- ILDJARN_DRONE definition ----
function makeIldjarnDrone() {
  const bpm = 190, bars = 8, stepsPerBar = 16;
  const totalSteps = bars * stepsPerBar;
  const cells = [
    new Array(totalSteps).fill(null),
    new Array(totalSteps).fill(null),
    new Array(totalSteps).fill(null),
    new Array(totalSteps).fill(null),
  ];
  const melody = [
    NOTE_HZ['F4'], NOTE_HZ['Eb4'], NOTE_HZ['Db4'],
    NOTE_HZ['C4'], NOTE_HZ['Bb3'], NOTE_HZ['Ab3'],
    NOTE_HZ['G3'], NOTE_HZ['F3'],
  ];
  for (let bar = 0; bar < 8; bar++) {
    const barStart = bar * stepsPerBar;
    for (const beat of [0, 4, 8, 12]) {
      cells[0][barStart + beat] = { hz: NOTE_HZ['F2'], patchName: 'TONE' };
      cells[1][barStart + beat] = { hz: NOTE_HZ['Ab2'], patchName: 'TONE' };
      cells[2][barStart + beat] = { hz: melody[bar], patchName: 'TONE' };
    }
    // Rock backbeat: kick beats 1+3, snare beats 2+4.
    cells[3][barStart + 0]  = { patchName: 'KICK' };
    cells[3][barStart + 4]  = { patchName: 'SNARE' };
    cells[3][barStart + 8]  = { patchName: 'KICK' };
    cells[3][barStart + 12] = { patchName: 'SNARE' };
  }
  return { name: 'ILDJARN_DRONE', bpm, bars, stepsPerBar, cells };
}

// ---- NIDHOGG_BLAST definition (32 bars: A x2 + B x2) ----
function makeNidhoggBlast() {
  const bpm = 200, bars = 32, stepsPerBar = 16;
  const totalSteps = bars * stepsPerBar;
  const cells = [
    new Array(totalSteps).fill(null),
    new Array(totalSteps).fill(null),
    new Array(totalSteps).fill(null),
    new Array(totalSteps).fill(null),
  ];
  const beats = [0, 4, 8, 12];
  const isPivotA = (i) => (i === 3 || i === 7);
  const leadA = [
    [NOTE_HZ['A4'],  NOTE_HZ['A4'],  NOTE_HZ['G4'], NOTE_HZ['F4']],
    [NOTE_HZ['E4'],  NOTE_HZ['D4'],  NOTE_HZ['E4'], NOTE_HZ['F4']],
    [NOTE_HZ['A4'],  NOTE_HZ['A4'],  NOTE_HZ['G4'], NOTE_HZ['F4']],
    [NOTE_HZ['Bb4'], NOTE_HZ['Ab4'], NOTE_HZ['G4'], NOTE_HZ['F4']],
    [NOTE_HZ['A4'],  NOTE_HZ['A4'],  NOTE_HZ['G4'], NOTE_HZ['F4']],
    [NOTE_HZ['E4'],  NOTE_HZ['D4'],  NOTE_HZ['E4'], NOTE_HZ['F4']],
    [NOTE_HZ['A4'],  NOTE_HZ['A4'],  NOTE_HZ['G4'], NOTE_HZ['F4']],
    [NOTE_HZ['Bb4'], NOTE_HZ['Ab4'], NOTE_HZ['G4'], NOTE_HZ['Eb4']],
  ];
  const leadB = [
    [NOTE_HZ['E5'],  NOTE_HZ['E5'],  NOTE_HZ['D5'], NOTE_HZ['C5']],
    [NOTE_HZ['B4'],  NOTE_HZ['A4'],  NOTE_HZ['B4'], NOTE_HZ['C5']],
    [NOTE_HZ['E5'],  NOTE_HZ['E5'],  NOTE_HZ['D5'], NOTE_HZ['C5']],
    [NOTE_HZ['F5'],  NOTE_HZ['Eb5'], NOTE_HZ['D5'], NOTE_HZ['C5']],
    [NOTE_HZ['E5'],  NOTE_HZ['E5'],  NOTE_HZ['D5'], NOTE_HZ['C5']],
    [NOTE_HZ['B4'],  NOTE_HZ['A4'],  NOTE_HZ['B4'], NOTE_HZ['C5']],
    [NOTE_HZ['E5'],  NOTE_HZ['E5'],  NOTE_HZ['D5'], NOTE_HZ['C5']],
    [NOTE_HZ['F5'],  NOTE_HZ['Eb5'], NOTE_HZ['D5'], NOTE_HZ['Bb4']],
  ];
  for (let bar = 0; bar < bars; bar++) {
    const barStart = bar * stepsPerBar;
    // A→A→B→B: bars 0-15 = Section A (twice), bars 16-31 = Section B (twice).
    const inSectionA = bar < 16;
    const sectionBar = bar % 8;
    const isPivot = isPivotA(sectionBar);
    let rootHz, fifthHz, leadRow;
    if (inSectionA) {
      rootHz  = isPivot ? NOTE_HZ['Eb2'] : NOTE_HZ['D2'];
      fifthHz = isPivot ? NOTE_HZ['Bb2'] : NOTE_HZ['A2'];
      leadRow = leadA[sectionBar];
    } else {
      rootHz  = isPivot ? NOTE_HZ['Bb2'] : NOTE_HZ['A2'];
      fifthHz = isPivot ? NOTE_HZ['F3']  : NOTE_HZ['E3'];
      leadRow = leadB[sectionBar];
    }
    for (let bi = 0; bi < 4; bi++) {
      cells[0][barStart + beats[bi]] = { hz: rootHz, patchName: 'TONE' };
      cells[1][barStart + beats[bi]] = { hz: fifthHz, patchName: 'TONE' };
      cells[2][barStart + beats[bi]] = { hz: leadRow[bi], patchName: 'TONE' };
    }
    if (inSectionA) {
      for (const beat of [2, 6, 10, 14]) cells[3][barStart + beat] = { patchName: 'SNARE' };
      for (const beat of [0, 4, 8, 12]) cells[3][barStart + beat] = { patchName: 'KICK' };
    } else {
      for (let st = 0; st < 16; st += 2) {
        cells[3][barStart + st] = { patchName: 'SNARE' };
      }
    }
  }
  return { name: 'NIDHOGG_BLAST', bpm, bars, stepsPerBar, cells };
}

// ---- Schedule song into a flat list of per-voice inc transitions ----
function scheduleSong(song) {
  const stepSec = (60 / song.bpm) / (song.stepsPerBar / 4);
  const totalSteps = song.bars * song.stepsPerBar;
  const totalSec = stepSec * totalSteps;
  const stepFrames = Math.max(1, Math.round(stepSec * 60));
  // Events: { voice, t, inc }. We'll sort by t at the end.
  const events = [];
  for (let s = 0; s < totalSteps; s++) {
    const tStep = s * stepSec;
    for (let v = 0; v < 4; v++) {
      const cell = song.cells[v][s];
      if (!cell) {
        // No event — let the previous cell's natural duration / silence
        // event control this voice. Writing inc=0 here would clobber a
        // held note's slope or stomp a still-ringing tone.
        continue;
      }
      // Look ahead for next non-rest event on this voice.
      let nextEventStep = totalSteps;
      for (let ns = s + 1; ns < totalSteps; ns++) {
        if (song.cells[v][ns]) { nextEventStep = ns; break; }
      }
      const framesUntilNext = (nextEventStep - s) * stepFrames;
      const isDrumCell = (cell.hz === undefined || cell.hz === null) && (cell.patchName !== 'TONE');
      if (isDrumCell) {
        const dp = DRUMS[cell.patchName];
        const holdFrames = Math.min(dp.duration, framesUntilNext);
        let freq = dp.freq_start;
        for (let f = 0; f < holdFrames; f++) {
          events.push({ voice: v, t: tStep + f * FRAME_SEC, inc: -hzToInc(freq) });
          freq = Math.max(50, Math.min(8000, freq + dp.freq_slope));
        }
        if (holdFrames < framesUntilNext) {
          events.push({ voice: v, t: tStep + holdFrames * FRAME_SEC, inc: 0 });
        }
      } else {
        // Tone cell — fixed timbre (slope 0, duration ≈ chord patch default
        // 20 frames). Mirroring sandbox getToneTimbre('TONE') fallback.
        const TONE_SLOPE = 0, TONE_DUR = 20;
        const holdFrames = Math.min(TONE_DUR, framesUntilNext);
        let freq = cell.hz;
        for (let f = 0; f < holdFrames; f++) {
          events.push({ voice: v, t: tStep + f * FRAME_SEC, inc: hzToInc(freq) });
          freq = Math.max(50, Math.min(8000, freq + TONE_SLOPE));
        }
        if (holdFrames < framesUntilNext) {
          events.push({ voice: v, t: tStep + holdFrames * FRAME_SEC, inc: 0 });
        }
      }
    }
  }
  events.sort((a, b) => a.t - b.t);
  return { events, totalSec };
}

// ---- Render: walk events, emit samples ----
function render(song, opts = {}) {
  const { events, totalSec } = scheduleSong(song);
  const tailSec = 0.2;
  const numSamples = Math.ceil((totalSec + tailSec) * SAMPLE_RATE);
  const samples = new Int16Array(numSamples);
  // Per-voice state.
  const incs = [0, 0, 0, 0];
  const phase = [0, 0, 0, 0];
  const lfsr = [0xACE1, 0xACE1, 0xACE1, 0xACE1];
  const lfsrAcc = [0, 0, 0, 0];
  // Per-voice mode: 0=tone, 1=noise, 2=rasp. Override via opts.modes.
  const modes = opts.modes || [0, 0, 0, 0];
  const raspCounter = [0, 0, 0, 0];
  const duty = [0.5, 0.5, 0.5, 0.5];
  const voiceGain = [0.85, 0.85, 0.85, 1.6];
  const amp = 0.45;
  const autoDuck = opts.autoDuck !== false;
  // Tremolo: rate Hz, depth 0..1. Applied to V0/V1/V2 tone voices.
  const tremRate  = opts.tremRate  !== undefined ? opts.tremRate  : 7;
  const tremDepth = opts.tremDepth !== undefined ? opts.tremDepth : 0.25;
  const tremPhase = [0, 0.33, 0.67, 0];
  let evIdx = 0;
  for (let i = 0; i < numSamples; i++) {
    const t = i / SAMPLE_RATE;
    while (evIdx < events.length && events[evIdx].t <= t) {
      const ev = events[evIdx++];
      incs[ev.voice] = ev.inc | 0;
    }
    // Step voices
    const bits = [0, 0, 0, 0];
    const any = [false, false, false, false];
    for (let v = 0; v < 4; v++) {
      const inc = incs[v];
      if (inc === 0) continue;
      // inc<0 = legacy noise-mode encoding.
      const effectiveMode = (inc < 0) ? 1 : modes[v];
      const incMag = (inc < 0) ? -inc : inc;
      let toneBit = 0;
      if (effectiveMode !== 1) {
        phase[v] = (phase[v] + incMag) & 0xFFFF;
        const threshold = (duty[v] * 65536) | 0;
        toneBit = (phase[v] < threshold) ? 1 : 0;
      }
      let lfsrBit = 0;
      if (effectiveMode === 1 || effectiveMode === 2) {
        if (effectiveMode === 1) {
          const old = lfsrAcc[v];
          const next = (old + incMag) & 0xFFFF;
          lfsrAcc[v] = next;
          if (next < old) {
            let l = lfsr[v];
            const lsb = l & 1;
            l >>= 1;
            if (lsb) l ^= 0xB400;
            lfsr[v] = l & 0xFFFF;
          }
        } else {
          // Rasp: shift LFSR every 4 samples for less-dense noise.
          if ((raspCounter[v] & 7) === 0) {
            let l = lfsr[v];
            const lsb = l & 1;
            l >>= 1;
            if (lsb) l ^= 0xB400;
            lfsr[v] = l & 0xFFFF;
          }
          raspCounter[v] = (raspCounter[v] + 1) & 0xFF;
        }
        lfsrBit = lfsr[v] & 1;
      }
      bits[v] = (effectiveMode === 0) ? toneBit
              : (effectiveMode === 1) ? lfsrBit
              : (toneBit | lfsrBit);  // rasp: OR (less destructive than XOR)
      any[v] = true;
    }
    const v3Noise = incs[3] < 0;
    const suppress012 = autoDuck && v3Noise;
    const a0 = any[0] && !suppress012;
    const a1 = any[1] && !suppress012;
    const a2 = any[2] && !suppress012;
    const a3 = any[3];
    // Tremolo modulation on V0/V1/V2 (matches worklet path).
    let tm0 = 1, tm1 = 1, tm2 = 1;
    if (tremDepth > 0) {
      const tremIncCycles = tremRate / SAMPLE_RATE;
      tremPhase[0] = (tremPhase[0] + tremIncCycles) % 1;
      tremPhase[1] = (tremPhase[1] + tremIncCycles) % 1;
      tremPhase[2] = (tremPhase[2] + tremIncCycles) % 1;
      const c0 = (Math.cos(tremPhase[0] * 2 * Math.PI) + 1) * 0.5;
      const c1 = (Math.cos(tremPhase[1] * 2 * Math.PI) + 1) * 0.5;
      const c2 = (Math.cos(tremPhase[2] * 2 * Math.PI) + 1) * 0.5;
      tm0 = (1 - tremDepth) + tremDepth * c0;
      tm1 = (1 - tremDepth) + tremDepth * c1;
      tm2 = (1 - tremDepth) + tremDepth * c2;
    }
    const ge0 = voiceGain[0] * tm0;
    const ge1 = voiceGain[1] * tm1;
    const ge2 = voiceGain[2] * tm2;
    const ge3 = voiceGain[3];
    // Fixed-divisor PFM mix — see sandbox worklet for rationale.
    const FIXED_DIVISOR = 4;
    const sum = (a0 ? bits[0] * ge0 : 0)
              + (a1 ? bits[1] * ge1 : 0)
              + (a2 ? bits[2] * ge2 : 0)
              + (a3 ? bits[3] * ge3 : 0);
    const halfTotal = ((a0 ? ge0 : 0) + (a1 ? ge1 : 0)
                     + (a2 ? ge2 : 0) + (a3 ? ge3 : 0)) * 0.5;
    const val = ((sum - halfTotal) / FIXED_DIVISOR) * amp * 2;
    samples[i] = Math.round(val * 32767);
  }
  return { samples, totalSec };
}

// ---- WAV writer ----
function writeWav(path, samples) {
  const dataBytes = samples.length * 2;
  const buf = Buffer.alloc(44 + dataBytes);
  buf.write('RIFF', 0, 'ascii');
  buf.writeUInt32LE(36 + dataBytes, 4);
  buf.write('WAVE', 8, 'ascii');
  buf.write('fmt ', 12, 'ascii');
  buf.writeUInt32LE(16, 16);  // fmt chunk size
  buf.writeUInt16LE(1, 20);   // PCM
  buf.writeUInt16LE(1, 22);   // mono
  buf.writeUInt32LE(SAMPLE_RATE, 24);
  buf.writeUInt32LE(SAMPLE_RATE * 2, 28);
  buf.writeUInt16LE(2, 32);
  buf.writeUInt16LE(16, 34);
  buf.write('data', 36, 'ascii');
  buf.writeUInt32LE(dataBytes, 40);
  for (let i = 0; i < samples.length; i++) {
    buf.writeInt16LE(samples[i], 44 + i * 2);
  }
  fs.writeFileSync(path, buf);
}

// ---- Main ----
// Pick which song to render via CLI arg: `node render_song.mjs nidhogg`
// Default: ildjarn. Special targets:
//   `node render_song.mjs single-d2`     pure D2 tone, 2 sec
//   `node render_song.mjs single-a2`     pure A2 tone, 2 sec
//   `node render_song.mjs single-f4`     pure F4 tone, 2 sec
//   `node render_song.mjs power-da`      D2 + A2 (no drums, no auto-duck)
//   `node render_song.mjs power-daf`     D2 + A2 + F4 (the NIDHOGG chord, no drums)

function makeSingleNote(hz, secs = 2) {
  // 16-step bar, BPM = 60/secs * 4 so the whole song is `secs` long.
  const bpm = 240 / secs;  // step = secs/16
  const stepsPerBar = 16, bars = 1;
  const cells = [
    new Array(stepsPerBar).fill(null),
    new Array(stepsPerBar).fill(null),
    new Array(stepsPerBar).fill(null),
    new Array(stepsPerBar).fill(null),
  ];
  cells[0][0] = { hz, patchName: 'TONE' };
  return { name: `SINGLE_${hz}HZ`, bpm, bars, stepsPerBar, cells };
}
function makeChord(hzs, secs = 2) {
  const bpm = 240 / secs;
  const stepsPerBar = 16, bars = 1;
  const cells = [
    new Array(stepsPerBar).fill(null),
    new Array(stepsPerBar).fill(null),
    new Array(stepsPerBar).fill(null),
    new Array(stepsPerBar).fill(null),
  ];
  for (let v = 0; v < hzs.length && v < 4; v++) {
    cells[v][0] = { hz: hzs[v], patchName: 'TONE' };
  }
  return { name: `CHORD_${hzs.join('_')}`, bpm, bars, stepsPerBar, cells };
}
// Important: tone duration is 20 frames by default (TONE_SLOPE=0, TONE_DUR=20).
// For a debug sustained tone we need to override — use a long-duration patch.
// Easiest: set TONE_DUR larger by directly hacking. Cleaner: bake a tone cell
// that re-articulates every step so the note rings the whole bar.
function makeChordSustained(hzs, secs = 2) {
  const bpm = 240 / secs;
  const stepsPerBar = 16, bars = 1;
  const cells = [
    new Array(stepsPerBar).fill(null),
    new Array(stepsPerBar).fill(null),
    new Array(stepsPerBar).fill(null),
    new Array(stepsPerBar).fill(null),
  ];
  for (let v = 0; v < hzs.length && v < 4; v++) {
    for (let s = 0; s < stepsPerBar; s++) {
      cells[v][s] = { hz: hzs[v], patchName: 'TONE' };
    }
  }
  return { name: `CHORD_SUSTAINED_${hzs.join('_')}`, bpm, bars, stepsPerBar, cells };
}

// Parse args: positional = song name; --snare=A..E swaps SNARE patch;
// --trem-rate=N --trem-depth=F overrides tremolo defaults;
// --rasp=0,2 puts those voices in rasp mode (tone XOR LFSR).
const positional = [];
let snareVariant = null;
let tremRateArg = null;
let tremDepthArg = null;
let raspVoices = null;
for (const arg of process.argv.slice(2)) {
  if (arg.startsWith('--snare=')) {
    snareVariant = arg.slice('--snare='.length).toUpperCase();
  } else if (arg.startsWith('--trem-rate=')) {
    tremRateArg = +arg.slice('--trem-rate='.length);
  } else if (arg.startsWith('--trem-depth=')) {
    tremDepthArg = +arg.slice('--trem-depth='.length);
  } else if (arg.startsWith('--rasp=')) {
    raspVoices = arg.slice('--rasp='.length).split(',').map(s => +s);
  } else {
    positional.push(arg);
  }
}
if (snareVariant) {
  if (!SNARE_VARIANTS[snareVariant]) {
    console.error(`Unknown snare variant: ${snareVariant}. Try: ${Object.keys(SNARE_VARIANTS).join(', ')}`);
    process.exit(1);
  }
  DRUMS.SNARE = SNARE_VARIANTS[snareVariant];
}

const which = (positional[0] || 'ildjarn').toLowerCase();
const songFactories = {
  ildjarn:  () => makeIldjarnDrone(),
  nidhogg:  () => makeNidhoggBlast(),
  'single-d2': () => makeSingleNote(NOTE_HZ['D2']),
  'single-a2': () => makeSingleNote(NOTE_HZ['A2']),
  'single-f4': () => makeSingleNote(NOTE_HZ['F4']),
  'power-da':  () => makeChordSustained([NOTE_HZ['D2'], NOTE_HZ['A2']]),
  'power-daf': () => makeChordSustained([NOTE_HZ['D2'], NOTE_HZ['A2'], NOTE_HZ['F4']]),
};
const factory = songFactories[which];
if (!factory) {
  console.error(`Unknown song: ${which}. Try: ${Object.keys(songFactories).join(', ')}`);
  process.exit(1);
}
const song = factory();
// Auto-duck off by default for ensemble renders — matches the seed
// song default (autoDuck=false). Constant ducking masked the chord
// arrangement; songs sound better with all voices ringing through.
const enableDuck = false;
const renderOpts = { autoDuck: enableDuck };
if (tremRateArg  !== null) renderOpts.tremRate  = tremRateArg;
if (tremDepthArg !== null) renderOpts.tremDepth = tremDepthArg;
if (raspVoices) {
  renderOpts.modes = [0, 0, 0, 0];
  for (const v of raspVoices) {
    if (v >= 0 && v <= 3) renderOpts.modes[v] = 2;  // 2 = rasp
  }
}
const { samples, totalSec } = render(song, renderOpts);
const tags = [];
if (snareVariant) tags.push(`snare${snareVariant}`);
if (tremRateArg !== null || tremDepthArg !== null) {
  const r = tremRateArg !== null ? tremRateArg : 7;
  const d = tremDepthArg !== null ? tremDepthArg : 0.25;
  tags.push(`trem${r}-${d}`);
}
if (raspVoices) tags.push(`rasp${raspVoices.join('')}`);
const variantTag = tags.length ? '_' + tags.join('_') : '';
const outName = `out_${which}${variantTag}.wav`;
writeWav(outName, samples);
console.error(`Rendered ${song.name}${tags.length ? ` [${tags.join(', ')}]` : ''}: ${totalSec.toFixed(2)}s → ${outName}`);

// Also dump a schedule trace so we can sanity-check what's happening.
const { events } = scheduleSong(song);
const traceLines = ['# t_sec | voice | inc | meaning'];
// Dump everything — useful for diagnosing end-of-song freezes.
for (const ev of events) {
  let meaning;
  if (ev.inc === 0) meaning = 'silence';
  else if (ev.inc > 0) {
    const hz = Math.round(ev.inc * SAMPLE_RATE / 65536);
    meaning = `tone ${hz} Hz`;
  } else {
    const hz = Math.round((-ev.inc) * SAMPLE_RATE / 65536);
    meaning = `noise ${hz} Hz`;
  }
  traceLines.push(`${ev.t.toFixed(4)} | V${ev.voice} | ${ev.inc} | ${meaning}`);
}
const traceName = `schedule_${which}.txt`;
fs.writeFileSync(traceName, traceLines.join('\n') + '\n');
console.error(`Wrote schedule trace (first 200 events) to ${traceName}`);
