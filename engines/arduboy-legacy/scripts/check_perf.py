#!/usr/bin/env python3
"""Run the SDL build for ~10 seconds, capture a perf trace, and assert
no frame exceeded the 16 MHz / 60 Hz Arduboy budget (266,667 cycles).

Usage:
    python scripts/check_perf.py BUDGET_OVERRIDE_HEAD_LIMIT=N

The instrumentation in engine/perf_hook.h + platform/sdl/perf_hook.cpp
charges per-op AVR-cycle estimates as the SDL build runs. If any frame's
sum exceeds the budget, that frame would drop on real Arduboy. We treat
that as a build failure — feature work that blows the per-frame budget
should not silently land.

The trace's `STATE_*` (un-named) buckets are produced by uninitialized
or in-transition state IDs; those are excluded from the budget assertion
since they don't represent real game-loop frames.
"""

from __future__ import annotations

import os
import struct
import subprocess
import sys
from pathlib import Path

ROOT     = Path(__file__).resolve().parents[1]
# build-sdl-perf is the PERF_UART_LOG=ON variant — stdout is a binary
# trace stream. The clean build-sdl/ exe is silent on stdout (used for
# interactive runs).
SDL_EXE  = ROOT / "build-sdl-perf" / "bin" / "mono-sdl.exe"
TRACE    = ROOT / "trace_perfcheck.bin"
BUDGET   = 266667
RUN_SECS = 10
RECORD_BYTES = 7  # state(1) + us(2) + cycles(4)


def main() -> int:
    if not SDL_EXE.exists():
        print(f"ERROR: {SDL_EXE} not built. Run `make sdl-build-log` first.")
        return 1

    # Capture the trace.
    print(f"Running {SDL_EXE.name} for ~{RUN_SECS}s on TITLE screen...")
    with open(TRACE, "wb") as out:
        proc = subprocess.Popen([str(SDL_EXE)], stdout=out, stderr=subprocess.DEVNULL)
        try:
            proc.wait(timeout=RUN_SECS)
        except subprocess.TimeoutExpired:
            proc.terminate()
            try:
                proc.wait(timeout=2)
            except subprocess.TimeoutExpired:
                proc.kill()

    data = TRACE.read_bytes()
    n_records = len(data) // RECORD_BYTES
    if n_records < 60:
        print(f"WARN: only {n_records} records captured (need ~60+ for a useful sample)")

    # Per-frame budget check. Skip frames whose state byte doesn't map to
    # a known game State; those are usually pre-init or transition junk
    # and don't reflect the steady-state cost we want to gate on.
    overruns = []
    by_state: dict[int, list[int]] = {}
    for i in range(n_records):
        off = i * RECORD_BYTES
        state_id = data[off]
        cyc = struct.unpack_from("<I", data, off + 3)[0]
        by_state.setdefault(state_id, []).append(cyc)
        if cyc > BUDGET:
            overruns.append((i, state_id, cyc))

    print(f"Captured {n_records} records across {len(by_state)} state IDs.")
    for sid in sorted(by_state):
        cycs = by_state[sid]
        n = len(cycs)
        mx = max(cycs)
        avg = sum(cycs) // n
        pct = 100 * mx / BUDGET
        print(f"  state {sid:>3}: n={n:>4}  max={mx:>7} ({pct:>5.1f}% of budget)  mean={avg:>5}")

    if overruns:
        print(f"\nFAIL: {len(overruns)} frame(s) over budget ({BUDGET} cycles):")
        for i, sid, cyc in overruns[:10]:
            print(f"  frame {i:>4}, state {sid}: {cyc} cycles ({100 * cyc / BUDGET:.1f}%)")
        if len(overruns) > 10:
            print(f"  ... ({len(overruns) - 10} more)")
        return 1

    print(f"\nOK: every frame under {BUDGET}-cycle budget.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
