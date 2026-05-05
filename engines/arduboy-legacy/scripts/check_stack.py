#!/usr/bin/env python3
"""Stack-headroom analyzer for the Arduboy build.

Gcc's `-fstack-usage` flag emits one `.su` file per compiled `.cpp`,
listing the static stack frame size of every function. We parse those,
build a call graph from the linker's disassembly, and compute the
worst-case stack depth from `main`.

Compare that depth to the real stack headroom:

    headroom = RAMEND - .data - .bss - safety_margin

If the worst path exceeds the headroom, AVR's stack will collide with
.bss at runtime — silent corruption, not a clean fault. Stock Arduboy
RAMEND = 0x800AFF = 2,815. ATmega32u4 SRAM is 2,560 B mapped at
0x800100..0x800AFF.

Limitations:
- `-fstack-usage` doesn't compose with `-flto`. The real ship build
  uses LTO + aggressive inlining, which usually REDUCES stack depth
  (callees get inlined into callers, sharing one frame). So this
  analysis is a CONSERVATIVE upper bound on the LTO-stripped build's
  stack peak.
- Function pointers / virtuals are not visible in the call graph; we
  approximate by treating any unresolved call as a worst-case leaf
  (caller frame + 0).
- ISR stack peaks are not modeled (Arduboy has the Timer1 ISR in
  clock.cpp; its stack frame is small but technically additive).

Usage:
    make stack-report

Exits non-zero if peak > headroom.
"""

from __future__ import annotations

import re
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
BUILD_NOLTO = ROOT / "build" / "rpg-arduboy-release-nolto"
ELF_NOLTO = BUILD_NOLTO / "rpg.elf"

# Arduboy ATmega32u4 specifics.
RAMEND = 0x800AFF      # absolute address of SRAM top
RAM_BASE = 0x800100    # absolute address of SRAM base
SRAM_SIZE = RAMEND - RAM_BASE + 1  # 2,560 B

# Per-call overhead on AVR: rcall pushes 2 B return address. Estimate.
CALL_FRAME_OVERHEAD = 2

# Safety margin to leave between deepest stack peak and .bss top.
# Empirically, ISR usage + register-save spills push real peaks ~20-50 B
# above static estimates. 64 B is conservative.
SAFETY_MARGIN = 64


def parse_su(path: Path) -> dict[tuple[str, int], int]:
    """Parse one .su file.

    Format per line: `path/to/file.cpp:LINE:COL:funcname\tNN\tstatic`.
    We key by (source_path, line) instead of name — that lets us match
    against `addr2line` output directly without C++ unmangling.

    Source path is normalized to forward slashes + repo-relative form so
    Windows paths match Unix-style `addr2line` output.
    """
    out: dict[tuple[str, int], int] = {}
    if not path.exists():
        return out
    for line in path.read_text(encoding="utf-8", errors="replace").splitlines():
        # Use rsplit so file paths with colons (Windows drive letters) parse.
        parts = line.split("\t")
        if len(parts) < 2:
            continue
        head = parts[0]
        try:
            frame = int(parts[1])
        except ValueError:
            continue
        # Find the file:line:col:funcname split. funcname can contain
        # arbitrary characters (parens, commas, ::), so anchor on the
        # last 3 colons before a non-digit.
        m = re.match(r"^(.*?):(\d+):\d+:.+$", head)
        if not m:
            continue
        src = m.group(1).replace("\\", "/")
        # Strip leading drive prefix if present.
        if re.match(r"^[a-zA-Z]:/", src):
            src = src[2:]
        ln = int(m.group(2))
        out[(src, ln)] = max(out.get((src, ln), 0), frame)
    return out


