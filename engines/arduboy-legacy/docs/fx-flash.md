# Arduboy FX flash storage

Reference for how the engine reads from the W25Q128 SPI flash chip on
the Arduboy FX cart. Written after a multi-hour debugging session
that uncovered ~five overlapping issues — capturing what we learned
so future-us doesn't re-derive any of it.

## Why we have FX at all

Internal flash on the ATmega32u4 is **28,672 B usable** (32 KB minus
the 4 KB Caterina bootloader). We've been bumping against this ceiling
since the title screen + dialog tables landed. FX is the relief valve:
content that doesn't need to be in the audio ISR's hot path can move
to the FX cart's 16 MB chip and stay readable at ~1 µs/byte.

What's safe to move:
- **Songs** (per-frame reads, 60 Hz)
- **LZ77-compressed images** (decoded once per state change)
- **Bestiary sprites** that aren't drawn every frame
- **Dialog trees** (shown a few times per session)
- **Level layouts**

What stays in PROGMEM:
- **Audio ISR data** — the 16 kHz mixer reads notes and waveform tables;
  SPI is ~10× slower per byte than LPM
- **Per-frame draw data** — anything the draw loop touches every frame
- **Anything < ~100 B** — the SPI setup overhead per call (4 bytes + CS
  toggles ≈ 6 µs) dwarfs the savings on tiny blobs

## What the Arduboy FX chip actually is

W25Q128: 16 MB serial NOR flash, SPI mode 0, max 50 MHz on Standard
READ. Wired on the cart per the official Arduboy schematic:

```
FX CS   → PD1 (active low, idle high)   ← NOT PD2
SCK     → PB1 (shared with OLED)
MOSI    → PB2 (shared with OLED)
MISO    → PB3 (FX-only — OLED is write-only)
```

The earlier homemade-Arduboy schematics used PD2 for CS. The original
data_flash stub comment said PD2 — it was wrong, and that mattered.
Sources: Arduboy community schematic discussions, MrBlinky FX mod chip
reference.

## SPI bus sharing with the OLED

The SPI peripheral is configured ONCE at boot by `display::init()`:
master mode, mode 0 (CPOL=0, CPHA=0), fosc/2 = 8 MHz. **The FX driver
does not touch SPCR/SPSR.** Both peripherals use the same settings;
the only thing that distinguishes "talking to OLED" from "talking to
FX" is which CS line is asserted.

**Hard rule: OLED CS (PD6) and FX CS (PD1) must never be low at the
same time.** Both are active-low. The display driver bookends each
command/flush with CS low/high; the FX driver does the same. As long
as FX reads happen from `update()` / per-frame logic (NOT from inside
a `draw()` that overlaps a flush, NOT from an ISR), there's no
collision.

Audio ISR doesn't touch SPI at all — phase-accumulator mixer writes a
single GPIO bit on PORTC, no shared peripheral.

## The W25Q128 boots into Deep Power-Down

This is the gotcha that ate the most time. Out of reset, the W25Q128
enters DPD mode and **ignores every command except 0xAB (Release From
Deep Power-Down)** until released. Reads in DPD return 0xFF (MISO
floats high).

The driver sends `0xAB` once on first use, then waits 5 µs (W25Q tRES1
= 3 µs minimum). Without this step every read returns 0xFF and the
chip looks like it isn't there.

