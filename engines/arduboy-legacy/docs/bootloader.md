# Bootloader & flash budget

The ATmega32u4 on the Arduboy has 32,768 bytes of physical flash. The
top portion is reserved for the USB bootloader; what remains is the
program area we ship into. The exact split depends on which bootloader
is installed on the device.

## Three options

### Caterina (4 KB) — current default

- **Usable program area: 28,672 B** (32,768 − 4,096)
- The original Arduino Leonardo bootloader. Ships on every original
  Arduboy that left the factory before the FX revision.
- Zero distribution friction: anyone with any Arduboy can flash games
  using existing web tools (ProjectABE, Arduboy Manager, etc.).
- This is what `make` defaults to via `BOOTLOADER_BYTES=4096`.

### Cathy3K (3 KB) — modern community standard

- **Usable program area: 29,696 B** (+1,024 B over Caterina)
- Assembly-optimized rewrite of Caterina that fits in 3 KB while keeping
  the same USB programming interface.
- **Ships by default on the Arduboy FX** (the current production model).
- Players on the original Arduboy need a one-time bootloader upgrade,
  which requires either a USB programmer (USBasp / AVRISP) or another
  Arduino used as ISP. Not zero friction, but well-documented in the
  community.
- Drop-in compatible with all Arduboy game .hex files.
- Source: <https://github.com/MrBlinky/cathy3k>

### No bootloader (programmer-only)

- **Usable program area: 32,768 B**
- Skips the USB programming path entirely; flash via external ISP only.
- Fatal for retail distribution. Useful only for hardware tinkering.

## How we target each

Our `make flash-gate` validates the build against the active bootloader's
usable area. Override `BOOTLOADER_BYTES` to change targets:

```sh
make                        # default: Caterina (4096 B reserved)
BOOTLOADER_BYTES=3072 make  # Cathy3K (1 KB more headroom)
BOOTLOADER_BYTES=0 make     # programmer-only (full 32 KB)
```

The script in `scripts/check_flash.py` reads the env var; the Makefile's
`flash-gate` target exports it.

## Why monobit ships Caterina by default

Per `docs/business-model.md`: the Arduboy edition is the *proof-of-rigor*
SKU. The value proposition is "a real Arduboy game that fits the
smallest sane budget." Targeting Cathy3K — even though it gains 1 KB —
weakens that story:

- The original Arduboy population is large and won't go away.
- Any player who buys a $1 monobit game and discovers their device
  needs a bootloader upgrade is a player who never plays the game.
- The 1 KB gain is real but not transformative; the discipline of
  fitting Caterina is the brand.

## When to consider switching to Cathy3K

Three triggers worth re-evaluating:

1. **The Arduboy FX becomes the dominant install base.** Right now both
   exist in roughly equal numbers in active use; if FX-only buyers
   become the majority, the calculus flips.
2. **A specific feature requires the extra 1 KB and can't be fit
   any other way.** Real audits exhaust before targeting changes.
3. **The brand evolves to accept "modern Arduboy required."** Then
   document the requirement on the storefront page; players opt in
   knowingly.

If we ever switch the default, this doc gets updated and the Makefile's
`BOOTLOADER_BYTES` default flips.

## Symptoms of overflowing the bootloader region

When a binary crosses past the usable area into bootloader space:

- **Real hardware:** USB flashing breaks. Player has to recover via
  external programmer or a known-good .hex.
- **Ardens emulator:** Often manifests as **black or white screen on
  boot**, OR the simulator boots to the **ARDUBOY-FX-LOADER selection
  screen** (when the bootloader region is dirty enough that the
  simulator concludes "no valid app to launch").

When users report "F5 shows black screen" or "Ardens shows the loader,"
check `avr-size`'s `Program:` line first against `BOOTLOADER_BYTES`-
aware ceiling. The `make flash-gate` target catches the gross-overflow
case at build time.

## The layout trap (`flash-gate` says OK but boot still breaks)

`flash-gate` sums section *sizes* against the bootloader-aware ceiling.
That catches "binary too big," but it misses a subtler failure where
the **load address** of `.data` ends up inside the bootloader region
even though the section sizes themselves are well under budget.

This is specific to the scene-paging OVERLAY layout. The OVERLAY
linker block places all four scene banks at the same VMA but at
*sequential LMAs* in flash. With four ~7 KB banks, that's ~28 KB of
LMA space allocated even though only one bank's bytes (TITLE) are
actually written into the .hex — the other three are stripped by
`avr-objcopy --remove-section` before flashing because they live on
FX flash as cold copies, not internal flash.

The trap: `.data`'s LMA is computed by the linker as "after the last
scene bank's LMA," not "after the bytes that actually ship in the
hex." So `.data`'s LMA can be ~24 KB when the actual flashed bytes
are only ~16 KB. If that LMA crosses 28,672 (`0x7000` for Caterina),
boot breaks even though `flash-gate` reports plenty of headroom.

**Fix (already applied in `platform/arduboy/scene.ld`):** the OVERLAY
block uses `AT(0x10000)` to park the cold-copy LMAs out of the chip's
32 KB physical-flash range entirely. Cold-copy bytes get extracted
by `avr-objcopy --dump-section` (which works by section name, not
address) and then stripped before flashing, so high LMAs are
harmless. `.data` LMA then follows TITLE bank's LMA only — well
inside real flash.

**Symptoms in this trap specifically:**
- `flash-gate` reports OK at e.g. 17 KB / 28 KB.
- `avr-objdump -h <elf>` shows `.data` with LMA in bootloader range
  (≥ 0x7000 on Caterina, ≥ 0x7400 on Cathy3K).
- Ardens boots to the FX-LOADER selection screen instead of the game.
- Real hardware: USB-flashing fails or the device hangs at reset.

**How to verify the layout is sane after any linker-script touch:**

```sh
avr-objdump -h build/rpg-arduboy-release/rpg.elf | grep -E "data|text|scene"
```

`.data`'s third column (LMA) MUST be below `BOOTLOADER_BYTES`-aware
ceiling. If it's not, the OVERLAY's `AT(...)` clause is missing or
mis-aimed.

This trap surfaced 2026-04-29 when adding vestigia push-pulled
~2 KB of new CORE code; the bank-base shift cascaded `.data` LMA
past the Caterina boundary even though absolute flash usage was
fine. See git log around that date for the full investigation.
