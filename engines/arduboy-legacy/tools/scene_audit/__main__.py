"""scene_audit — figure out whether scene-paging is viable for this codebase.

Walks the build's avr-nm + avr-objdump output, builds a call graph from
the instruction stream, classifies every function into a "scene"
(CORE / MENUS / CUTSCENES / PLAYING / UNKNOWN) by name pattern, and
reports:

  - bytes per scene
  - cross-scene call edges (the things that BLOCK paging — if MENUS
    calls into a function only present in CUTSCENES, those two scenes
    can't be paged independently)
  - the always-resident set ("CORE": reachable from any scene; can't
    move) vs the pageable set
  - a verdict on whether scene-paging would actually save flash here

This is the load-bearing artifact that decides whether the
scene-paging architecture is worth implementing on this codebase.
If the audit reports >70% of code is in CORE (reachable everywhere),
scene-paging buys little; we should pivot. If <50% is CORE, paging
is a real win.

Run: `python -m tools.scene_audit`
Build first if needed: `make all`
"""
from __future__ import annotations

import re
import subprocess
import sys
from dataclasses import dataclass, field
from pathlib import Path

REPO = Path(__file__).resolve().parents[2]
ELF = REPO / "build" / "rpg-arduboy-release" / "rpg.elf"


# ---------------------------------------------------------------- scene rules --
#
# A scene is defined as a set of name-patterns. A function whose
# demangled name matches a CORE pattern is always-resident. A function
# matching MENUS/CUTSCENES/PLAYING is scene-specific *if no other scene
# also reaches it*. UNKNOWN is the bucket for anything that doesn't
# match — typically libgcc helpers, vector tables, or our own helpers
# that need a rule added here.
#
# Order matters: rules are checked in this order, first match wins.
SCENE_RULES: list[tuple[str, list[str]]] = [
    # --- CORE: always resident, can't be paged out ---
    ("CORE", [
        r"^__\w+",                              # libgcc, ctors, isr trampolines
        r"^_GLOBAL__sub_",                      # static initializers
        r"^main\b",                             # entry point
        r"\bnamespace\b.*::tick\b",             # audio::tick
        r"::__vector_\d+\b",                    # ISRs
        # AVR libc — eeprom + spi + display low-level
        r"^eeprom_(read|update|write)_(byte|word|dword|block)\b",
        r"\boled_cmd\b",
        r"\bspi_xfer\b",
        # framebuffer ops — used by every screen
        r"\bfb::draw_sprite\b",
        r"\bfb::draw_sprite_progmem\b",
        r"\bfb::clear_sprite\b",
        r"\bfb::stroke_rect\b",
        r"\bfb::fill_rect\b",
        r"\bfb::clear_rect\b",
        r"\bfb::clear\b",
        r"\bfb::invert_all\b",
        r"\bfb::set_pixel\b",
        r"\bfb::clear_pixel\b",
        r"\bfb::dither_invert\b",
        r"\bfb::BAYER4\b",                      # dither table
        # font ops — every screen draws text
        r"\bfont::draw_text\b",
        r"\bfont::draw_text_pgm\b",
        r"\bfont::draw_text_inverse\b",
        r"\bfont::draw_text_inverse_pgm\b",
        r"\bfont::draw_char\b",
        r"\bfont::draw_char_inverse\b",
        r"\bfont::::glyphs\b",
        r"\bfont::::index_of\b",
        # input ops
        r"\binput::pressed\b",
        r"\binput::held\b",
        r"\binput::poll\b",
        # storage ops
        r"\bstorage::read_meta\b",
        r"\bstorage::write_meta\b",
        r"\bstorage::wipe_meta\b",
        r"\bstorage::read_best_run\b",
        r"\bstorage::write_best_run\b",
        # audio platform
        r"\baudio::\w+\b",
        # FX flash + LZ77 + clock
        r"\bdata_flash::read\b",
        r"\blz77::decode\b",
        r"\blz77::decode_from_fx\b",
        # lz77 internals: FxStream and other anonymous helpers used by
        # decode_from_fx. Newer toolchains (MSYS2 gcc 14) inline these
        # so the symbols don't appear; CI's older gcc-avr 7.x emits
        # them out-of-line. Both are CORE-resident — called only
        # from decode_from_fx, which we already classify CORE.
        # Note: _callable_part strips `(anonymous namespace)` (line
        # 625's regex matches `(...)::` and replaces with `::`), so
        # `lz77::(anonymous namespace)::FxStream::next()` becomes
        # `lz77::::FxStream::next` (note the doubled `::`). Match
        # the post-strip form with the empty segment in place.
        r"\blz77::(?:[^:]*::)?FxStream\b",
        r"\bclock::\w+\b",
        # display
        r"\bdisplay::\w+\b",
        # sprite metadata accessors
        r"\bsprites::width\b",
        r"\bsprites::height\b",
        r"\bsprites::data\b",
        r"\bsprites::fx_offset\b",
        r"\bsprites::lz77_data\b",
        r"\bsprites::is_lz77\b",
        r"\bsprites::ram_data\b",
        r"\bsprites::pages_for\b",
        # sprite metadata tables — accessed by every draw
        r"\bsprites::::DATA\b",
        r"\bsprites::::WIDTHS\b",
        r"\bsprites::::HEIGHTS\b",
        r"\bsprites::::FX_OFFSETS_(PLAYER|BOSS|UB_ANIM|PEN_ANIM)\b",
        r"\bsprites::logo_title_LZ77\b",        # title screen logo
        # 1-byte trampoline data syms (ene_*_data, dart_*_data, sangue_*_data, bullet_data, enemy_data)
        # — these are sprite_id constants used everywhere a sprite is drawn
        r"\bsprites::(ene_|dart_|sangue_|bullet_data|enemy_data|player_)",
        # audio internals — ISR-adjacent, can't page
        r"\baudio::::step_voice_state\b",
        r"\baudio::::emit_voice\b",
        # tilemap
        r"\btilemap::draw\b",
        r"\btilemap::draw_fx\b",
        # math helpers — pure functions, used everywhere
        r"\bformat_roman_u16\b",
        r"\bformat_sangue_value\b",
        r"\bformat_descent_long\b",
        r"\bcopy_pgm_str\b",
        r"\bcopy_pgm_table_entry\b",
        r"\bdraw_roman\b",
        r"\bdraw_roman_or_nihil\b",
        r"\bdraw_position_of_total\b",
        r"\bdraw_sangue\b",
        r"\bdraw_sangue_u32\b",
        r"\bdraw_sangue_inverse\b",
        r"\bdraw_pip_bar\b",
        r"\bdraw_stat_row\b",
        r"\bdraw_progmem_sprite\b",
        r"\bdraw_progmem_sprite_mirrored\b",
        r"\bdraw_progmem_sprite_inverse\b",
        r"\bdraw_ram_sprite\b",
        r"\bdraw_fx_sprite\b",
        r"\bdraw_fx_sprite_mirrored\b",
        r"\bdraw_engraved_portrait\b",
        r"\bdraw_logo\b",
        r"\bdraw_logo_inverse\b",
        r"\bdraw_logo_lz77_inverse\b",
        r"\bdraw_pilgrim_name_header\b",
        r"\bdraw_footer_pgm\b",
        r"\bdraw_header_rule\b",
        r"\bdraw_text_screen_header\b",
        r"\bdraw_text_screen_footer\b",
        r"\bdraw_menu_pgm\b",
        r"\bdraw_scrollable_list\b",
        r"\bdraw_named_list\b",
        r"\bdraw_named_lookup_list\b",
        r"\bmenu_cursor_step\b",
        r"\bmenu_cursor_step4\b",
        r"\bstrlen_\b",
        r"\bstrcpy_\b",
        r"\bformat_roman\b",                    # u8 -> roman, used by draw_roman + screens
        r"\banim_def_frames_ptr\b",             # animation table dispatch, audio-frequency
        # Shared PROGMEM literals — deduplicated across screens
        r"::LIT_[A-Z_]+\b",
        # Class/tier/bullet name tables — used by both PLAYING (HUD) and MENUS (bestiary)
        r"::CLASS_(NAMES|LEVEL_NAMES)\b",
        r"::TIER_(OF_IDX|EMPEROR|KEEPER|THRALL|SINNER|FIEND)\b",
        r"::BULLET_NAMES\b",
        r"::BUL_NAME_\d+\b",
        r"::BULLET_SPRITE_BY_TIER\b",
        # Wave/circle dispatch tables — read by both PLAYING (spawn) and CUTSCENES (gate-card preview)
        r"::BOSS_(BY_CIRCLE|HP_BY_CIRCLE)\b",
        r"::BASE_ENEMY_BY_CIRCLE\b",
        # Animation frame tables — driven by tick_player_animation in PLAYING but referenced
        # via the anim_def_frames_ptr dispatch which is itself CORE
        r"::ANIM_(DEFS_PENITENT|DEFS_UNBURDENED|PEN_\w+|UB_\w+)\b",
        # SFX descriptors — fired from every scene (menu nav, combat, death)
        r"::SFX_\w+\b",
        # gate layout — drawn by CUTSCENES gate-card AND title-return; treat as CORE-shared
        r"\bimages::GATE_LAYOUT_data\b",
        # direction unit-vector LUT — used by every entity update
        r"\bdir::::UNIT\b",
        # entity ops
        r"\bent::\w+\b",
        r"\bdir::\w+\b",
        # the dispatcher itself
        r"\bgame::draw\b",
        r"\bgame::update\b",
        r"\bgame::init\b",
        r"\bscene::set_active\b",
        r"\bscene::current\b",
        r"::VTABLE_BY_STATE\b",
        r"::vtable_for_state\b",
        r"\bscene::scene_vtables\b",
        r"\bSCENE_ID_BY_STATE\b",
        # Vestigia save protocol — reachable from MAIN_MENU (manual save UI)
        # AND CUTSCENES (autosave on return_to_wood) AND PLAYING (end_run path).
        # Always-resident; placed in .text. The pattern matches both
        # `vestigia::foo` and `vestigia::(anonymous namespace)::foo`.
        r"\bvestigia::",
        r"\bvestigia_load_into_meta\b",
        # data_flash save API (vestigia backing) — must stay CORE for the
        # same reason: callable from any scene.
        r"\bdata_flash::save_(read|write_page|erase_sector)\b",
        # Anonymous helpers in platform/arduboy/data_flash.cpp — internal
        # to the save path, used by save_*. CORE-resident with their callers.
        r"\bchip_read_abs\b",
        r"\bwait_wip_clear\b",
        r"\bensure_cs_configured\b",
        # Bootloader trampoline — called only by scene::set_active during
        # SPM swap; needs to be CORE (and the swap-manager itself is CORE).
        r"\bkp_boot::",
        r"\bapp_spm_trampoline_entry\b",
        # Cover/uncover transition content — called from scene::set_active
        # which runs during EVERY bank crossing. Must be CORE. Pattern
        # covers both `transitions::foo` and the anon-namespace helpers.
        r"\btransitions::",
        # Vestigia popup string-table data symbols (VP_OVERWRITE, VP_SAVE
        # etc.) referenced from draw_vestigia_popup. The data lives in
        # .text alongside other PROGMEM literals — they're CORE-resident
        # because draw_vestigia_popup is a MAIN_MENU bank function reading
        # them via `pgm_read_byte`, which works regardless of where the
        # *consumer* is paged but requires the data itself to be present.
        r"\bgame::VP_(OVERWRITE|CANCEL|SAVE|LOAD|OPTS_MODE\d)\b",
        # data_flash CS-line trampolines — 4-byte one-liners inlined
        # everywhere SPI talks to the chip. CORE.
        r"\bfx_cs_(low|high)\b",
        # libc helpers pulled in by the build — always-resident.
        r"^memset$",
        r"^memcpy$",
        r"^strlen$",
        r"^strcpy$",
    ]),

    # --- PLAYING: the gameplay state's draw + update path ---
    ("PLAYING", [
        r"\bupdate_player\b",
        r"\bupdate_enemy\b",
        r"\bupdate_bullet\b",
        r"\bupdate_pickup\b",
        r"\bupdate_play_scene\b",
        r"\bdraw_play_scene\b",
        r"\bspawn_enemy_at\b",
        r"\bspawn_player\b",
        r"\bspawn_boss\b",
        r"\bspawn_bullet\b",
        r"\bspawn_sangue\b",
        r"\bspawn_wave\b",
        r"\bany_enemies_alive\b",
        r"\bdraw_player_overlay\b",
        r"\bdraw_enemy_overlay\b",
        r"\bdraw_entity_sprite\b",
        r"\bdraw_hud\b",
        r"\bdraw_pause_menu\b",
        r"\bdraw_second_death\b",
        r"\bresolve_collisions\b",
        r"\bentity_bbox\b",
        r"\bbegin_play\b",
        r"\bresume_play\b",
        r"\btick_player_animation\b",
        r"\bset_player_anim\b",
        r"\bend_run\b",
        r"\bcircle_for_wave\b",
    ]),

    # --- CUTSCENES: pre/post-run beats ---
    ("CUTSCENES", [
        r"\bdraw_title\b",
        r"\bdraw_gate_card\b",
        r"\bdraw_gate_full\b",
        r"\bdraw_gate_faded\b",
        r"\bdraw_circle_card\b",
        r"\bdraw_card_text_strip\b",
        r"\breturn_to_wood\b",
        r"\bleave_title\b",
        r"\bchalice_payout\b",
        # Scene-bucket update/draw entry points
        r"\bupdate_title_scene\b",
        r"\bdraw_title_scene\b",
        r"\bupdate_gate_scene\b",
        r"\bdraw_gate_scene\b",
    ]),

    # --- MENUS: browse / configure / read ---
    ("MENUS", [
        r"\bdraw_main_menu\b",
        r"\bdraw_name_entry\b",
        r"\bdraw_upgrade_menu\b",
        r"\bdraw_stats_screen\b",
        r"\bdraw_stats_page_pilgrim\b",
        r"\bdraw_stats_page_portrait\b",
        r"\bdraw_stats_page_lifetime\b",
        r"\bdraw_shades\b",
        r"\bdraw_shades_list\b",
        r"\bdraw_shades_detail\b",
        r"\bdraw_shades_portrait\b",
        r"\bdraw_numerals\b",
        r"\bdraw_lexicon\b",
        r"\bdraw_text_screen\b",
        r"\bdraw_text_top_list\b",
        r"\bdraw_text_about_list\b",
        r"\bdraw_text_two_column_body\b",
        r"\bdraw_text_body_generic\b",
        r"\bdraw_text_body\b",
        r"\bdraw_tutorial\b",
        r"\bdraw_guide\b",
        r"\bdraw_vestigia_screen\b",
        r"\bdraw_vestigia_popup\b",
        r"\bvestigia_row_label\b",
        r"\bvestigia_row_sprite_id\b",
        r"\bguide_visible_actions\b",
        r"\bchalice_range\b",
        r"\bchalice_roll\b",
        r"\bupdate_main_menu_scene\b",
        r"\bdraw_main_menu_scene\b",
        r"\bshades_desc_to_ram\b",
        r"\btext_title_to_ram\b",
        r"\btext_line_to_ram\b",
        r"\btext_body\b",
        r"\bshades_mark_seen\b",
        # Bestiary tables (Roll of Shades — descriptions, names)
        r"::SH_[DN]\d+\b",
        r"::SHADES_(NAMES|DESCS)\b",
        r"::SHADES_PORTRAIT_NUDGE_DOWN\b",
        r"::RECKONING_PORTRAIT_NUDGE_DOWN\b",
        r"::RECKONING_STATS\b",
        # Roman numerals helper tables (used only by format_roman, but format_roman is CORE)
        # — keep these in CORE because format_roman is CORE
        # — handled by the format_roman::R<n> rule below
        # Pause menu tables
        r"::PAUSE_(OPTS?|OPT_\d+|CONFIRM_TITLE|CONFIRM_OPTS|CONF_OPT_\d+|EMPTY_TITLE)\b",
        # TEXT_SCREEN (in-game grimoire) tables — main menu navigates here
        r"::T_(TITLE|TOP|ABOUT|CTRL|DES|SAN|GUI|VIR)_[A-Z0-9]+\b",
        r"::TEXT_(TITLE_TOP|TITLE_ABOUT|TOP_TITLES|ABOUT_TITLES|SECTION_TITLES|FOOTER_BACK)\b",
        r"::TEXT_(CONTROLS|DESCENT|SANGUE|GUIDE|VIRTU)_(HEADS|DESCS|LINES)\b",
        # Numerals + Lexicon screens
        r"::NUM_(NAME|VAL)_[IVXLCDM]\b",
        r"::NUMERALS_(NAMES|VALUES)\b",
        r"::LEX_(NAME|VAL)_[A-Z]+\b",
        r"::LEX_NAME_\d+\b",
        r"::LEXICON_(NAMES|VALUES)\b",
    ]),
]

