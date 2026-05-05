#!/usr/bin/env node
// Quick spectrum analysis: read a 16-bit mono WAV, run autocorrelation
// to find the dominant period, and report it as Hz. Used to verify
// "what does this WAV actually sound like" against the intended pitch.
//
// Usage: node analyze_wav.mjs out_single-d2.wav
//
// For chord WAVs (multiple tones XOR'd) the autocorrelation peak picks
// the strongest underlying period — usually the lowest fundamental.
// We also dump the first ~30 zero-crossings and time gaps so you can
// see exactly what frequency the speaker is being driven at.

import fs from 'node:fs';

const path = process.argv[2];
if (!path) {
  console.error('Usage: node analyze_wav.mjs <file.wav>');
  process.exit(1);
}

const buf = fs.readFileSync(path);
const sampleRate = buf.readUInt32LE(24);
const dataOffset = 44;
const numSamples = (buf.length - dataOffset) / 2;
const samples = new Float32Array(numSamples);
for (let i = 0; i < numSamples; i++) {
  samples[i] = buf.readInt16LE(dataOffset + i * 2) / 32768;
}

console.log(`File: ${path}`);
console.log(`Sample rate: ${sampleRate} Hz, ${numSamples} samples (${(numSamples/sampleRate).toFixed(2)}s)`);

// --- Autocorrelation peak in [50, 1500] Hz range ---
function autocorr(samples, lag) {
  let sum = 0;
  const n = Math.min(samples.length - lag, sampleRate);  // 1 second window max
  for (let i = 0; i < n; i++) sum += samples[i] * samples[i + lag];
  return sum / n;
}
let bestLag = 0, bestVal = -Infinity;
const minLag = Math.floor(sampleRate / 1500);  // 1500 Hz max
const maxLag = Math.floor(sampleRate / 50);    // 50 Hz min
for (let lag = minLag; lag <= maxLag; lag++) {
  const v = autocorr(samples, lag);
  if (v > bestVal) { bestVal = v; bestLag = lag; }
}
const dominantHz = sampleRate / bestLag;
console.log(`Autocorrelation peak: lag ${bestLag} samples = ${dominantHz.toFixed(1)} Hz`);

// --- Zero-crossing detection ---
const crossings = [];
for (let i = 1; i < Math.min(samples.length, sampleRate); i++) {
  if (samples[i - 1] < 0 && samples[i] >= 0) crossings.push(i);
}
console.log(`Found ${crossings.length} positive zero-crossings in first 1s`);
if (crossings.length >= 2) {
  // Median gap between crossings = half period.
  const gaps = [];
  for (let i = 1; i < crossings.length; i++) gaps.push(crossings[i] - crossings[i - 1]);
  gaps.sort((a, b) => a - b);
  const medianGap = gaps[Math.floor(gaps.length / 2)];
  const fundFromCrossings = sampleRate / medianGap;
  console.log(`Median ZC gap: ${medianGap} samples → fundamental ~${fundFromCrossings.toFixed(1)} Hz`);
  console.log(`First 10 crossings (sample indices): ${crossings.slice(0, 10).join(', ')}`);
  console.log(`First 10 ZC gaps: ${gaps.slice(0, 10).join(', ')}`);
}

// --- Naive peak-pick FFT (manual, on 4096 samples) ---
// Just for sanity. Window from sample ~500 (skip the silence/transient
// at the start) to 500+4096.
const N = 4096;
if (numSamples >= 500 + N) {
  // Box DFT at integer Hz from 50 to 1500. Slow but adequate for one-shot.
  const buckets = [];
  for (let hz = 50; hz <= 1500; hz += 1) {
    let re = 0, im = 0;
    for (let i = 0; i < N; i++) {
      const t = i / sampleRate;
      const phase = 2 * Math.PI * hz * t;
      re += samples[500 + i] * Math.cos(phase);
      im += samples[500 + i] * Math.sin(phase);
    }
    buckets.push({ hz, mag: Math.sqrt(re*re + im*im) });
  }
  buckets.sort((a, b) => b.mag - a.mag);
  console.log('Top 5 spectral peaks:');
  for (const b of buckets.slice(0, 5)) {
    console.log(`  ${b.hz} Hz: mag ${b.mag.toFixed(1)}`);
  }
}