def build_call_graph(elf: Path, sym_addrs: dict[str, int]) -> dict[str, set[str]]:
    """Parse `avr-objdump -d` output to find caller -> callee edges.

    The naive approach (track current symbol from `<name>:` headers)
    fails on AVR's `-mcall-prologues` setup: `game::draw` rjmps to a
    shared prologue stub then resumes at `.L1^B32`, so all the real
    body — and all outgoing calls — live under `.L*` sub-labels, never
    the function symbol itself.

    Real fix: build an address->real-symbol map (sorted by address;
    each real symbol owns its range up to the next one). For every
    `call/jmp` instruction, attribute the edge to whichever real
    symbol's range the instruction address falls in. Sub-labels are
    transparent.

    Edges TO sub-labels also get redirected to the parent real symbol
    via the same range lookup.
    """
    # Build sorted real-symbol address list. Filter out:
    #   - symbols whose name starts with a label prefix (.L, .tmp, .do_)
    #   - the shared prologue/epilogue helpers (we don't follow into them)
    is_label_re = re.compile(r"^\.L|^\.tmp|^\.do_|^__prologue_saves__|^__epilogue_restores__")
    real_syms = sorted(
        ((addr, name) for name, addr in sym_addrs.items() if not is_label_re.match(name)),
        key=lambda kv: kv[0],
    )
    real_addrs = [a for a, _ in real_syms]
    real_names = [n for _, n in real_syms]

    def owner_of(addr: int) -> str | None:
        """Bisect-find the real symbol whose range contains addr."""
        import bisect
        i = bisect.bisect_right(real_addrs, addr) - 1
        if i < 0:
            return None
        return real_names[i]

    proc = subprocess.run(
        ["avr-objdump", "-d", str(elf)],
        capture_output=True, text=True, check=True,
    )
    graph: dict[str, set[str]] = {n: set() for _, n in real_syms}
    line_re = re.compile(r"^\s+([0-9a-f]+):\s")
    call_re = re.compile(
        r"\b(?:r?call|jmp)\s.*?;\s*0x([0-9a-f]+)\s+<([^>+]+)(?:\+0x[0-9a-f]+)?>"
    )
    for line in proc.stdout.splitlines():
        lm = line_re.match(line)
        if not lm:
            continue
        cm = call_re.search(line)
        if not cm:
            continue
        ins_addr = int(lm.group(1), 16)
        target_addr = int(cm.group(1), 16)
        target_name = cm.group(2)
        caller = owner_of(ins_addr)
        if not caller:
            continue
        # Resolve the target back to its owning real symbol via range
        # lookup — that way edges to .L* targets count toward the parent.
        callee = target_name if not is_label_re.match(target_name) else owner_of(target_addr)
        if not callee or callee == caller:
            continue
        graph[caller].add(callee)
    return graph


def get_section_size(elf: Path, name: str) -> int:
    proc = subprocess.run(
        ["avr-size", "-A", str(elf)],
        capture_output=True, text=True, check=True,
    )
    for line in proc.stdout.splitlines():
        parts = line.split()
        if len(parts) >= 2 and parts[0] == name:
            return int(parts[1])
    return 0


def addr2line_batch(elf: Path, addrs: list[int]) -> dict[int, tuple[str, int] | None]:
    """Map symbol addresses to (source_path, line) via avr-addr2line.

    Batched: invoke addr2line once with all addresses on stdin to avoid
    a process-per-symbol fork bomb (AVR-toolchain on Windows is slow).
    Returns address -> (relpath, line) or None for "??:?" answers.
    """
    if not addrs:
        return {}
    proc = subprocess.run(
        ["avr-addr2line", "-e", str(elf)] + [f"0x{a:x}" for a in addrs],
        capture_output=True, text=True, check=True,
    )
    result: dict[int, tuple[str, int] | None] = {}
    out_lines = proc.stdout.splitlines()
    for addr, line in zip(addrs, out_lines):
        m = re.match(r"^(.*?):(\d+)(?:\s|$)", line)
        if not m:
            result[addr] = None
            continue
        src = m.group(1).replace("\\", "/")
        # Normalize to repo-relative if absolute.
        try:
            p = Path(src)
            if p.is_absolute():
                src = p.resolve().relative_to(ROOT).as_posix()
        except Exception:
            pass
        # Strip drive prefix if any survived.
        if re.match(r"^[a-zA-Z]:/", src):
            src = src[2:]
        try:
            ln = int(m.group(2))
        except ValueError:
            result[addr] = None
            continue
        result[addr] = (src, ln)
    return result


def get_symbol_addrs(elf: Path) -> dict[str, int]:
    """Mangled-symbol -> address via avr-nm."""
    proc = subprocess.run(
        ["avr-nm", "--radix=d", str(elf)],
        capture_output=True, text=True, check=True,
    )
    out: dict[str, int] = {}
    for line in proc.stdout.splitlines():
        parts = line.split()
        if len(parts) < 3:
            continue
        try:
            addr = int(parts[0])
        except ValueError:
            continue
        kind = parts[1]
        if kind not in ("T", "t", "W", "w"):
            continue
        out[parts[2]] = addr
    return out