# Helpers that are pure data-table internals: classify them as CORE iff their
# parent function is CORE. We do this with explicit rules above (format_roman
# is CORE, so its constexpr R/romans tables go to CORE too).
SCENE_RULES[0][1].extend([
    r"\bformat_roman::R\d+\b",
    r"\bformat_roman::romans\b",
    r"\bROMAN_(SYMS|VALS)\b",
])


# ---------------------------------------------------------------- data types --

@dataclass(eq=False)
class Func:
    """One function symbol from the ELF. eq=False so Func is hashable by
    object identity — important because OVERLAY-linked builds can have
    multiple distinct Funcs at the same (addr, size, raw_name) key
    permutation; identity-keyed sets keep them all."""
    addr: int           # absolute byte address
    size: int
    raw_name: str       # mangled — UNIQUE across the build (linker-enforced)
    name: str           # demangled
    scene: str = ""     # filled by classify(); name-based bucket guess
    section: str = ""   # filled by parse_sections(); ELF section the symbol lives in
    callees: "set[Func]" = field(default_factory=set)  # functions called from this func


# ---------------------------------------------------------------- pipeline --

def run(cmd: list[str]) -> str:
    """Run a subprocess and return stdout, raising if it fails."""
    r = subprocess.run(cmd, capture_output=True, text=True, encoding="utf-8", errors="replace")
    if r.returncode != 0:
        raise RuntimeError(f"{cmd[0]} failed: {r.stderr.strip()[:200]}")
    return r.stdout


