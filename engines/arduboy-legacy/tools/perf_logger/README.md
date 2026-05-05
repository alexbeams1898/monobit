# perf_logger

Per-frame performance instrumentation for the mono engine. Lives entirely
on the PC (SDL) side — the Arduboy build carries no profiling code and
the 28 KB flash ceiling is never pressured by dev tooling.

## How it works

The SDL build's `platform/sdl/perf.cpp` writes a 7-byte record per frame
to stdout when compiled with `PERF_UART_LOG` defined. Each record
contains:

| Bytes | Field          | Meaning                                   |
|-------|----------------|-------------------------------------------|
| 0     | `state_id`     | game's current `State` enum value         |
| 1-2   | `us` (u16 LE)  | wall-clock microseconds for this frame    |
| 3-6   | `cycles` (u32) | simulated AVR cycles this frame would cost|

The simulated-cycles number comes from hand-instrumented hot-path
calls to `perf::sim_charge(op, units)` in engine wrappers. Cost per op
is in [`avr_costs.json`](avr_costs.json) — source of truth, calibrated
occasionally against Ardens's Profiler.

## Capture a trace

From an MSYS2 MinGW64 shell in the repo root:

```bash
make sdl-build-log                 # configures PERF_UART_LOG=ON, rebuilds
./build-sdl/bin/mono-sdl.exe > trace.bin
# play the game, close the window
```

## Analyze

```bash
python -m tools.perf_logger trace.bin                    # summary + cycles
python -m tools.perf_logger trace.bin --summary           # just wall-clock
python -m tools.perf_logger trace.bin --cycles            # just sim cycles
python -m tools.perf_logger trace.bin --timeline          # ASCII timeline
python -m tools.perf_logger trace.bin --filter MAIN_MENU  # narrow to one state
python -m tools.perf_logger trace.bin --top 10            # 10 slowest frames
```

State names are parsed directly from `games/rpg/game.cpp`'s `State` enum
so adding a new state in code automatically flows into analyzer output.

## What the simulated-cycle number means

The PC running this code is ~100× faster than a 16 MHz ATmega32u4, so
wall-clock `us` readings on PC don't predict Arduboy frame cost. The
simulated-cycle counter converts each engine hot-path call (LZ77 decode,
display flush, dither, font render, sprite blit, PROGMEM bulk read) into
a pre-measured AVR-cycle estimate and sums per frame.

Budget: 16 MHz / 60 Hz = 266,667 cycles per frame. Any frame where the
sim-cycle total exceeds the budget would drop a frame on real Arduboy.

Cost estimates are initial hand-measurements. To recalibrate against
real AVR data, capture an Ardens Profiler session and update the
`cycles_per_unit` fields in `avr_costs.json`.

## What this tool does NOT catch

- Real-hardware-only bugs (USB timing, EEPROM wear, RF, battery droop).
  Flash a real Arduboy before releases.
- Interrupt-driven jitter. ISRs preempt work on AVR; the simulator does
  not model this. For ISR correctness, test in Ardens — its cycle-accurate
  emulation does model ISRs.
- Cost-table drift. If a hot-path function changes without the cost
  table being re-measured, the simulated cycles will be stale. The
  linter rule in `calibrate.py` (future) will flag this.