def deepest_path(graph: dict[str, set[str]],
                 frames: dict[str, int],
                 root: str) -> tuple[int, list[str]]:
    """DFS from `root`, return (max_total_stack, path) avoiding cycles.

    Stack at a node = node's frame size + max(child path). Cycles bound
    by visiting set; any back edge contributes 0 (treat as already
    accounted for).
    """
    memo: dict[str, tuple[int, list[str]]] = {}
    visiting: set[str] = set()

    def dfs(node: str) -> tuple[int, list[str]]:
        if node in memo:
            return memo[node]
        if node in visiting:
            return 0, [node + " (cycle)"]
        visiting.add(node)
        frame = frames.get(node, 0)
        # Approximate frame for unknown symbols (libgcc, ISRs, AVR-CRT
        # entry points): treat as 0 unless we can guess. The libgcc
        # 32-bit divide helpers are leaves with small frames.
        best_child_total = 0
        best_child_path: list[str] = []
        for child in graph.get(node, ()):
            total, path = dfs(child)
            if total > best_child_total:
                best_child_total = total
                best_child_path = path
        visiting.discard(node)
        result = (
            frame + CALL_FRAME_OVERHEAD + best_child_total,
            [f"{node} ({frame}B)"] + best_child_path,
        )
        memo[node] = result
        return result

    return dfs(root)