def demangle(names: list[str]) -> list[str]:
    """Batch-demangle C++ names via avr-c++filt."""
    if not names:
        return []
    inp = "\n".join(names)
    out = subprocess.run(
        ["avr-c++filt"], input=inp, capture_output=True, text=True, encoding="utf-8",
        errors="replace",
    )
    if out.returncode != 0:
        # Fall back to raw names.
        return names
    return out.stdout.splitlines()


def parse_nm(elf: Path) -> list[Func]:
    """Parse `avr-nm --print-size --numeric-sort` for function symbols.

    Each line for a function looks like:
        00000NNN SSSSSSSS T <name>
    where NNN is the address, SSSSSSSS the size, T = code (or t = local code).

    OVERLAY-build caveat: scene-paging links four `.scene.<NAME>` sections
    at the same VMA, so multiple distinct functions share an address.
    Returning a list (not an addr-keyed dict) keeps every collision-mate.
    Downstream consumers index by raw_name (linker-unique) or by
    (addr, section) tuples to disambiguate. An earlier addr-keyed dict
    silently dropped collision-mates — the survivor's section determined
    every cross-bank-edge report, and the report became fiction.
    """
    text = run(["avr-nm", "--print-size", "--numeric-sort", str(elf)])
    funcs: list[Func] = []
    for line in text.splitlines():
        m = re.match(r"^([0-9a-f]+)\s+([0-9a-f]+)\s+([Tt])\s+(\S+)", line)
        if not m:
            continue
        addr = int(m.group(1), 16)
        size = int(m.group(2), 16)
        name = m.group(4)
        if size == 0:
            continue
        funcs.append(Func(addr=addr, size=size, raw_name=name, name=name))
    # Batch-demangle. Parallel lists are safe here because we built both
    # in the same iteration order, with no dict-collision drops.
    raw_names = [f.raw_name for f in funcs]
    demangled = demangle(raw_names)
    if len(demangled) == len(raw_names):
        for f, dn in zip(funcs, demangled):
            f.name = dn
    return funcs


