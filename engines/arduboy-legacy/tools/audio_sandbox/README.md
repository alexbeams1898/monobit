# audio_sandbox

A browser-based scratchpad for tuning 1-bit **beeps and chords**. No
build, no toolchain — just open `index.html` in a browser.

A "beep" is the project's catch-all for one of these
`{freq_start, freq_slope, duration}` triples — what the engine's
`audio::Sfx` struct holds. We call them beeps because that's what they
are: the speaker is a single bit, every effect is literally a beep with
a slope. SFX, melody notes, drone clicks — all beeps.

Two modes:

- **SFX mode** (default) — single voice, classic beep tuning. What the
  sandbox started as. Output mirrors `engine/audio.cpp`'s tick stepper
  and `platform/sdl/audio.cpp`'s square-wave generator.
- **CHORD mode** — three voices, XOR-mixed. Same algorithm as the
  Arduboy ISR and SDL backend (3-voice phase-accumulator XOR). Use it
  to audition power chords, detuned voicings, octave doubles. Each
  voice is its own `Sfx` and can be muted individually.

## Why this exists

Tuning the three numbers by ear means: edit C++ → `make` → `make ardens`
→ click around → maybe rebuild SDL → repeat. A 30-second iteration loop.

This sandbox does the same synthesis in WebAudio with sliders. Iteration
loop becomes ~1 second.

## Use

```
start tools/audio_sandbox/index.html      # opens default browser
```

Sliders adjust the three beep parameters live. PLAY hears the current
shape. PLAY × 3 reproduces "rapid scroll" timing (3 plays, 120 ms apart).

## Beeps panel

One unified list. On a fresh browser, the list is seeded with the values
currently shipped in `games/rpg/game.cpp`; from then on every beep is
fully editable and persisted to `localStorage`. Edits here do NOT touch
the repo.

Each entry is a [▶] [NAME] pair:

- **▶** plays the beep. Nothing else.
- **NAME** selects the beep (highlights orange). Nothing else.

### Action buttons

- **Save current as new** — name it, save current slider values as a
  fresh beep.
- **Load selected → sliders** — pull the selected entry's params into
  the live sliders.
- **Clone selected** — duplicate the selected entry into `<ORIG>_COPY`.
- **Update selected** — overwrite the selected beep with current slider
  values.
- **Delete selected** — remove the selected beep.

To get a beep back to its original `game.cpp` values, delete it (or
rename / clone it aside) and reload the page — the seed list will
re-add it.

### Shipping a beep into the game

When you've tuned something you like, the C++ paste line is shown at
the bottom of the Beeps panel:

```
const audio::Sfx SFX_NEW PROGMEM = {700, -120, 3};
```

Drop into `games/rpg/game.cpp` as a NEW symbol — don't overwrite the
in-game default until you've A/B-tested. The sandbox's localStorage and
the repo's source are intentionally not synced.

## Three play buttons, three meanings

- **▶ Preview live slider values** (next to the sliders) — plays the
  exact current slider state. Never touches any saved beep.
- **▶ next to a beep's name** — plays that saved beep without touching
  the sliders.
- **PLAY** (top of Playback panel) — same as Preview live slider
  values; kept for the SPACE-key habit.

The sliders are your dirty workspace. Preview to hear it. Save / Update
to commit.

## Undo / redo

Slider edits and beep-loads push onto a 50-entry stack. Drags are
debounced (one entry per ~250 ms idle) so a single drag isn't 200 entries.

- ↶ Undo button or **Ctrl/Cmd+Z**
- ↷ Redo button or **Ctrl/Cmd+Shift+Z** / **Ctrl/Cmd+Y**

Status text shows `(N/M states)` so you know stack depth.

## Faithfulness

Synthesis here mirrors `engine/audio.cpp`'s tick stepper exactly: first
tick after `play()` is a no-op (the `start_pending` hold), each
subsequent tick applies the slope, frequencies clamp to 50–8000 Hz.
What you hear here should match what comes out the SDL build or the
Arduboy speaker.

If something sounds different here vs Ardens, the SDL audio backend is
the more likely culprit (per-callback target_hz reads, audio-buffer
latency). Not this sandbox.

## What this sandbox does NOT cover

- Duty cycle (timbre / "growl" knob) — not a runtime knob yet, on the
  list right after the first multi-voice song lands.
- Per-note vibrato / glide / accent — Follin-style expression, future
  work; the engine accepts only `{freq_start, freq_slope, duration}`
  today.
- Note-list / pattern authoring (a real DAW UI) — the chord mode lets
  you tune voicings, but composing a multi-bar song is still a hand-
  edit-the-C-array job. Tracker UI is on the list.
- The actual game's input timing — re-trigger drops, frame-budget
  delays. Use the SDL build's `--record-audio=PATH` flag for that.

## Keyboard

- **SPACE** — play current sliders
- **SHIFT+SPACE** — play × 3 (rapid)
- **ESC** — stop
- **Ctrl/Cmd+Z** — undo
- **Ctrl/Cmd+Shift+Z** / **Ctrl/Cmd+Y** — redo
