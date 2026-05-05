# Future platform — back-burner

Captured 2026-04-25 during the audio-engine planning session. Not a
commitment, not a roadmap — a holding doc so the conversation isn't lost.

## The idea

A monobit-defined fantasy 1-bit handheld. Like Pico-8 in spirit (define
the spec, ship games for it, the spec IS the brand) but 1-bit and
dark-fantasy ARPG-shaped instead of cartoon and arcade-shaped.

Identity: **"Every monobit game is hand-tuned for a constraint we
control, in a 1-bit aesthetic we control, with a 1-bit audio engine
we control."** The constraint is the brand. The hardware is optional —
emulator-first, hardware later (or never).

## Why this is on the back burner, not active

Current pain isn't the display dimensions or the platform-ownership
question. It's flash + RAM headroom for content. **Arduboy FX solves
that today.** Going to a hand-rolled spec adds a refactor (canonical-
emulator work in `platform/sdl/`, new platform-spec doc, port-vs-
canonical split for UI) before any new content can land. Not the right
trade right now.

If/when we revisit, the trigger is one of:
- Arduboy FX itself becomes too constrained (16 MB SPI fills up)
- A real opportunity to ship hardware appears (partner, fab connection,
  retail interest)
- The brand needs more independence from Arduboy as a platform

## Sketch of what the spec might look like

If we ever do this:

- **Display: 128×64, 1-bit**. Same as Arduboy. Going larger (192×96 was
  considered) creates a port-vs-canonical UI split that doubles layout
  work for arguable gain — the gameplay viewport on Arduboy FX would
  still be 128×64 either way, since downscaling 1-bit pixel art looks
  bad. Keeping the canonical at 128×64 preserves the pixel-perfect
  port to FX.
- **RAM: ~16 KB** (6× ATmega32u4) — comfortable for entity pools,
  audio mixer state, dialog buffers.
- **Code flash: ~128 KB** (4× ATmega32u4 usable) — engine + game logic
  with real headroom.
- **Data flash: 16 MB SPI** — matches Arduboy FX exactly. Songs,
  sprites, dialog, level data live here.
- **Audio: single-pin 1-bit speaker, software-mixed** — preserves the
  Tim Follin / 1-bit music identity.
- **Input: D-pad + A/B (+ Start/Select?)** — Arduboy-compatible.

This is essentially *"Arduboy FX with 6× RAM and 4× code flash."* If
we ever build hardware, an RP2040 ($3 chip) overshoots this spec by
a wide margin. Plausible but not committed.

## The Pico-8 framing

The reason Pico-8 works: the constraint is external-feeling because
the author committed to it forever. **Spec inflation is the death
spiral** — the moment you start loosening when content gets tight,
you're back to "it's whatever fits" and the brand evaporates.

If/when monobit-virtual exists, the discipline mechanism would be:
- Spec doesn't change for at least 12 months after publication
- Spec changes require a hardware revision (if hardware exists) or a
  major version bump (if not)
- Existing games stay on the original spec

## Anti-goals (the things that would break the brand)

- Tiered experience across platforms. PC version with more content
  than FX version = "FX is the lesser tier" = brand collapse.
- 1-bit downscaling (looks bad, kills the aesthetic).
- Spec inflation when content gets tight. Get clever within the spec
  instead.

## What we're doing instead, for now

Arduboy FX is canonical. Stock Arduboy is no longer guaranteed (FX
upgrade kits exist; newer Arduboys ship with FX built in). PC and iOS
remain primary revenue platforms per `business-model.md`.

The audio engine, content additions, sprite expansion — all sized for
Arduboy FX limits (28 KB internal code flash, 16 MB external data
flash). When that wall is hit, we revisit this doc.