def parse_objdump(elf: Path, funcs: list[Func]) -> None:
    """Parse `avr-objdump -d` output and populate Func.callees with the
    set of Funcs each function calls.

    AVR call/jmp instructions have several forms; we want absolute calls
    that reference another in-range function:

        2686:   0e 94 e0 13     call    0x27c0   ; 0x27c0 <foo>
        2698:   c0 ce           rcall   .+1664   ; 0x2d18 <bar>
        26a0:   0c 94 a0 13     jmp     0x2740   ; 0x2740 <baz>

    The "; 0xADDR <name>" suffix is what we extract. We classify all four
    flow-control ops (call, rcall, jmp, rjmp) as edges.

    OVERLAY disambiguation: scene-paging puts every `.scene.<NAME>`
    section at the same VMA. Multiple distinct functions therefore live
    at the same address, distinguished only by section. objdump's
    `<symbol>` hint after a call target is unreliable at OVERLAY
    addresses (it picks one collision-mate arbitrarily).

    Two corrections:

    1. The function-header (`<name>:` lines) is keyed by RAW NAME, which
       is linker-unique. `current_fn` follows the actual disassembled
       function, not whichever collision-mate happens to share its addr.
    2. Call targets are resolved by (addr, section): when caller is in
       `.scene.X`, prefer a callee at addr in `.scene.X`; else fall
       back to a callee in `.text` (CORE — always resident); else give
       up. That matches runtime semantics: when X is the active bank,
       X's body at addr is what answers the call.

    Pre-fix, both layers conflated collision-mates and produced ~60
    fictitious cross-bank edges in steady state.
    """
    text = run(["avr-objdump", "-d", str(elf)])
    # Index by raw name (unique) for header attribution.
    by_raw: dict[str, Func] = {f.raw_name: f for f in funcs}
    # Index by addr-range → list of containing Funcs. Two layers needed
    # because an OVERLAY call target can fall:
    #   (a) at the entry-point of one or more functions (collision-mates
    #       at the same VMA), or
    #   (b) STRICTLY INSIDE a function's body, i.e. at a sub-label /
    #       internal jump target. In OVERLAY, (b) is hugely common: any
    #       intra-function jump in a same-bank function shows up in
    #       objdump as `call ADDR ; <some_other_bank_function>` because
    #       objdump's hint picks whichever globally-visible symbol
    #       happens to live at ADDR — and that symbol is in a different
    #       bank by definition (its own bank's function would have been
    #       inlined into the caller's text by avr-objdump's local-symbol
    #       fallback if it existed).
    #
    # Lookup precedence per call from `caller`:
    #   1. caller itself contains target → intra-function jump, ignore.
    #   2. another same-section function STARTS at target → same-bank call.
    #   3. a CORE (.text) function STARTS or CONTAINS target → CORE call.
    #   4. a different-bank function starts at target AND no same-bank
    #      function contains the addr → real cross-bank edge.
    #
    # The pre-fix audit collapsed (1)/(2)/(3) into (4) at every OVERLAY
    # collision and produced ~60 phantom cross-bank edges.
    by_start: dict[int, list[Func]] = {}
    for f in funcs:
        by_start.setdefault(f.addr, []).append(f)
    starts_in_text = sorted(f.addr for f in funcs if f.section == ".text")

    import bisect

    def text_contains(addr: int) -> bool:
        """True if a CORE (.text) function's body contains addr."""
        i = bisect.bisect_right(starts_in_text, addr) - 1
        if i < 0:
            return False
        start = starts_in_text[i]
        # Any .text func at this start that contains addr.
        for f in by_start.get(start, []):
            if f.section == ".text" and addr < f.addr + f.size:
                return True
        return False

    def resolve_callee(target: int, caller: Func) -> Func | None:
        """Pick the right collision-mate for a call from `caller` to
        `target`. See the precedence list above."""
        # (1) Intra-function jump — target is inside caller's own body.
        if caller.addr <= target < caller.addr + caller.size:
            return None
        starts = by_start.get(target, [])
        # (2) Same-section function entry-point at target.
        same = [f for f in starts if f.section == caller.section]
        if same:
            return same[0]
        # (3) CORE function at or containing target.
        core_at_start = [f for f in starts if f.section == ".text"]
        if core_at_start:
            return core_at_start[0]
        if text_contains(target):
            # Inside a CORE function's body — sub-label of a CORE func;
            # not a true call edge, ignore.
            return None
        # (4) Different-bank function starts here. Before flagging as a
        # cross-bank edge, check whether ANY same-bank function contains
        # this target as a sub-label — if so, it's really an intra-bank
        # internal jump that objdump mis-hinted to a different bank.
        # Walk same-section functions and check containment.
        if caller.section.startswith(".scene."):
            for f in funcs:
                if f.section != caller.section:
                    continue
                if f is caller:
                    continue
                if f.addr <= target < f.addr + f.size:
                    return None  # sub-label inside another same-bank func
        # Genuinely a different-bank entry point. Pick the bank entry.
        diff = [f for f in starts if f.section.startswith(".scene.")]
        if diff:
            return diff[0]
        return None

    current_fn: Func | None = None
    fn_header_re = re.compile(r"^([0-9a-f]+) <([^>]+)>:")
    call_re = re.compile(r"^\s*([0-9a-f]+):\s+[0-9a-f ]+\s+(call|rcall|jmp|rjmp)\b.*?;\s*0x([0-9a-f]+)\s+<")
    # Address-load instructions: GCC emits `ldi rXX, hi8(symbol)` /
    # `ldi rXX, lo8(symbol)` / `ldi rXX, pm_hi8(symbol)` etc. when
    # taking the address of a function for an indirect call (icall) or
    # storing it in a fnptr table. The disassembly shows the target as
    # `; 0xADDR <name>` after the operands. Without catching these,
    # cross-bank references via fnptr are invisible to the audit —
    # which is exactly the discrepancy the linker's NOCROSSREFS catches
    # (historically caught a draw_wood_transition->draw_main_menu fnptr
    # load Apr 2026; that function has since been deleted).
    addr_load_re = re.compile(
        r"^\s*([0-9a-f]+):\s+[0-9a-f ]+\s+(ldi|lds)\b.*?;\s*0x([0-9a-f]+)\s+<")

    for line in text.splitlines():
        h = fn_header_re.match(line)
        if h:
            raw = h.group(2)
            # Re-anchor on REAL function entries by raw_name (which is
            # linker-unique) — preserve current_fn across sub-labels
            # (.L123, .Loc.5) that lack nm entries.
            new_fn = by_raw.get(raw)
            if new_fn is not None:
                current_fn = new_fn
            # else: keep current_fn — this is a sub-label, not a function boundary.
            continue
        if current_fn is None:
            continue
        c = call_re.match(line)
        if c:
            target = int(c.group(3), 16)
            tf = resolve_callee(target, current_fn)
            if tf is None or tf is current_fn:
                continue
            current_fn.callees.add(tf)
            continue
        # Address-load: track as a callee too. This catches fnptr-table
        # loads and indirect call setup.
        a = addr_load_re.match(line)
        if a:
            target = int(a.group(3), 16)
            tf = resolve_callee(target, current_fn)
            if tf is None or tf is current_fn:
                continue
            current_fn.callees.add(tf)


