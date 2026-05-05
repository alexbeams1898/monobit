"""Binary trace reader + analysis.

Binary format (7 bytes per frame, little-endian u16/u32):
    byte 0: state_id  (u8)
    byte 1: us_lo     (u8)
    byte 2: us_hi     (u8)   -> frame wall-clock us (u16 LE)
    byte 3-6: sim_cycles     (u32 LE)

State-name mapping is parsed directly from games/rpg/game.cpp's State
enum so adding a state in code automatically flows into analyzer output.
"""
from __future__ import annotations

import re
import struct
from dataclasses import dataclass
from pathlib import Path
from typing import Iterable, Sequence

FRAME_SIZE = 7


@dataclass(frozen=True)
class FrameSample:
    idx: int          # position in the stream
    state_id: int
    us: int
    sim_cycles: int


def read_trace(path: Path) -> list[FrameSample]:
    """Read a binary trace file into a list of FrameSamples."""
    data = path.read_bytes()
    if len(data) % FRAME_SIZE != 0:
        # Truncate the partial trailing record. Capture may have been
        # cut mid-frame (Ctrl-C, window close). Not an error.
        data = data[:len(data) - (len(data) % FRAME_SIZE)]
    out: list[FrameSample] = []
    for i in range(0, len(data), FRAME_SIZE):
        state, us, cycles = struct.unpack_from("<BHI", data, i)
        out.append(FrameSample(idx=i // FRAME_SIZE, state_id=state,
                               us=us, sim_cycles=cycles))
    return out


def parse_state_enum(game_cpp_path: Path) -> dict[int, str]:
    """Extract the State enum from games/rpg/game.cpp and return {id: name}.

    Matches the common pattern `enum State : u8 { A, B = 2, ... }` and
    walks comma-separated entries, honoring explicit `= N` assignments.
    Robust enough for the current project; not a full C++ parser.
    """
    text = game_cpp_path.read_text(encoding="utf-8", errors="replace")
    # Strip C++ comments so we don't pick up lookalikes in docs.
    text = re.sub(r"/\*.*?\*/", "", text, flags=re.DOTALL)
    text = re.sub(r"//[^\n]*", "", text)
    m = re.search(r"enum\s+State\s*:\s*\w+\s*\{([^}]+)\}", text)
    if not m:
        return {}
    body = m.group(1)
    out: dict[int, str] = {}
    next_id = 0
    for tok in body.split(","):
        tok = tok.strip()
        if not tok:
            continue
        # Support "NAME = 3" and plain "NAME"
        if "=" in tok:
            name_part, val_part = tok.split("=", 1)
            name = name_part.strip()
            try:
                next_id = int(val_part.strip(), 0)
            except ValueError:
                # Non-numeric RHS — give up on this entry.
                continue
        else:
            name = tok
        if name and name.replace("_", "").isalnum():
            out[next_id] = name
        next_id += 1
    return out


def state_name(names: dict[int, str], sid: int) -> str:
    return names.get(sid, f"STATE_{sid}")


# -------------------------------------------------------------- analyses --


def _percentile(sorted_vals: Sequence[int], pct: float) -> int:
    if not sorted_vals:
        return 0
    k = max(0, min(len(sorted_vals) - 1, int(round(pct / 100 * (len(sorted_vals) - 1)))))
    return sorted_vals[k]


def summary(samples: Sequence[FrameSample],
            names: dict[int, str],
            budget_us: int = 16667) -> str:
    """Per-state table of min/mean/p50/p95/p99/max us, over-budget count."""
    from collections import defaultdict
    buckets: dict[int, list[FrameSample]] = defaultdict(list)
    for s in samples:
        buckets[s.state_id].append(s)
    lines = ["state            n       min   mean    p50    p95    p99    max   over"]
    lines.append("-" * 72)
    for sid in sorted(buckets.keys()):
        b = buckets[sid]
        us_sorted = sorted(s.us for s in b)
        n = len(b)
        mn = us_sorted[0]
        mx = us_sorted[-1]
        mean = sum(us_sorted) // n
        p50 = _percentile(us_sorted, 50)
        p95 = _percentile(us_sorted, 95)
        p99 = _percentile(us_sorted, 99)
        over = sum(1 for u in us_sorted if u > budget_us)
        lines.append(f"{state_name(names, sid):<16} {n:>5} {mn:>7} {mean:>6} "
                     f"{p50:>6} {p95:>6} {p99:>6} {mx:>6} {over:>6}")
    return "\n".join(lines)


