# Business model & platform priority

## What monobit is

A studio shipping severely-constrained 1-bit games at $1 each. Every
title is built around a 128×64 1-bit display, no greys/shading/
transparency, and a single-pin 1-bit speaker for both music and SFX.

The constraint is the product. Customers are buying rigor and
restraint — the 1-bit art and 1-bit music are the brand identity, not
"an indie game that happens to be monochrome."

## Platform priority

**PC (SDL2) and iOS are the products.** Steam / itch.io / App Store
are where revenue comes from. These are where most players will
actually play.

**Arduboy FX is the canonical hardware target and the proof-of-rigor.**
"This game runs on a $70 microcontroller" is marketing. Every title
must boot and play on Arduboy FX (real hardware or Ardens emulator).
If a feature breaks that, the feature is wrong.

Stock Arduboy (no FX) is no longer a guaranteed target. Modern
Arduboys ship with FX built in; older ones can be upgraded. The
audio engine + content scope make stock-Arduboy compat actively
hurt the games — the brand promise survives because Arduboy FX is
still a Harvard-architecture 8-bit microcontroller doing the heavy
lifting. The "rigor" pitch is that the runtime fits in 28 KB of
internal code flash and 2.5 KB of SRAM, not that all data fits there.
Data (sprites, songs, dialog, level layouts) lives on the FX's 16 MB
external SPI flash.

This means the Arduboy FX build is a **correctness constraint**, not
the primary dev target. Day-to-day iteration happens on the PC build
where the debugger, hex editor, and file-backed saves are usable.
Smoke-test on Arduboy FX / Ardens periodically.

## What the constraint applies to

**Gameplay logic caps are platform-independent.** Max ichor, max
waves, max entities, save schema — all sized for Arduboy FX. The PC
version does NOT get "expanded" caps. Two reasons:
1. The brand promise is "the runtime fits ATmega32u4 + FX." Breaking
   it on PC is a lie.
2. Shipping two games (lite Arduboy, full PC) makes the Arduboy
   version feel like the lesser tier. It is not lesser — it is
   canonical.

**What PC/iOS DO get** (things invisible across platforms):
- Save file portability / cloud sync
- Achievements, leaderboards
- Window scaling, keyboard/mouse/touch bindings
- Higher-fidelity audio rendering of the same 1-bit square waves
- Dev tooling (hex-editing saves, replays, profiling)

**What PC/iOS do NOT get:** new game logic, more content, bigger
worlds, different mechanics. If it affects what the player does,
it has to also work on Arduboy.

## The engine/platform split

Given this model, the engine/platform split is load-bearing, not a
nice-to-have. `engine/` is the game. `platform/<name>/` is the
render/input/clock/storage/audio backend for a specific target.

Current + planned platforms:
- `platform/arduboy/` — ATmega32u4 + SSD1306 + FX SPI flash, AVR-specific
- `platform/sdl/` — SDL2 window, file-backed saves, for PC dev + release
- `platform/ios/` — later, probably SDL2-based

A future "monobit-virtual" platform (own spec, own emulator, possibly
own hardware later) is sketched in `docs/future-platform.md`. Not on
the current roadmap; documented so the conversation isn't lost.

**Engine purity rule:** `engine/` has zero platform-specific includes
(no `<avr/*>`, no `<SDL.h>`, no `<windows.h>`). Anything hardware-flavored
goes behind an abstraction in `engine/` and is implemented per-platform.

Existing abstractions:
- `engine/flash.h` — read from read-only storage (PROGMEM on AVR,
  plain pointer deref on PC)
- `engine/storage.h` — persistent key-value (EEPROM on AVR, file on PC)
- `engine/clock.h`, `engine/framebuffer.h` (backed by `display`),
  `engine/input.h`, `engine/audio.h` — all platform-implemented

## Anti-goals

- Don't add Arduboy-only features that the PC version can't mirror.
- Don't add PC-only features that change gameplay.
- Don't "tier" the experience across platforms — no PC-only content,
  no Arduboy-lite mode.
- Don't treat Arduboy as legacy. It is the forcing function.