def _callable_part(demangled: str) -> str:
    """Extract just the function-name portion of a demangled C++ name,
    stripping the trailing parameter list (and any `[clone .isra.0]`
    suffix). This is what scene rules should match against: a parameter
    type like `ent::Entity&` shouldn't make `update_enemy(ent::Entity&)`
    match the `\\bent::\\w+\\b` CORE rule.

    Subtleties:
      - `(anonymous namespace)::foo()` — skip the leading parens; only
        the trailing param list belongs to the function.
      - `format_roman(unsigned char, char*)::romans` — local statics
        carry the parent function's signature in their demangled name;
        strip the inner `(...)` so `::romans` is what rules see.
      - `::operator()(int)` — keep the operator-paren glyphs.
    """
    # Strip one trailing param list and optional [clone ...] suffix.
    s = re.sub(r"\([^()]*\)( \[clone[^\]]*\])?$", "", demangled)
    # If the demangled name embeds a parent-function signature
    # (`format_roman(unsigned char, char*)::romans`), strip that inner
    # `(args)` so the trailing `::sym` is what we match on.
    s = re.sub(r"\([^()]*\)::", "::", s)
    return s


def parse_sections(elf: Path, funcs: list[Func]) -> None:
    """Parse `avr-objdump -t` and populate Func.section with the ELF
    section name (`.text`, `.scene.PLAY`, etc.) each symbol lives in.

    `avr-objdump -t` emits lines like:
        00006350 g     F .scene.PLAY  00000130 _ZN4game...

    Match by raw (mangled) name — linker-unique even at OVERLAY VMAs.
    Symbols not found in the dump (e.g. asm labels, internal compiler
    symbols) keep an empty section string.

    MUST run before parse_objdump so resolve_callee() can use sections
    for OVERLAY disambiguation.
    """
    text = run(["avr-objdump", "-t", str(elf)])
    by_raw: dict[str, Func] = {f.raw_name: f for f in funcs}
    sym_re = re.compile(r"^[0-9a-f]+\s+\S+\s+F\s+(\S+)\s+[0-9a-f]+\s+(\S+)$")
    for line in text.splitlines():
        m = sym_re.match(line)
        if not m:
            continue
        section = m.group(1)
        raw = m.group(2)
        f = by_raw.get(raw)
        if f is not None:
            f.section = section