def main() -> int:
    # Step 1: rebuild without LTO so .su files materialize. The
    # release-nolto variant is defined in the Makefile; it keeps
    # scene-paging on (OVERLAY layout is what makes the binary fit at
    # all even with LTO) and adds -fstack-usage + -g1.
    #
    # SKIP_LAYOUT_GATE=1 — without LTO inlining, .data's LMA shifts
    # past the bootloader boundary; that's fine because this binary
    # is never flashed.
    # FLASH_CEILING_PCT=999 — the no-LTO build can run over the 28 KB
    # ceiling; don't fail the size gate for an analysis-only artifact.
    print(f"Building no-LTO variant at {BUILD_NOLTO.relative_to(ROOT)} ...")
    BUILD_NOLTO.mkdir(parents=True, exist_ok=True)
    import os as _os
    env = dict(_os.environ)
    env["SKIP_LAYOUT_GATE"] = "1"
    rc = subprocess.run(
        ["make", "BUILD=release-nolto", "FLASH_CEILING_PCT=999", "all"],
        cwd=ROOT, env=env,
    ).returncode
    if rc != 0:
        print("ERROR: no-LTO build failed", file=sys.stderr)
        return rc

    elf = BUILD_NOLTO / "rpg.elf"
    if not elf.exists():
        print(f"ERROR: no ELF at {elf}", file=sys.stderr)
        return 1

    # Step 2: collect .su frames keyed by (source, line).
    frames_by_loc: dict[tuple[str, int], int] = {}
    su_files = list(BUILD_NOLTO.rglob("*.su"))
    for su in su_files:
        for k, v in parse_su(su).items():
            frames_by_loc[k] = max(frames_by_loc.get(k, 0), v)
    print(f"Parsed {len(su_files)} .su files, {len(frames_by_loc)} (file,line) entries.")

    # Step 3: build call graph from disasm. Need sym_addrs first so the
    # graph builder can map instruction addresses back to owning symbols.
    sym_addrs = get_symbol_addrs(elf)
    graph = build_call_graph(elf, sym_addrs)
    print(f"Call graph: {len(graph)} nodes, "
          f"{sum(len(v) for v in graph.values())} edges.")

    # Step 4: for each call-graph node, look up its source location via
    # addr2line and match against the (file,line) keyed .su map. Two
    # observations: avr-addr2line on a function symbol address returns
    # the FIRST line of the function body (one or two lines after the
    # signature line in .su), so we tolerate +/-2 lines of slack when
    # matching.
    nodes = sorted(graph.keys())
    addrs = [sym_addrs[n] for n in nodes if n in sym_addrs]
    addr2node = {sym_addrs[n]: n for n in nodes if n in sym_addrs}
    locs = addr2line_batch(elf, addrs)

    frames_by_mangled: dict[str, int] = {}
    for addr, loc in locs.items():
        if not loc:
            continue
        node = addr2node[addr]
        # Try exact, then ±2 lines (function body usually starts one line
        # below the signature; .su pins to the signature line).
        for delta in (0, -1, -2, 1, 2):
            key = (loc[0], loc[1] + delta)
            if key in frames_by_loc:
                frames_by_mangled[node] = frames_by_loc[key]
                break

    matched = len(frames_by_mangled)
    print(f"Matched .su frames to {matched}/{len(graph)} graph nodes.")

    # Step 5: deepest path from every entry point. The audit reaches
    # functions transitively from `main` AND from interrupt vectors
    # (Timer1 audio ISR, USB ISRs, watchdog) AND from scene update/draw
    # entry points (which are called via volatile vtable fnptrs that
    # are invisible to the call graph builder — but their addresses
    # are taken in the VT_* tables, so we know the names).
    #
    # We compute peak per root, take the max, and add an ISR push
    # budget on top: any ISR can fire at any time and push ~32 B of
    # register state onto the stack before its first call.
    ISR_PUSH_BUDGET = 32  # avr-gcc audio::tick prologue is < 32 B
    roots = ["main"]
    for n in graph.keys():
        # Anything beginning with `__vector_` is an ISR entry point.
        if "__vector_" in n:
            roots.append(n)
        # Volatile-fnptr scene entry points — invisible to the
        # call graph because the call goes through scene::current.
        # Their reach is the same code as the graph would otherwise
        # show; we just need to start the walk from them explicitly.
        if any(seg in n for seg in (
            "update_title_scene", "draw_title_scene",
            "update_main_menu_scene", "draw_main_menu_scene",
            "update_gate_scene", "draw_gate_scene",
            "update_play_scene", "draw_play_scene",
        )):
            roots.append(n)

    peaks: list[tuple[str, int, list[str]]] = []
    for root in roots:
        if root not in graph:
            continue
        rp, rpath = deepest_path(graph, frames_by_mangled, root)
        peaks.append((root, rp, rpath))
    if not peaks:
        print("ERROR: no roots found in call graph", file=sys.stderr)
        return 1
    # Take the max-peak root as THE peak.
    peaks.sort(key=lambda t: -t[1])
    peak_root, peak, path = peaks[0]

    # Step 6: compare to headroom.
    bss_size = get_section_size(elf, ".bss")
    data_size = get_section_size(elf, ".data")
    headroom = SRAM_SIZE - bss_size - data_size

    # Combined peak: deepest non-ISR path + worst-case ISR push budget,
    # since the audio ISR (16 kHz) can fire at any moment, including
    # the instant SP is at the deepest non-ISR path's bottom.
    combined = peak + ISR_PUSH_BUDGET

    print()
    print(f"=== stack-report ===")
    print(f".data:        {data_size:>4} B")
    print(f".bss:         {bss_size:>4} B")
    print(f"SRAM total:  {SRAM_SIZE:>4} B")
    print(f"Stack room:  {headroom:>4} B  (SRAM - .data - .bss)")
    print(f"Safety pad:  {SAFETY_MARGIN:>4} B")
    print(f"ISR budget:  {ISR_PUSH_BUDGET:>4} B  (worst-case audio ISR push at deepest SP)")
    print()
    print("Per-root peak (top 8 reaches):")
    for root, rp, _ in peaks[:8]:
        print(f"  {rp:>4} B  {root}")
    print()
    print(f"Worst non-ISR peak: {peak:>4} B  ({peak_root})")
    print(f"+ ISR budget:       {ISR_PUSH_BUDGET:>4} B")
    print(f"= Combined peak:    {combined:>4} B")
    print()
    if combined >= headroom - SAFETY_MARGIN:
        print(f"FAIL: combined peak {combined} B >= headroom-margin "
              f"{headroom - SAFETY_MARGIN} B. Stack risks .bss collision.")
        print()
        print(f"Worst path ({peak_root}):")
        for step in path[:25]:
            print(f"  {step}")
        if len(path) > 25:
            print(f"  ... ({len(path) - 25} more frames)")
        return 1

    print(f"OK: combined {combined} B + margin {SAFETY_MARGIN} B <= headroom {headroom} B")
    print()
    print(f"Worst path from {peak_root} (top 20 frames):")
    for step in path[:20]:
        print(f"  {step}")
    if len(path) > 20:
        print(f"  ... ({len(path) - 20} more)")
    print()
    print("Top 10 functions by frame size:")
    top = sorted(frames_by_mangled.items(), key=lambda kv: -kv[1])[:10]
    for sym, frame in top:
        print(f"  {frame:>4} B  {sym}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