def timeline(samples: Sequence[FrameSample],
             names: dict[int, str],
             width: int = 80,
             budget_us: int = 16667) -> str:
    """ASCII timeline: one char per frame-group, height = bucket of us.

    Collapses adjacent frames to fit `width` columns. Each column's
    height is the max us in that group. Height 0-8 maps to characters:
        ' ._-+=*#@' where ' ' is zero us and '@' is >= budget.
    Prints the state boundaries as a separator row below.
    """
    if not samples:
        return "(no samples)"
    group = max(1, len(samples) // width)
    cols = []
    for c in range(0, len(samples), group):
        chunk = samples[c:c + group]
        peak = max(s.us for s in chunk)
        # dominant state in the chunk
        from collections import Counter
        state = Counter(s.state_id for s in chunk).most_common(1)[0][0]
        cols.append((peak, state))

    # Build the height chart.
    glyphs = " ._-+=*#@"
    out_rows: list[str] = []
    header = f"timeline: {len(samples)} frames @ {group} frames/col, budget={budget_us}us"
    out_rows.append(header)
    bar = []
    for peak, _ in cols:
        idx = min(len(glyphs) - 1, int(peak * (len(glyphs) - 1) / max(1, budget_us)))
        bar.append(glyphs[idx])
    out_rows.append("".join(bar))

    # State-change markers on a second row.
    mark = []
    prev = None
    for _, st in cols:
        mark.append("|" if st != prev else " ")
        prev = st
    out_rows.append("".join(mark))

    # Legend of which state starts where.
    legend_parts = []
    prev = None
    for i, (_, st) in enumerate(cols):
        if st != prev:
            legend_parts.append(f"[{i}]{state_name(names, st)}")
            prev = st
    out_rows.append("states: " + " ".join(legend_parts))
    return "\n".join(out_rows)


def top(samples: Sequence[FrameSample],
        names: dict[int, str],
        n: int = 10) -> str:
    """Slowest N frames with ±2 neighbours for context."""
    if not samples:
        return "(no samples)"
    ranked = sorted(samples, key=lambda s: s.us, reverse=True)[:n]
    ranked.sort(key=lambda s: s.idx)
    lines = [f"top {n} slowest frames (ctx = ±2 neighbours)"]
    by_idx = {s.idx: s for s in samples}
    for s in ranked:
        lines.append(f"  frame {s.idx:>6}  state={state_name(names, s.state_id):<16} "
                     f"us={s.us:>6}  cycles={s.sim_cycles}")
        for d in (-2, -1, 1, 2):
            n_s = by_idx.get(s.idx + d)
            if n_s:
                lines.append(f"     {('  ' + ('+' if d > 0 else '-') + str(abs(d)) + ' '):>8} "
                             f"state={state_name(names, n_s.state_id):<16} us={n_s.us:>6}")
        lines.append("")
    return "\n".join(lines)


def filter_by_state(samples: Sequence[FrameSample],
                    names: dict[int, str],
                    name: str) -> list[FrameSample]:
    """Keep only samples whose state name matches (case-insensitive)."""
    target = name.upper()
    sids = {sid for sid, n in names.items() if n.upper() == target}
    if not sids:
        # Fallback: try substring match (e.g. --filter MENU matches MAIN_MENU).
        sids = {sid for sid, n in names.items() if target in n.upper()}
    return [s for s in samples if s.state_id in sids]


# -------------------------------------------------- AVR cycle analysis --


def cycle_summary(samples: Sequence[FrameSample],
                  names: dict[int, str],
                  budget_cycles: int = 266667) -> str:
    """Per-state table of simulated AVR cycles + % of frame budget."""
    from collections import defaultdict
    buckets: dict[int, list[FrameSample]] = defaultdict(list)
    for s in samples:
        buckets[s.state_id].append(s)
    lines = [f"simulated AVR cycles (budget {budget_cycles}/frame)"]
    lines.append("state            n       max_cyc   max%    mean_cyc   mean%    over")
    lines.append("-" * 72)
    for sid in sorted(buckets.keys()):
        b = buckets[sid]
        cyc = [s.sim_cycles for s in b]
        n = len(b)
        mx = max(cyc)
        mn = sum(cyc) // n
        mx_pct = (mx * 100) // budget_cycles
        mn_pct = (mn * 100) // budget_cycles
        over = sum(1 for c in cyc if c > budget_cycles)
        lines.append(f"{state_name(names, sid):<16} {n:>5} {mx:>10} {mx_pct:>5}%  "
                     f"{mn:>10} {mn_pct:>5}%  {over:>6}")
    return "\n".join(lines)