def cross_section_edges(funcs: list[Func]) -> list[tuple[Func, Func]]:
    """Find caller->callee edges where caller is in `.scene.X` and callee
    is in `.scene.Y` (Y != X) — these are forbidden cross-bank calls.
    A function in scene X's bank cannot call into scene Y's bank: when
    X is paged in, Y's bytes aren't there, the call goes to garbage.
    Calls into `.text` (CORE) are always OK because CORE is resident.
    """
    edges: list[tuple[Func, Func]] = []
    for f in funcs:
        if not f.section.startswith(".scene."):
            continue
        for cb in f.callees:
            if not cb.section.startswith(".scene."):
                continue  # call into .text/CORE — fine
            if cb.section == f.section:
                continue  # same bank — fine
            edges.append((f, cb))
    return edges


def classify(funcs: list[Func]) -> None:
    """Assign each function a scene by matching its demangled name
    against SCENE_RULES in order. Anything that doesn't match goes to
    UNKNOWN."""
    compiled = [
        (scene, [re.compile(p) for p in pats])
        for scene, pats in SCENE_RULES
    ]
    for f in funcs:
        callable_name = _callable_part(f.name)
        for scene, pats in compiled:
            if any(p.search(callable_name) for p in pats):
                f.scene = scene
                break
        if not f.scene:
            f.scene = "UNKNOWN"


