#!/usr/bin/env python3
"""Extract scene cold copies + per-scene entry offsets from an
OVERLAY-linked Arduboy ELF.

Background: docs/scene-paging.md describes the scene-paging
architecture. This tool runs after the application has been linked
with platform/arduboy/scene.ld (which puts all four .scene.<NAME>
sections at the same VMA, the swap-bank base, with sequential LMAs
in flash). It produces three artifacts the build needs:

1. cold.<NAME>.bin — one per scene, the raw bytes of that scene's
   .scene.<NAME> section. These are appended to data.bin so the
   swap manager can read them from FX at runtime.

2. scene_offsets.h — generated header exposing the FX offsets +
   sizes for each scene's cold copy. The application's swap manager
   uses these to know where in FX a given scene lives.

3. scene_entries.cpp — generated source file defining the PROGMEM
   table `scene::scene_entries[ID_COUNT]`. Each entry is the
   bank-relative byte offsets of the scene's update / draw /
   on_enter / on_leave entry points. Computed by reading each
   entry symbol's VMA from `avr-nm` output and subtracting
   `__scene_bank_start`'s VMA.

Why a separate script: the build requires two link passes. The
first pass compiles a stub scene_entries.cpp (all entries 0xFFFF)
to produce an ELF where the .scene.* sections are laid out at
their final VMAs. We extract entry offsets from that ELF, then
regenerate scene_entries.cpp with real values and relink. Only
the data in scene_entries.cpp changes between the two passes —
function VMAs are stable, so the second link is just a rebuild of
one tiny source file plus a relink.

Usage:
    python -m tools.fxdata.scene_extract --elf <path>

By default reads build/rpg-arduboy-release/rpg.elf and writes to
build/fxdata/. Override with --elf and --out-dir.
"""
from __future__ import annotations

import argparse
import re
import subprocess
import sys
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parents[2]
DEFAULT_ELF = REPO_ROOT / "build" / "rpg-arduboy-release" / "rpg.elf"
DEFAULT_OUT = REPO_ROOT / "build" / "fxdata"

# Order MUST match scene::Id in engine/scene.h.
SCENE_NAMES = ["TITLE", "MAIN_MENU", "GATE", "PLAY"]

# Entry-point function name → (scene name suffix, struct field).
# Each scene defines its update/draw entry as `update_<name>_scene` /
# `draw_<name>_scene` (matching the bucket name lowercased). on_enter/
# on_leave are optional and currently unused.
def entry_symbols(scene_upper: str) -> dict[str, str]:
    snake = scene_upper.lower()
    return {
        "update":   f"update_{snake}_scene",
        "draw":     f"draw_{snake}_scene",
        "on_enter": f"on_enter_{snake}_scene",  # optional
        "on_leave": f"on_leave_{snake}_scene",  # optional
    }


def run(cmd: list[str]) -> str:
    r = subprocess.run(cmd, capture_output=True, text=True, encoding="utf-8", errors="replace")
    if r.returncode != 0:
        raise RuntimeError(f"{cmd[0]} failed: {r.stderr.strip()[:200]}")
    return r.stdout


def read_symbols(elf: Path) -> dict[str, int]:
    """name → VMA. Demangled C++ names. Includes `__scene_bank_start`."""
    out = run(["avr-nm", "--demangle", str(elf)])
    syms: dict[str, int] = {}
    for line in out.splitlines():
        m = re.match(r"^([0-9a-f]+)\s+\S\s+(.+)$", line)
        if m:
            syms[m.group(2).strip()] = int(m.group(1), 16)
    return syms


def find_entry_offset(syms: dict[str, int], bank_base: int, fn_basename: str) -> int:
    """Find the demangled symbol matching the given short name and return
    its bank-relative offset. Returns 0xFFFF if no symbol matches.

    AVR demangled names look like: `game::update_title_scene()`. We
    search for any symbol whose name contains the basename followed by
    `(` (i.e. it's a function with that exact short name).
    """
    needle = fn_basename + "("
    for name, vma in syms.items():
        if needle in name:
            offset = vma - bank_base
            if 0 <= offset <= 0xFFFE:
                return offset
            # Out of bank range — not a paged-in scene entry.
            return 0xFFFF
    return 0xFFFF


def extract_section(elf: Path, section: str, out_path: Path) -> int:
    """Extract a section's bytes via avr-objcopy --dump-section. Returns
    the number of bytes written, or 0 if the section was empty / absent."""
    try:
        run(["avr-objcopy", "--dump-section", f"{section}={out_path}",
             str(elf), str(out_path.with_suffix(".tmp"))])
    except RuntimeError as e:
        # Section may not exist on a no-paging build; that's an empty extract.
        out_path.write_bytes(b"")
        return 0
    finally:
        # objcopy writes a side-effect output ELF we don't need.
        tmp = out_path.with_suffix(".tmp")
        if tmp.exists():
            tmp.unlink()
    return out_path.stat().st_size if out_path.exists() else 0


