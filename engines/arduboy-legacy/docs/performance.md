# Performance notes — what we do to fit in the Arduboy

The ATmega32u4 gives us **32 KB flash + 2.5 KB SRAM**. Every technique in
this document exists because one of those two numbers was about to be
broken. This is the running record of choices, in descending order of
impact.

Rules of thumb we obey everywhere:

- **Flash is cheap; RAM is precious.** RAM holds the stack too — if
  `.data + .bss` swells, the stack collides and we get silent
  corruption. Prefer PROGMEM + a small RAM scratch over "just keep it in
  RAM."
- **Measure before and after every change.** The Makefile reports flash
  and RAM on every `make`; `make audit` runs the progmem-violation
  check + top-20 `.data`/`.bss`/`.rodata` report. Numbers go in the
  commit message.

---

## Build toolchain settings

| Flag | What it buys |
|---|---|
| `-Os` | Optimize for size. Non-negotiable. |
| `-flto` | Link-time optimization — cross-TU dead-code + inlining. **Saved ~1.1 KB flash** on this project by itself. |
| `-ffunction-sections -fdata-sections` + `-Wl,--gc-sections` | Each function / global gets its own section; linker drops unused ones. Essential for LTO to show its teeth. |
| `-fno-exceptions -fno-rtti -fno-threadsafe-statics` | Strips C++ features we don't use. Saves kilobytes. |

## Language subset

- No exceptions, no RTTI, no STL containers.
- No `new` / `delete` / `malloc`. Only fixed-size arrays and pools.
- Custom integer typedefs (`u8`, `i16`, etc. in `engine/types.h`) — AVR
  `int` is 16 bits, which mis-sizes portable code written assuming
  32-bit. Typedefs avoid accidental 32-bit arithmetic.

## Flash-resident data (PROGMEM)

**Every constant table that outlives a function goes in PROGMEM** via
the portable shim `engine/progmem.h`. The shim expands to AVR macros on
hardware and to plain pointers on PC, so game/engine code looks the same
everywhere.

Big wins to date:

- **Sprite / glyph tables** — all 1-bit art in `games/rpg/sprites.cpp`.
- **Bestiary names + descriptions** — ~210 + 576 B. Would have burned
  ~200 B of RAM otherwise.
- **Text screen bodies** — CONTROLS, LEXICON, DESCENT, GUIDE plus the
  list titles. Decoded into a ~40 B stack buffer one line at a time.
- **Menu option strings** — main menu, pause menu, confirm dialog.
  **Saved ~124 B RAM** in one commit.
- **Poem tables, Roman numerals, per-circle enemy/boss lookups.**

Rule: if a `const char* const X[]` holds more than a few strings, each
string longer than ~8 chars, it belongs in PROGMEM. `scripts/check_ram.py`
fails the build over 85 % and helps catch regressions.

### The PROGMEM audit rule

`make audit` runs `scripts/check_progmem_violations.py`. It scans
`engine/` and `games/` for raw `#include <avr/pgmspace.h>` and fails if
any file outside `platform/arduboy/` uses the header directly. All code
must go through `engine/progmem.h` so the same files compile on PC.

## Streaming decompression for the circle backgrounds

Nine full-screen backgrounds, one per circle of Hell. Naive storage:
9 × 1024 B = **9216 B of flash**, zero RAM. We got them down to
**~4.5 KB flash, 0 B RAM**. Pipeline (`scripts/convert_circle_backgrounds.py`):

1. **Atkinson dither** to 1-bit — preserves pure-black regions (the
   spaces the player and enemies stand out against).
2. **2×2 checker mask.** Every pixel where `(x + y)` is even is forced
   off. This halves perceived brightness — the entity sprites remain
   solid-white against a stippled background.
3. **Nibble pack.** The checker guarantees that in any framebuffer byte,
   exactly 4 of 8 bit positions are permanently zero (the pattern is
   `0xAA` for even columns, `0x55` for odd). So we keep only the 4 bits
   that could be non-zero and pack two columns' nibbles into one byte.
   **512 B per tile** (was 1024 B).
4. **Zero-run + literal-run RLE** on the nibble bytes. Tag-byte scheme:
   `0b1NNNNNNN` = run of N zero bytes; `0b0LLLLLLL` = literal run of
   L bytes; `0x00` = end of stream. The nibble form leaves long runs
   of zeros at page edges, so RLE averages to **~470 B per tile**.
