# Profiling

The engine has built-in performance instrumentation — no external tooling
required. For function-level analysis when the built-in numbers point at
something, fall back to the Ardens GUI profiler.

## Built-in: in-game perf overlay (default)

Every game on this engine gets a frame timer and three named scopes for
free, wired up in `platform/arduboy/main.cpp`:

| Scope | Measures |
|-------|----------|
| UPDATE | game::update() — input, AI, physics, collisions |
| DRAW   | game::draw() — clearing fb + sprite blits + HUD + menus |
| FLUSH  | display::flush() — sending the 1024-byte framebuffer over SPI |

### Showing the overlay

In any game state, **hold A and B together for ~0.5 seconds** to toggle a
small overlay in the top-left of the screen:

```
FT  8423      <- last frame work time, microseconds
AV  7912      <- rolling average over the last 16 frames
```

Hold A+B again to hide it.

### What the numbers mean

- **Frame budget at 60Hz = 16667 us.** Anything over that drops a frame.
- The overlay shows `FT` (single-frame instant) and `AV` (16-frame
  smoothed). `AV` is the better number for "is my game fast enough"; `FT`
  is useful for spotting one-off spikes.
- Numbers are work-time only. The CPU spends the rest of the frame
  asleep waiting for the next 60Hz tick.

### Adding your own scopes

`engine/perf.h` exposes `scope_begin(id)` / `scope_end(id)` /
`scope_us(id)`. SCOPE_COUNT is currently 4; the engine uses IDs 0-2
(UPDATE / DRAW / FLUSH), so games can use 3+ if SCOPE_COUNT is bumped.

Example:

```cpp
perf::scope_begin(3);
expensive_collision_check();
perf::scope_end(3);
// ...later, in draw():
font::draw_uint(64, 0, perf::scope_us(3));
```

The current overlay only shows `FT` and `AV` — extend `perf::draw_overlay()`
to surface scope numbers when you need them.

## Fallback: Ardens GUI profiler

When the overlay says "X is too slow" but you need to know which *function*
is hot, use Ardens' built-in profiler (function-level, cycle-accurate).

```bash
make ardens
```

Then in Ardens:

1. **Windows menu → Profiler** (note: "Windows" not "Tools")
2. Click **Start Profiling** in the panel
3. Play during a busy moment (~30 sec)
4. Click **Stop Profiling**
5. Sort by `%` descending — top entries are your hottest functions

The release build's LTO inlines aggressively, so most hot work folds
into a single huge `main` symbol. Cross-reference Ardens' raw addresses
against `avr-objdump -d build/rpg-arduboy-release/rpg.elf` to find the
specific function. (We tried a debug-symbol build for nicer names but
its 32 KB ELF spills into bootloader space and breaks Ardens — see
Makefile comment on `debug:`.)

If Ardens misbehaves between launches:

```bash
make ardens-fresh   # wipes Ardens' cached save state, then relaunches
```

## Why no external profiler

Real CPU profilers (Tracy, Perfetto, etc.) assume megabytes of RAM and a
network — neither exists on AVR. Ardens is the only emulator-side tool, and
its profiler is GUI-only. The in-game overlay is faster to consult than any
of them and works on real hardware too, so it's the primary workflow.