Symptom we saw: smoke-test bytes at offset 0 were just plausible-looking
garbage from the boot state — once we started reading "real" data we
got 0xFF back. The fix is at [platform/arduboy/data_flash.cpp:70](../platform/arduboy/data_flash.cpp#L70).

## Image size: 16 MB minus 4 KB save sector

Our build pipeline pads `data.bin` to the full chip-data region. The
top 4 KB of the chip is reserved by convention as the per-game save
area. So:

```
DATA_SIZE = 16 MB - 4 KB = 16,773,120 bytes (0xFFF000)
```

Ardens validates the image fits in this space and rejects a full 16 MB
image with `FX data too large`. Anything bigger means our manifest's
real assets exceed the chip's data section and need trimming.

The pad bytes are 0xFF (the chip's natural erased state). They have no
observable effect — reads past the actual asset end just look like
unprogrammed flash.

## Where Ardens places fxdata: NOT at chip offset 0

The most surprising lesson. When you pass `file=foo.bin` to Ardens with
a non-flashcart file (no "ARDUBOY"+"Bootloader" magic), Ardens places
it at the END of the chip address space, just below the save sector.

The formula from `absim_arduboy.cpp::reload_fx`:

```
fxsave_bytes = round_up(fxsave.size, 4096)
fxdata_bytes = round_up(fxdata.size, 256)
fxsave_offset = 16 MB - fxsave_bytes
fxdata_offset = fxsave_offset - fxdata_bytes
```

For our case (empty fxsave, 16 MB - 4 KB fxdata):

```
fxsave_bytes = 0
fxdata_bytes = 16,773,120
fxsave_offset = 16,777,216
fxdata_offset = 16,777,216 - 16,773,120 = 4,096 = 0x1000
```

**Our data lives at chip offset 0x1000.** Reading offset 0 returns the
default chip image Ardens initializes (an "ARDUBOY\0..." header that
looks like file-format magic but is actually the empty-cart placeholder
the simulator paints into the chip even when no file is loaded).

This matches real-hardware FX cart layout: per-game data is allocated
near the top of the chip by the FX flash tool, not at offset 0 (where
the loader image lives).

## The programDataPage mechanism

For a real flashed cart, the offset isn't fixed. The FX flash tool
(MrBlinky's `fxdata-build.py` and friends) places each game's data
wherever there's room and patches the game's hex to know where.

The patch protocol uses interrupt vector 5's slot (which Arduboy
doesn't otherwise use — most games never enable EE_READY):

```
PROGMEM offset 0x0014: u16 magic = 0x9518   (RETI instruction — harmless if executed)
PROGMEM offset 0x0016: u16 programDataPage  (page where game data was placed, big-endian)
```

`0x9518` is the AVR `RETI` opcode, chosen so that even in the
unlikely case the CPU jumps to vector 5, execution returns
immediately. The bytes appear as `0x18 0x95` in flash (little-endian
word storage).

The flash tool:
1. Scans the hex for the 0x9518 marker
2. Writes the page number (in big-endian) to PROGMEM 0x16/0x17

The driver checks the magic at boot. If found, it uses the patched
page. If not (un-patched hex, e.g. running in Ardens without the
flash tool), it falls back to `FX_PAGE_FALLBACK = 0x10` — chosen
because `0x10 * 256 = 0x1000` matches Ardens' fxdata placement
formula for our specific image size.

See `resolve_program_data_page()` in
[platform/arduboy/data_flash.cpp](../platform/arduboy/data_flash.cpp).

**Real-hardware enablement still needed:** integration with an FX
flash tool (`ardugotools`, MrBlinky's utilities, or a custom patcher)
that runs as part of `make flash-hardware` to upload data.bin to the
cart and patch the magic in our hex. Until that exists, real-hardware
testing requires manually patching bytes 0x16/0x17 of the hex to
match where the cart's flash layout placed our data.

## What we tried that didn't matter

So future-us doesn't burn time re-investigating these:

- **SPI timing between consecutive reads** — checked tCSH, tSHSL,
  tCSS. All met by single-instruction cycles at 16 MHz; no software
  delay needed between read() calls.
- **JEDEC ID probe (0x9F) before reads** — not required. The wake
  command is sufficient.
- **FAST_READ (0x0B) vs Standard READ (0x03)** — Standard READ tops at
  50 MHz on the W25Q128. Our 8 MHz SCK is well within spec.
- **Read window size in lz77 streaming decoder** — tried 1, 32, larger.
  None mattered for correctness; 32 is fine for performance.
- **Per-byte vs per-multi-byte read calls** — both work identically
  once the chip base offset is right.
- **CMD_RELEASE_POWERDOWN with dummy bytes** — older variants of the
  command spec mention 3 dummy bytes for legacy device-ID return.
  Modern parts don't need them.

## Build pipeline

`tools/fxdata/manifest.txt` lists assets, one per line:

```
TITLE_LZ77 data/fx/title_lz77.bin
```

`tools/fxdata/build.py`:
1. Concatenates assets in order into `build/fxdata/data.bin`
2. Pads to 16 MB - 4 KB with 0xFF
3. Emits `build/fxdata/data_offsets.h` with `OFFSET_<NAME>` constants
   (file-relative, so OFFSET_TITLE_LZ77 = 0)

Both top-level `make` and the SDL CMake target depend on this script
(via the `fxdata` Make target / `add_custom_command` in CMakeLists)
so changes to the manifest trigger a rebuild of any TU that includes
`data_offsets.h`.

The SDL backend reads `data.bin` from the executable's directory via
`SDL_GetBasePath()`. The Makefile copies `data.bin` next to the SDL
exe as part of `make sdl`. Missing-file or past-EOF reads return 0xFF
(matching real chip erased state).

## Streaming source pattern (for compressed data)

Plain `data_flash::read()` reads N bytes into a caller-provided buffer.
For data the caller would otherwise consume sequentially (LZ77 source
bytes), the `lz77::decode_from_fx()` entry point uses a small
internal read-ahead window so each individual byte access doesn't
incur SPI setup overhead.

```
struct FxStream {
  u32 cursor;
  u8 win[FX_WINDOW];  // FX_WINDOW = 32 currently
  u8 win_pos, win_len;
  u8 next();  // refills win[] from FX when exhausted
};
```

32-byte window: chosen empirically. Larger reduces SPI overhead but
inflates stack frame. 32 keeps stack pressure low while amortizing
setup over ~30 byte fetches per refill.

This is the template for any future "compressed asset on FX" decoder.
The pattern doesn't generalize to back-references (LZ77 back-refs
read from already-decoded OUTPUT, not the source) — only to cases
where source consumption is monotonically forward.

## Empty-byte semantics

Reads from un-mapped or erased chip regions return **0xFF**, not 0x00.
This matches:
- Real W25Q128 erased flash
- Stock Arduboy without FX cart (MISO floats high)
- Our SDL backend (configured to return 0xFF on missing-file/past-EOF)

Callers that need a "this asset doesn't exist" sentinel should check
for 0xFF, never 0x00. 0x00 is a valid asset byte; 0xFF is the platform
guarantee for "nothing here."

## Footprint after migration

Pre-migration baseline: 27,026 B flash / 2007 B RAM
Post-migration: 26,476 B flash / 2008 B RAM

Net **-550 B flash** for moving TITLE_LZ77 (929 B PROGMEM blob) to FX.
The driver + decoder cost ~380 B; the PROGMEM blob removal saved 929
B. Ratio improves with bigger blobs — first migration paid the
infrastructure cost; future ones are pure win.