5. **Zero-cache streaming decoder.** Each frame, `backgrounds::render()`
   walks the RLE stream out of PROGMEM and bit-spreads each nibble
   directly into `fb::buffer`. No persistent RAM cache — the background
   costs zero bytes of RAM between frames.

CPU cost: ~4 KB of PROGMEM reads per frame, roughly 6 ms of the
16.7 ms frame budget. CPU-for-RAM is the right trade on this chip.

## Framebuffer discipline

- `fb::buffer` is the single 1024 B RAM canvas; page-major layout
  matches the SSD1306 GDDRAM so the display driver just `memcpy`s it
  out. No second backing buffer.
- `fb::draw_sprite_progmem` blits directly from flash — no RAM scratch
  for sprite drawing. This was a real fix: a prior version used a
  128-byte on-stack scratch inside `draw_progmem_sprite`, which at 92%
  RAM caused a stack overflow into `.bss`, corrupting `meta` and
  `best`. Learned the hard way.

## Dirty-flag rendering

Every screen except `PLAYING` is static; we only redraw when the visible
state changes. `cursor_proxy` packs the "cursor" of the current screen
(name-entry index, menu selection, bestiary row + detail-mode bit, text
section + scroll, pause sub-state, tutorial slide, etc.) into a single
u8. Between frames, if `state` and `cursor_proxy` are both unchanged,
we skip the whole render. This is what lets the UI screens stay cheap
even with dense PROGMEM decoding — most frames do nothing.

## Struct size guards

`engine/storage.h` ends with `static_assert(sizeof(BestRun) == 5, ...)`
and similar for `MetaCharacter`. Anyone adding a field without updating
the hand-picked EEPROM offsets in `platform/arduboy/storage.cpp` gets a
compile error instead of a silent save-file mis-read. Same idea in
`engine/entity.h` for `Entity` layout.

## Compile-time RAM gate

`scripts/check_ram.py` parses the ELF at build time and fails when
`.data + .bss` exceeds `RAM_CEILING_PCT` (default 85 %). Intended
*explicit* overrides use `make RAM_CEILING_PCT=95`. The gate has caught
real regressions (e.g. a 1 KB background scratch that would have run
the chip past physical SRAM).

## Known discipline violations worth their weight

- **Per-screen "static const char\* const opts[]" arrays in game.cpp.**
  These land in RAM (`.data`) unless the literals themselves are marked
  PROGMEM and read through a shim. We did the sweep for the main menu +
  pause menus; other small ones remain. Each ~50 B when present.
  Candidates for the next RAM squeeze.
- **`draw_menu_pgm` vs `draw_menu`.** We kept a PROGMEM-friendly
  renderer and deleted the raw-pointer one — LTO still dropped both
  when they existed unused, but the readability win is real.

## Compile-time checks the build runs

- `make` → builds + `make size` (flash report) + `make ram-gate`
- `make audit` → `progmem-check` + top-20 `.data`/`.bss` symbols
- `make format-check` → clang-format dry run (no reformatting)
- Pre-commit hook → runs `make format-check` before every commit

## Techniques we've measured but NOT applied yet

Ranked by expected return per effort. Each is a future commit.

| Technique | Est. save | Effort | Risk |
|---|---|---|---|
| Byte-pair / LZ4 tiny on the backgrounds | +1–2 KB flash on top of current | medium | decoder in flash |
| Sprite dedup (shared base + delta) | 0.2–0.8 KB flash | high | visual review per sprite |
| Custom 2-bit-per-column sprite format for short sprites | 0.4–0.8 KB flash | high | engine change |
| Remaining `.data` sweeps for screens not yet PROGMEM-ified | 50–200 B RAM | low | per-screen tedium |

## What NOT to do

- **Don't cache a whole 1024 B image in RAM "for speed."** RAM is the
  chip's real bottleneck. Stream or decode on demand.
- **Don't put long-lived string literals at file scope without PROGMEM.**
  They silently land in `.data`.
- **Don't allocate big stack buffers inside deeply-nested draw
  functions.** The stack grows down into `.bss`; at 90 % RAM headroom
  a 128 B temporary is a silent corruption bomb.
- **Don't use `std::string` or anything that allocates.** There is no
  allocator. `new` will bloat the binary and link a heap we never want.
- **Don't rely on `-Os` to delete "unused" globals if anything might
  take their address.** The linker keeps them; audit `make audit`.

---

If a new technique is shipped, add it here with its before/after
numbers. Performance work is only real when the commit message can cite
exact byte counts.
