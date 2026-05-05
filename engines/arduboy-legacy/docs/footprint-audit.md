# Footprint audit — 2026-04-25

Snapshot of where flash and RAM are going, what's slack, and what
"get clever before considering Arduboy FX" actually buys us.

Captured at commit `79167a4` (post-cand-1 + perf hooks + cand-2 + cand-3
+ stack-collision fix). Audit driven by the future audio-engine plan
(2-voice expression SFX + 3-voice music + ~30 sec song data) needing
~1.5–2 KB flash and ~50 B RAM.

## Top-level numbers

| | Used | Cap | Headroom |
|---|---|---|---|
| Flash | 26,660 B | 28,672 B | **2,012 B** |
| RAM   | 2,139 B | 2,560 B | **421 B (40 B from gate)** |

Flash budget is real but not the bottleneck. **RAM is the wall** — the
85% gate fires at 2,176 B, leaving only 40 B of true headroom before
adding anything new.

## Flash spend (top 10 consumers)

| Symbol | Bytes | Notes |
|---|---|---|
| `main` | 8,350 | Inlined entire game loop + all `case STATE: draw_X()` dispatches. After LTO this is one giant function. Hard to attack directly. |
| `game::update` | 4,870 | Per-state input handlers, menu cursors, gameplay logic. |
| `images::TITLE_data` | 1,024 | Full 128×64 raw image. Never compressed — used only on title splash. **Compression candidate.** |
| `images::GATE_PALETTE_data` | 544 | Already palette-compressed; further wins speculative. |
| `images::FOREST_LZ77_data` | 348 | Pre-masked + LZ77'd in cand-3. |
| `draw_menu_pgm` | 374 | Pause menu renderer. |
| Boss LZ77 streams (×9) | ~1,441 | Already LZ77'd in cand-1. |
| Font glyph table | 176 | 5×7 fixed glyphs. Tight. |
| Various `draw_*` | ~1,500 | Per-state render code. Touchpoint for incremental shrinks. |

Implication: code is the bulk (~60%), data is ~25%, the rest is render
helpers. **The biggest data target left is TITLE_data at 1024 B raw.**

## RAM spend

| Symbol | Bytes | Notes |
|---|---|---|
| `fb::buffer` | 1,024 | Framebuffer. Required, immutable. |
| `ent::pool[24]` | 384 | 24 × 16 B Entity. Worst-case audited at ~20 active in combat — **24 is right-sized**, not slack. |
| `sprites::LZ77_CACHE` | 276 | Boss + logo decode buffer. Sized for Lucifer (46×44). **Slack candidate**: shrink to 128 B + stack-decode big bosses. |
| `meta` (storage::MetaCharacter) | 24 | Persistent character. Required. |
| `CSWTCH.515` / `CSWTCH.619` | 17 | Compiler-synthesized `.data` jump tables. ~5 places they could be eliminated by switch→PROGMEM-table refactors, ~17 B reclaimable but tedious. |
| Dirty-cache sentinels (`last_*`) | ~25 | Per-screen 0xFF-init bytes. Could bit-pack but uglifies dirty-check code substantially. |
| Audio state | ~14 | Will grow with new engine. |
| Misc per-state | ~50 | `state`, `wave`, `menu_index`, etc. Each 1–2 B; not compressible without intrusive changes. |

## Reality check on FX

Arduboy **FX** has 16 MB **external SPI flash** for data — not extra
program flash. Implications:

- **Internal program flash stays at 32 KB.** Code, ISRs, hot draw paths
  must live there. The 16 MB is data-only.
- What FX would unlock: PROGMEM data (sprites, song bytes, text strings)
  can move to external flash, freeing internal program flash for code.
- **Cost**: SPI reads are ~10× slower than internal flash. Audio ISR
  (~16 kHz) must stay internal; song stepper (slow) is fine on FX.

If we moved all data to FX:
- TITLE_data (1,024 B) + GATE_PALETTE (544 B) + FOREST_LZ77 (348 B)
  + boss LZ77 (~1,441 B) ≈ **+3,357 B internal flash freed.**
- Songs go on FX → effectively unlimited music data.

But: the FX SPI driver costs ~150 B internal flash, and player install
base is smaller. Per `business-model.md` the stock Arduboy is the
default platform.

**Verdict**: defer FX until we hit a real wall. The audit below shows
we have ~1.5 KB of "get clever" wins available without FX.

## Cleverness roadmap (do these before FX)

Ordered by effort/payoff. Each is a candidate session of work.

### 1. LZ77 cache shrink — ~30 B RAM (much smaller than first thought)

`LZ77_CACHE[276]` is sized for Lucifer (46 cols × 6 pages = 276 B).
**Re-audit of actual sprite sizes** found the cache is closer to the
median than the max:

| Sprite | Decoded size |
|---|---|
| Charon | 130 |
| Phlegyas | 150 |
| Medusa, Geryon | 200 |
| Cerberus | 220 |
| Minos | 230 |
| Plutus, Minotaur | 240 |
| LOGO_TITLE, LOGO_SECOND_DEATH | 246 |
| **Lucifer** | **276** |

Realistic options:
- Shrink to 246 B, stack-decode Lucifer only. **Saves 30 B RAM.**
- Shrink to 240 B, stack-decode Lucifer + both logos. ~36 B RAM but
  8 of 11 LZ77 sprites need stack-decode at sizes that risk our
  ~400 B stack headroom during boss combat. Not safe.