# NOTE: scene_offsets.h emission was moved to tools/fxdata/build.py
# because the FX offsets depend on where the scene cold copies are
# appended to data.bin (= after the existing manifest payload). build.py
# is the single source of truth for data.bin layout, so it owns the
# offsets header. scene_extract still emits one as a stub so the first
# build pass has a header to compile against.
def emit_scene_offsets_h(layout: list[tuple[str, int, int]], out: Path,
                         data_bin_base_offset: int) -> None:
    """Emit the offsets header.

    layout: [(name, start_offset_in_data_bin, size), ...] in scene order.
    data_bin_base_offset: where the FIRST scene starts in data.bin (= end
                          of the existing manifest payload).
    """
    out.parent.mkdir(parents=True, exist_ok=True)
    lines = [
        "// AUTOGENERATED by tools/fxdata/scene_extract.py — DO NOT EDIT.",
        "// Source of truth: the OVERLAY-linked rpg.elf.",
        "",
        "#pragma once",
        "",
        "#include \"types.h\"",
        "#include \"scene.h\"  // scene::Id enum",
        "",
        "namespace scenes {",
        "",
    ]
    for name, off, size in layout:
        lines.append(f"constexpr u32 OFFSET_{name} = {off}u;")
        lines.append(f"constexpr u16 SIZE_{name}   = {size}u;")
    lines.append("")
    lines.append("// Lookup helpers indexed by scene::Id.")
    lines.append("inline u32 offset_for(u8 id) {")
    lines.append("  switch (id) {")
    for i, (name, _, _) in enumerate(layout):
        lines.append(f"    case scene::ID_{name}: return OFFSET_{name};")
    lines.append("    default: return 0xFFFFFFFFu;")
    lines.append("  }")
    lines.append("}")
    lines.append("inline u16 size_for(u8 id) {")
    lines.append("  switch (id) {")
    for i, (name, _, _) in enumerate(layout):
        lines.append(f"    case scene::ID_{name}: return SIZE_{name};")
    lines.append("    default: return 0;")
    lines.append("  }")
    lines.append("}")
    lines.append("")
    lines.append("}  // namespace scenes")
    lines.append("")
    out.write_text("\n".join(lines), encoding="utf-8")


def emit_scene_entries_cpp(entries: list[dict[str, int]], out: Path) -> None:
    """Emit a PROGMEM table of per-scene VTable structs whose fnptrs
    reference the actual entry-point SYMBOLS — not raw byte offsets.

    Why symbols, not offsets: avr-gcc's function-pointer ABI is
    counter-intuitive (word-address encoding for some forms, byte for
    others, and inline-asm gas relocations have their own quirks). The
    only fully-portable way to get a correct fnptr is to let the
    compiler resolve a symbol reference. The linker handles all the
    encoding details.

    Under our OVERLAY layout, each scene's entry-point symbol resolves
    to bank_base + within_bank_offset. When the bank is paged in with
    the right scene's bytes, calling through a fnptr that the linker
    set to (e.g.) bank_base+0x0034 jumps to the right code — because
    that ADDRESS holds the scene's update body once paging completes.

    entries: one dict per scene (Id order), values unused now (we
             reference symbols by name); the dict keys are kept so
             missing entries become `nullptr`.
    """
    out.parent.mkdir(parents=True, exist_ok=True)
    lines = [
        "// AUTOGENERATED by tools/fxdata/scene_extract.py — DO NOT EDIT.",
        "// Per-scene VTable table indexed by scene::Id. fnptrs resolve to",
        "// bank-resident addresses via the OVERLAY linker layout — when",
        "// scene X is paged in, the bytes at bank_base + within-X-offset",
        "// hold X's code, which is exactly what these fnptrs point at.",
        "",
        "#include \"scene.h\"",
        "#include \"progmem.h\"",
        "",
        "namespace game {",
        "// Forward decls so we can take addresses without including game.cpp.",
    ]
    # Forward-declare the entry symbols by demangled C name (in `game::`
    # namespace, where they live).
    for i, e in enumerate(entries):
        name = SCENE_NAMES[i]
        snake = name.lower()
        # Only forward-declare entries that exist (offset != 0xFFFF).
        if e['update'] != 0xFFFF:
            lines.append(f"void update_{snake}_scene();")
        if e['draw'] != 0xFFFF:
            lines.append(f"bool draw_{snake}_scene();")
    lines.append("}  // namespace game")
    lines.append("")
    lines.append("namespace scene {")
    lines.append("")
    lines.append("extern const VTable scene_vtables[ID_COUNT] PROGMEM;")
    lines.append("const VTable scene_vtables[ID_COUNT] PROGMEM = {")
    for i, e in enumerate(entries):
        name = SCENE_NAMES[i]
        snake = name.lower()
        update_expr   = f"game::update_{snake}_scene" if e['update'] != 0xFFFF else "nullptr"
        draw_expr     = f"game::draw_{snake}_scene"   if e['draw']   != 0xFFFF else "nullptr"
        on_enter_expr = "nullptr"  # not yet supported (no scene currently has one)
        on_leave_expr = "nullptr"
        lines.append(f"    // ID_{name}")
        lines.append(f"    {{ {update_expr}, {draw_expr}, {on_enter_expr}, {on_leave_expr} }},")
    lines.append("};")
    lines.append("")
    lines.append("}  // namespace scene")
    lines.append("")
    out.write_text("\n".join(lines), encoding="utf-8")


