"""CLI entry: python -m tools.perf_logger <trace.bin> [flags]"""
from __future__ import annotations

import argparse
import json
import sys
from pathlib import Path

from .analyze import (cycle_summary, filter_by_state, parse_state_enum,
                      read_trace, summary, timeline, top)

REPO_ROOT = Path(__file__).resolve().parents[2]
GAME_CPP = REPO_ROOT / "games" / "rpg" / "game.cpp"
COSTS_JSON = Path(__file__).resolve().parent / "avr_costs.json"


def main(argv: list[str] | None = None) -> int:
    ap = argparse.ArgumentParser(
        prog="python -m tools.perf_logger",
        description="Analyze a binary perf trace produced by mono-sdl."
    )
    ap.add_argument("trace", type=Path, help="trace.bin file")
    ap.add_argument("--summary", action="store_true",
                    help="per-state wall-clock summary table")
    ap.add_argument("--cycles", action="store_true",
                    help="per-state simulated-AVR-cycles summary")
    ap.add_argument("--timeline", action="store_true",
                    help="ASCII timeline of us per frame group")
    ap.add_argument("--top", type=int, metavar="N",
                    help="show the N slowest frames with context")
    ap.add_argument("--filter", metavar="STATE",
                    help="only include frames in this state (name, case-insensitive)")
    ap.add_argument("--budget", type=int, default=16667,
                    help="per-frame us budget (default 16667 = 60Hz)")
    ap.add_argument("--width", type=int, default=80,
                    help="timeline character width (default 80)")
    args = ap.parse_args(argv)

    if not args.trace.exists():
        print(f"error: {args.trace} not found", file=sys.stderr)
        return 2

    samples = read_trace(args.trace)
    if not samples:
        print(f"warning: {args.trace} is empty", file=sys.stderr)
        return 0

    names = parse_state_enum(GAME_CPP) if GAME_CPP.exists() else {}

    if args.filter:
        samples = filter_by_state(samples, names, args.filter)
        if not samples:
            print(f"warning: no frames in state '{args.filter}'", file=sys.stderr)
            return 0

    # Load simulated-cycle budget from the cost table if present.
    budget_cycles = 266667
    if COSTS_JSON.exists():
        with COSTS_JSON.open() as f:
            budget_cycles = json.load(f).get("budget", {}).get("frame_cycles", budget_cycles)

    printed_any = False
    if args.summary:
        print(summary(samples, names, budget_us=args.budget))
        printed_any = True
    if args.cycles:
        if printed_any:
            print()
        print(cycle_summary(samples, names, budget_cycles=budget_cycles))
        printed_any = True
    if args.timeline:
        if printed_any:
            print()
        print(timeline(samples, names, width=args.width, budget_us=args.budget))
        printed_any = True
    if args.top is not None:
        if printed_any:
            print()
        print(top(samples, names, n=args.top))
        printed_any = True

    if not printed_any:
        # Default: show --summary and --cycles when no explicit flag.
        print(summary(samples, names, budget_us=args.budget))
        print()
        print(cycle_summary(samples, names, budget_cycles=budget_cycles))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