- Shrink to 130 B + stack-decode 9 of 11 sprites: would save ~146 B
  RAM but blows the stack on every boss fight. Don't do this.

The original audit's "~150 B RAM win" claim assumed the cache was
sized for ONE outlier (Lucifer). It's actually sized close to the median;
real safe win is ~30 B. **Low cost/benefit — deferred or skipped entirely.**

### 2. Compress base enemy sprites via LZ77 — ~~~30–50 B flash~~ NOT WORTH

12 base enemies at 8 B raw each = 96 B. **Measured:** LZ77 on 8-byte
payloads adds 2 B per sprite of overhead (literal-block tag + END
terminator). 48 B raw → 60 B encoded individually. Even
concatenating all 12 into one stream: 48 → 50 B (still bigger).

The audit's "30–50 B savings" estimate didn't account for LZ77's
per-stream overhead. RLE doesn't help either: 91% of byte runs are
length-1 (visually distinct sprites, low byte repetition).

**Skipped.** Tiny pixel-art sprites have no compression handle worth
using.

### 3. Compress TITLE_data — ~300–500 B flash

The 1,024 B title image is full-screen and detailed but only renders
on the splash screen. Pre-mask + LZ77 like FOREST_LZ77 should hit
similar compression ratios (FOREST went 1,024 → 348 B).

Decode-into-fb on title-render is acceptable; title is dirty-once.

**Largest single win on this list.**

### 4. Reuse boss-cache as audio-engine scratchpad — ~50–100 B effective RAM

`LZ77_CACHE` is unused outside PLAYING (95% of playtime). Audio engine
state (~50 B) could squat there during MAIN_MENU, TITLE, etc., and
yield it back when a boss spawns.

This is a memory-multiplexing trick, not a true shrink — only works if
audio doesn't need to play during boss combat with a different sound
than at boss spawn time. Worth modeling carefully before implementing.

### 5. Bit-pack dirty-cache sentinels — ~15–20 B RAM

~25 single-byte `last_*` vars compare against current state. Most are
small enums or booleans. Packing into a few u32s saves bytes but adds
shift/mask overhead in every dirty check.

Probably worth doing only if (1)+(4) aren't enough. Marginal payoff for
ugliness cost.

### 6. Switch → PROGMEM-table refactors for CSWTCH — ~17 B RAM

The 11 B and 6 B `CSWTCH.*` symbols come from large `switch` statements
the compiler chose to implement as jump tables. We did this for
`sprites::lz77_data` already (cand-1). Two more candidates worth
finding via `avr-objdump`. Each is small; collectively ~17 B.

## Total available without FX

| Win | Flash | RAM | Status |
|---|---|---|---|
| (1) LZ77 cache shrink | -10 | +30 | skipped — payoff too small |
| (2) Enemy sprite LZ77 | 0 | 0 | skipped — LZ77 overhead bigger than savings |
| (3) TITLE_data LZ77 | **+118** | 0 | **shipped** (3c29719) |
| (3b) GATE tile 0 empty-by-convention | **+22** | 0 | **shipped** (bd29d5f) |
| (4) Audio scratchpad reuse | 0 | +~50 effective | deferred — design alongside audio engine |
| (5) Sentinel bit-pack | -10 | +15 | pending — fragile |
| (6) CSWTCH refactors | 0 | +17 | pending |
| **Shipped so far** | **+140** | 0 | |

Reality after audit: (3) and (3b) are the real wins. The other items
are smaller than the audit predicted. The remaining cleverness budget
is ~+50 B RAM (item 4, when audio lands) and ~+30 B from sentinel +
CSWTCH work. Total realistic ceiling: **~+150-180 B flash, +80 B RAM**
beyond what we have today.

**Next constraint check** (audio engine + 7 redesigned SFX + 1 song):
- audio engine flash budget: 1.0–1.5 KB
- 7 SFX redesigned with expression: ~150–250 B
- 1 short song (30 sec, mono): ~250–400 B
- Total: **~1.4–2.1 KB**

Current flash headroom: 28,672 - 26,520 = **2,152 B**. That fits the
**bottom of the range**. The moment songs grow past ~400 B (i.e., a
real album of 4+ tracks, multi-circle music) we hit the wall. **At
that point FX is the answer.**

## Other PROGMEM data surveyed (no further wins)

- **Enemy sprites (8 B each)**: too small for LZ77 (per-stream overhead
  exceeds compressible content). 91% of byte runs are length-1, so RLE
  also fails.
- **Boss LZ77 streams**: already compressed in cand-1.
- **Logos**: already LZ77 (cand-1 inheritance).
- **Glyph table** (176 B): tightly packed 5×7 fonts; nothing to compress.
- **Tile palettes** beyond GATE: we have one (just GATE).

If the audio engine landings push past those numbers, that's when FX
becomes the right answer. Until then, get clever.

## What this doc does NOT cover

- The actual audio engine design (separate doc when we get there).
- Whether the music data shape benefits from a custom compression
  scheme over LZ77 (likely yes — patterns + order list compress music
  better than generic LZ77 on the same bytes).
- Future content additions (NPCs, dialog icons) — those plans need
  their own audit when they land.