# ---------------------------------------------------------------- analysis --

def cross_scene_edges(funcs: list[Func]) -> list[tuple[Func, Func]]:
    """Return all (caller, callee) pairs where they're in different scenes
    and neither is CORE. CORE is always resident so calls into/out of it
    don't block paging; only pageable-to-pageable cross-edges block."""
    edges: list[tuple[Func, Func]] = []
    for f in funcs:
        for cb in f.callees:
            if f.scene == cb.scene:
                continue
            if f.scene == "CORE" or cb.scene == "CORE":
                continue
            edges.append((f, cb))
    return edges


def reachability_from(funcs: list[Func], scene: str) -> "set[Func]":
    """All Funcs reachable (transitively) from any function in `scene`.
    Used to compute "what's actually used by PLAYING" so we can see
    what'd have to be in CORE no matter what."""
    visited: set[Func] = set()
    stack: list[Func] = [f for f in funcs if f.scene == scene]
    while stack:
        f = stack.pop()
        if f in visited:
            continue
        visited.add(f)
        for cb in f.callees:
            if cb not in visited:
                stack.append(cb)
    return visited


# ---------------------------------------------------------------- output --

def report(funcs: list[Func]) -> str:
    out: list[str] = []
    total_bytes = sum(f.size for f in funcs)

    # Per-scene size totals
    scene_sizes: dict[str, int] = {}
    scene_funcs: dict[str, list[Func]] = {}
    for f in funcs:
        scene_sizes[f.scene] = scene_sizes.get(f.scene, 0) + f.size
        scene_funcs.setdefault(f.scene, []).append(f)

    out.append("# Scene Audit")
    out.append("")
    out.append(f"Total code bytes: **{total_bytes}** (across {len(funcs)} functions)")
    out.append("")
    out.append("## Bytes per scene")
    out.append("")
    out.append("| Scene | Bytes | Funcs | % of total |")
    out.append("|---|---:|---:|---:|")
    for scene in ("CORE", "PLAYING", "MENUS", "CUTSCENES", "UNKNOWN"):
        sz = scene_sizes.get(scene, 0)
        nfn = len(scene_funcs.get(scene, []))
        pct = 100.0 * sz / total_bytes if total_bytes else 0
        out.append(f"| {scene} | {sz} | {nfn} | {pct:.1f}% |")
    out.append("")

    # Reachability check: what's truly always-resident?
    playing_reach = reachability_from(funcs, "PLAYING")
    out.append("## Reachability from PLAYING")
    out.append("")
    out.append("Every function transitively reachable from a PLAYING-scene")
    out.append("function MUST be always-resident (or PLAYING itself can't run).")
    out.append("This is the floor on CORE size.")
    out.append("")
    pl_reach_bytes = sum(f.size for f in playing_reach)
    out.append(f"Reachable from PLAYING: **{pl_reach_bytes} B** ({len(playing_reach)} functions)")
    out.append(f"  -> Of which already classified CORE: {sum(f.size for f in playing_reach if f.scene == 'CORE')} B")
    out.append(f"  -> Of which classified MENUS/CUTSCENES (must promote to CORE): {sum(f.size for f in playing_reach if f.scene in ('MENUS', 'CUTSCENES'))} B")
    out.append(f"  -> Of which UNKNOWN (audit gap, also must promote): {sum(f.size for f in playing_reach if f.scene == 'UNKNOWN')} B")
    out.append("")

    # The verdict
    truly_pageable = total_bytes - pl_reach_bytes
    out.append("## Verdict")
    out.append("")
    out.append(f"Always-resident floor: **{pl_reach_bytes} B**")
    out.append(f"Pageable ceiling:      **{truly_pageable} B**")
    out.append("")
    if truly_pageable < 2048:
        out.append("**[NO] Scene-paging not worth it.** Pageable code is < 2 KB —")
        out.append("the trampoline + swap manager (~500 B always-resident overhead)")
        out.append("would eat most of the savings.")
    elif truly_pageable < 5120:
        out.append("**[MAYBE] Marginal.** 2-5 KB pageable. Worth doing only if a feature")
        out.append("is actively blocked by flash AND cold-string FX migration is exhausted.")
    else:
        out.append("**[YES] Scene-paging viable.** > 5 KB pageable code. Implementing the")
        out.append("trampoline + swap manager is a clear win.")
    out.append("")

    # Section-based bank overview (post-tagging)
    section_sizes: dict[str, int] = {}
    section_funcs: dict[str, list[Func]] = {}
    for f in funcs:
        sec = f.section or "(unsectioned)"
        section_sizes[sec] = section_sizes.get(sec, 0) + f.size
        section_funcs.setdefault(sec, []).append(f)
    scene_sections = sorted(s for s in section_sizes if s.startswith(".scene."))
    if scene_sections:
        out.append("## Per-bank ELF sections (post-tagging)")
        out.append("")
        out.append("Real bytes the linker placed into each `.scene.<NAME>` section.")
        out.append("These are the cold-copy candidates — what'll move to FX once the")
        out.append("linker script + swap manager land.")
        out.append("")
        out.append("| Section | Bytes | Funcs |")
        out.append("|---|---:|---:|")
        for sec in scene_sections:
            out.append(f"| `{sec}` | {section_sizes[sec]} | {len(section_funcs[sec])} |")
        bank_total = sum(section_sizes[s] for s in scene_sections)
        out.append(f"| **total in banks** | **{bank_total}** | |")
        out.append("")

    # Cross-bank edges (real, post-tagging — distinct from name-based scenes)
    bank_edges = cross_section_edges(funcs)
    out.append(f"## Cross-bank edges (real ELF sections) — {len(bank_edges)} found")
    out.append("")
    if bank_edges:
        out.append("These call from one `.scene.X` section into another `.scene.Y` —")
        out.append("the destination's bytes won't be loaded when X is the active bank.")
        out.append("Either move the callee to CORE (untag it) or tag the caller into")
        out.append("the same scene as the callee.")
        out.append("")
        out.append("| Caller (section) | -> | Callee (section) |")
        out.append("|---|---|---|")
        for caller, callee in bank_edges[:50]:
            out.append(f"| `{caller.name[:60]}` ({caller.section}) | -> | `{callee.name[:60]}` ({callee.section}) |")
        if len(bank_edges) > 50:
            out.append(f"| ...+{len(bank_edges) - 50} more | | |")
    else:
        out.append("**None.** Every cross-bank call lands in CORE.")
    out.append("")

    # Cross-scene edges (name-based — kept for legacy comparison)
    edges = cross_scene_edges(funcs)
    out.append(f"## Cross-scene edges (name-based) — {len(edges)} found")
    out.append("")
    if edges:
        out.append("These edges call between two PAGEABLE scenes. If they exist,")
        out.append("those scenes can't be loaded independently — when one is paged in,")
        out.append("the other still needs to be reachable, defeating the purpose.")
        out.append("")
        out.append("| Caller | -> | Callee |")
        out.append("|---|---|---|")
        for caller, callee in edges[:50]:
            out.append(f"| `{caller.name[:60]}` ({caller.scene}) | -> | `{callee.name[:60]}` ({callee.scene}) |")
        if len(edges) > 50:
            out.append(f"| ...+{len(edges) - 50} more | | |")
    else:
        out.append("**None.** Pageable scenes don't call each other directly.")
    out.append("")

    # UNKNOWN bucket — audit gaps
    unknowns = sorted(scene_funcs.get("UNKNOWN", []), key=lambda f: -f.size)
    out.append(f"## UNKNOWN — needs classification ({len(unknowns)} funcs, "
               f"{scene_sizes.get('UNKNOWN', 0)} B)")
    out.append("")
    out.append("Add patterns to SCENE_RULES in tools/scene_audit/__main__.py")
    out.append("to classify these. Anything > 100 B is worth an explicit rule.")
    out.append("")
    if unknowns:
        out.append("| Bytes | Function |")
        out.append("|---:|---|")
        for f in unknowns[:30]:
            out.append(f"| {f.size} | `{f.name[:90]}` |")
        if len(unknowns) > 30:
            out.append(f"| ... | +{len(unknowns) - 30} more |")
    out.append("")

    return "\n".join(out)


