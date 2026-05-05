# mono

A 1-bit game engine and the action RPG built on top of it. Targets the
Arduboy (ATmega32u4, 2.5 KB RAM, 32 KB flash, 128×64 SSD1306 OLED) and
other 1-bit-class hardware. Built from scratch — **no Arduboy2, no Arduino
core, no third-party HAL.** Raw `avr-gcc`, the SSD1306 datasheet, and the
ATmega32u4 datasheet.

## What's in here

```
engine/                hardware-agnostic core (framebuffer, entity pool, input/clock APIs)
platform/arduboy/      Arduboy HAL (SSD1306+SPI, button GPIO, Timer1 frame clock, main)
games/rpg/             the bullet-hell ARPG that ships first
docs/design/           design tree (setting, story, classes, etc.)
docs/dante-cliffs.md   source-material reference (the Commedia)
scripts/watch.sh       auto-rebuild + relaunch on file save
Makefile               build driver
```

## Design principles

- **True 1-bit, not 1-bit aesthetic.** The framebuffer is literally 1 bit
  per pixel because the hardware can't display anything else. No greys,
  no shading, no transparency.
- **Engine ↔ platform separation.** Engine code has zero `avr/*` includes.
  Porting to another 1-bit device means writing a new `platform/<name>/`,
  not touching the engine.
- **Entity symmetry.** Player, enemies, bosses, and bullets are all the
  same `Entity` struct in the same pool, processed by the same update and
  draw loops. Behavior differences are data, not code branches.

## Building

Requires `avr-gcc`, `avr-libc`, `avr-binutils`, `make`. On Windows + MSYS2:

```bash
pacman -S mingw-w64-x86_64-avr-gcc mingw-w64-x86_64-avr-libc \
          mingw-w64-x86_64-avr-binutils mingw-w64-x86_64-avrdude make
```

Then:

```bash
make                # build the .hex
make ardens         # build + launch Ardens emulator
make ardens-watch   # rebuild + relaunch Ardens on every source-file change
make clean          # wipe build/
```

The build reports flash and RAM usage after each compile — those numbers
are the budget you're working against (32 KB / 2.5 KB).

## Testing

[Ardens](https://github.com/tiberiusbrown/Ardens) is the cycle-accurate
Arduboy emulator used for development. `make ardens` expects it at
`C:\Users\alexb\Tools\Ardens\Ardens.exe` — adjust the `ARDENS` variable
in the Makefile to match your install path.