def emit_stub_artifacts(out_dir: Path) -> None:
    """Emit zero-content scene_offsets.h + scene_entries.cpp + four
    empty cold-copy bin files. Used for the FIRST link pass — engine/
    scene.cpp references these symbols via extern, so they must exist
    even before the OVERLAY-linked ELF is available to extract real
    values. The first build sees all entries = 0xFFFF and no FX cold
    copy bytes; running scene_extract on the resulting ELF then
    overwrites these with real values, and a second link picks them up.
    """
    out_dir.mkdir(parents=True, exist_ok=True)
    layout = [(name, 0, 0) for name in SCENE_NAMES]
    emit_scene_offsets_h(layout, out_dir / "scene_offsets.h", 0)
    # Stub entries: emit `scene_vtables` with all-nullptr rows. The first
    # build pass uses this so the symbol exists and engine/scene.cpp can
    # link cleanly. After the OVERLAY-linked ELF exists, scene_extract
    # regenerates with real symbol references — letting the linker
    # resolve fnptrs against the actual entry-point addresses.
    stub_entries = [
        {"update": 0xFFFF, "draw": 0xFFFF, "on_enter": 0xFFFF, "on_leave": 0xFFFF}
        for _ in SCENE_NAMES
    ]
    emit_scene_entries_cpp(stub_entries, out_dir / "scene_entries.cpp")
    for name in SCENE_NAMES:
        (out_dir / f"cold.{name}.bin").write_bytes(b"")


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--elf", type=Path, default=DEFAULT_ELF)
    ap.add_argument("--out-dir", type=Path, default=DEFAULT_OUT)
    ap.add_argument("--data-bin-base-offset", type=int, default=0,
                    help="Where the FIRST scene cold copy starts in "
                         "data.bin (= end of existing manifest payload). "
                         "Default 0 emits scene-only data; the wrapper "
                         "build target appends to the existing data.bin.")
    ap.add_argument("--stub", action="store_true",
                    help="Emit zero-content stub artifacts instead of "
                         "reading from the ELF. Used for the first link "
                         "pass before the OVERLAY-linked ELF exists.")
    args = ap.parse_args()

    if args.stub:
        emit_stub_artifacts(args.out_dir)
        print(f"fxdata scene_extract: stub artifacts written to {args.out_dir.relative_to(REPO_ROOT)}")
        return 0

    if not args.elf.exists():
        print(f"ELF not found: {args.elf}", file=sys.stderr)
        return 2

    syms = read_symbols(args.elf)
    bank_base = syms.get("__scene_bank_start")
    if bank_base is None:
        print("__scene_bank_start not found in ELF — was it linked with "
              "platform/arduboy/scene.ld? scene_extract requires the "
              "OVERLAY-enabled build.", file=sys.stderr)
        return 2

    print(f"__scene_bank_start = 0x{bank_base:04X}")

    # Extract each scene's bytes + compute entry offsets.
    args.out_dir.mkdir(parents=True, exist_ok=True)
    layout: list[tuple[str, int, int]] = []
    entries: list[dict[str, int]] = []
    cumulative = args.data_bin_base_offset

    for name in SCENE_NAMES:
        section = f".scene.{name}"
        bin_path = args.out_dir / f"cold.{name}.bin"
        size = extract_section(args.elf, section, bin_path)
        layout.append((name, cumulative, size))
        cumulative += size

        symbols_for = entry_symbols(name)
        e = {role: find_entry_offset(syms, bank_base, sym)
             for role, sym in symbols_for.items()}
        entries.append(e)
        print(f"  {name:10s} {size:5d} B  update=0x{e['update']:04X} "
              f"draw=0x{e['draw']:04X} on_enter=0x{e['on_enter']:04X} "
              f"on_leave=0x{e['on_leave']:04X}")

    emit_scene_offsets_h(layout, args.out_dir / "scene_offsets.h",
                         args.data_bin_base_offset)
    emit_scene_entries_cpp(entries, args.out_dir / "scene_entries.cpp")
    print(f"  -> {(args.out_dir / 'scene_offsets.h').relative_to(REPO_ROOT)}")
    print(f"  -> {(args.out_dir / 'scene_entries.cpp').relative_to(REPO_ROOT)}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