def main() -> int:
    import argparse
    ap = argparse.ArgumentParser(description="scene-paging viability audit")
    ap.add_argument("--strict", action="store_true",
                    help="exit non-zero if any function is UNKNOWN (audit "
                         "rules drift from reality) or if there's a "
                         "cross-scene call edge between two pageable scenes")
    args = ap.parse_args()

    if not ELF.exists():
        print(f"build artifact not found: {ELF}", file=sys.stderr)
        print("run `make all` first", file=sys.stderr)
        return 2
    funcs = parse_nm(ELF)
    parse_sections(ELF, funcs)  # Must precede parse_objdump so OVERLAY-aware
                                 # call resolution can use sections.
    parse_objdump(ELF, funcs)
    classify(funcs)
    print(report(funcs))

    if args.strict:
        unknowns = [f for f in funcs if f.scene == "UNKNOWN"]
        edges = cross_scene_edges(funcs)
        bank_edges = cross_section_edges(funcs)
        problems: list[str] = []
        if unknowns:
            unknown_bytes = sum(f.size for f in unknowns)
            problems.append(
                f"{len(unknowns)} function(s) classified UNKNOWN "
                f"({unknown_bytes} B). Add patterns to SCENE_RULES in "
                f"tools/scene_audit/__main__.py to classify them — see "
                f"the 'UNKNOWN' table in the report above for the list."
            )
        if bank_edges:
            problems.append(
                f"{len(bank_edges)} cross-bank call edge(s): a function "
                f"in `.scene.X` calls into `.scene.Y`. When X is paged "
                f"in, Y's bytes aren't there — the call jumps to garbage. "
                f"Move the callee to CORE (drop its SCENE_FN tag) or "
                f"retag the caller. See the 'Cross-bank edges' table."
            )
        # Name-based cross_scene_edges is informational, not a strict-fail
        # condition. The section-based cross_bank_edges check above is
        # authoritative — name-based scene buckets and ELF section names
        # don't always agree (return_to_wood is in CUTSCENES name-bucket
        # but in CORE section after promotion), and that's expected.
        if problems:
            print("", file=sys.stderr)
            print("scene_audit --strict: FAIL", file=sys.stderr)
            for p in problems:
                print(f"  - {p}", file=sys.stderr)
            return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
