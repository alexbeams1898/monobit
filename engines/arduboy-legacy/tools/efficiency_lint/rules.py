"""Lint rules. Each rule is a function that takes (relpath, text) and yields
Findings. Register the rule in ALL_RULES at the bottom of the file.

Adding a rule
-------------
1. Write a function `def check_FOO(relpath, text) -> Iterable[Finding]:`.
2. Add `(name, check_FOO, doc)` to ALL_RULES.
3. Add a test in tests/test_rules.py demonstrating both a hit and a clean
   sample for the new rule.

Each rule's docstring should explain the trap, the cost, and the fix.
Future maintainers (including future Claude) read these to understand WHY
the rule exists, not just that it does.
"""

from __future__ import annotations

import re
from typing import Iterable

from .lint import Finding, line_of


# ---------------------------------------------------------------- helpers --

# Skip CLAUDE-allowed exceptions. Some files are entry points or shims that
# are EXPECTED to break the rules below.
_RULE_EXEMPT_FILES: dict[str, set[str]] = {
    # The progmem shim itself defines PROGMEM and intentionally pulls in the
    # raw header.
    "engine/progmem.h": {"unjustified-32bit-cast", "function-static-no-progmem"},
    # The fixed-point math header uses i32 widening as its core idiom and
    # divides u32 inputs as part of its core function.
    "engine/fixed.h": {"unjustified-32bit-cast", "u32-divide-not-justified"},
    # framebuffer.cpp's four sprite-blit variants (draw/clear/outline ×
    # RAM/PROGMEM) share column-iteration framing by intent — unifying
    # via function-pointer adds 1.5% CPU per draw frame in the hot path,
    # not worth ~150 B of flash savings. Justified at the function level
    # in each variant's comment.
    "engine/framebuffer.cpp": {"duplicate-block"},
    # sprites.cpp is a PROGMEM data file: each sprite is a hand-tuned
    # byte array. The duplicate-block rule fires constantly on common
    # edge bytes (rows of 0x00, 0xFF, etc.) — the bytes are intentionally
    # in their own sprite, not extractable to a helper.
    "games/rpg/sprites.cpp": {"duplicate-block"},
    # images.cpp is the same case — full-screen 1024-byte image arrays.
    "games/rpg/images.cpp": {"duplicate-block"},
}

# Prefix-based exemptions for entire subtrees. AVR-specific rules don't
# apply to PC platform code: the SDL build runs on hosts with cheap u32
# math, .data is plentiful, etc. The cross-platform rules
# (stdout-binary-without-setmode, etc.) still apply.
_RULE_EXEMPT_PREFIXES: dict[str, set[str]] = {
    "platform/sdl/": {
        "unjustified-32bit-cast",
        "u32-divide-not-justified",
        "function-local-string-literal",
        "function-static-no-progmem",
        "duplicate-block",
    },
    # platform/arduboy/ is the canonical AVR shim — most rules apply
    # but its raw-AVR-header use is by design (per CLAUDE.md), and its
    # storage/audio code mirrors SDL by intent.
    "platform/arduboy/": {
        "duplicate-block",
        # 32-bit math in audio.cpp is pre-existing; gate the rule there
        # behind explicit case-by-case justify rather than blanket exempt.
    },
}


def _exempt(relpath: str, rule: str) -> bool:
    if rule in _RULE_EXEMPT_FILES.get(relpath, set()):
        return True
    for prefix, rules in _RULE_EXEMPT_PREFIXES.items():
        if relpath.startswith(prefix) and rule in rules:
            return True
    return False


# Strip C/C++ comments and string literals before pattern matching, so a
# rule like "no (i32) cast" doesn't trip on "(i32) cast" appearing inside a
# comment that DESCRIBES the cast it just removed. Cheap and good enough —
# we're not building a full parser.
_BLOCK_COMMENT = re.compile(r"/\*.*?\*/", re.DOTALL)
_LINE_COMMENT  = re.compile(r"//[^\n]*")
_STRING_LIT    = re.compile(r'"(?:\\.|[^"\\])*"')


def strip_noise(text: str) -> str:
    """Remove comments and string literals while preserving line numbers
    (replace with spaces / newlines so character offsets are unchanged)."""
    def keep_newlines(m: re.Match) -> str:
        s = m.group(0)
        # Preserve the count of newlines so line_of() still works.
        return "".join("\n" if c == "\n" else " " for c in s)
    text = _BLOCK_COMMENT.sub(keep_newlines, text)
    text = _LINE_COMMENT.sub(keep_newlines, text)
    text = _STRING_LIT.sub(keep_newlines, text)
    return text


# --------------------------------------------------------- rule: 32-bit cast --

# Triggers libgcc's __udivmodsi4 / __mulhisi3 etc. — pulled from libgcc as
# 70+ B helpers each. On AVR an unjustified i32/u32 cast inside a function
# is almost always wrong; the few legitimate cases (sangue u32 totals, fx
# multiply chains) live in well-documented spots.
_CAST_RE = re.compile(r"\(\s*[ui]32\s*\)")


def check_unjustified_32bit_cast(relpath: str, text: str) -> Iterable[Finding]:
    """Flag (i32)/(u32) casts inside function bodies. Each one risks pulling
    in libgcc's 32-bit divide / multiply helpers (~70 B each). Justify in a
    comment on the same line OR within 3 lines above:
        // 32bit-ok: <reason>

    Costs: each unaccounted cast can pull in __udivmodsi4 (68 B),
    __umulhisi3 (~50 B), or worse. Empirically several casts in this
    codebase were redundant and shaved >250 B by being demoted to i16.
    """
    if _exempt(relpath, "unjustified-32bit-cast"):
        return
    stripped = strip_noise(text)
    for m in _CAST_RE.finditer(stripped):
        line = line_of(stripped, m.start())
        # Look at the original (unstripped) text: comment justification on
        # the same line OR up to 3 lines above counts.
        lines = text.splitlines()
        window_start = max(0, line - 4)
        window = "\n".join(lines[window_start:line])
        if "32bit-ok:" in window:
            continue
        yield Finding(
            rule="unjustified-32bit-cast",
            path=relpath,
            line=line,
            message="unjustified (i32)/(u32) cast — risks pulling libgcc 32-bit "
                    "helpers (~70 B each). Add a `// 32bit-ok: <reason>` comment "
                    "above if intentional, or demote to i16/u16.",
        )


# --------------------------------------------- rule: function-static no PROGMEM --

# `static const T arr[] = {...}` at function scope WITHOUT PROGMEM puts
# the array in .data (RAM). On AVR with 2.5 KB of RAM this is a real cost;
# every byte spent on a lookup table is a byte not available for the stack.

_FUNC_STATIC_RE = re.compile(
    r"\bstatic\s+const\s+\w[\w:\s\*&]*?\s+\w+\s*\[[^\]]*\]\s*(?:PROGMEM\s*)?=",
    re.MULTILINE,
)


def check_function_static_no_progmem(relpath: str, text: str) -> Iterable[Finding]:
    """Flag `static const T arr[] = {...}` at function scope without PROGMEM.

    Without PROGMEM such arrays land in .data (RAM-resident). On AVR the
    linker still emits the initialization payload to flash AND copies it
    into .data at boot — so it costs flash AND RAM. A PROGMEM array costs
    flash only and is the right answer for any read-only lookup table.

    A real example caught by hand earlier: `to_roman()` had `static const
    u16 vals[]` and `static const char* sym[]` consuming ~52 B of RAM
    until they got PROGMEM annotations.
    """
    if _exempt(relpath, "function-static-no-progmem"):
        return
    stripped = strip_noise(text)
    for m in _FUNC_STATIC_RE.finditer(stripped):
        # Skip if PROGMEM appears in the matched declaration.
        if "PROGMEM" in m.group(0):
            continue
        # Skip file-scope statics — heuristic: walk backwards to find the
        # nearest unmatched `{`. If none, we're at file scope (allowed).
        prefix = stripped[: m.start()]
        depth = prefix.count("{") - prefix.count("}")
        if depth <= 0:
            continue
        line = line_of(stripped, m.start())
        yield Finding(
            rule="function-static-no-progmem",
            path=relpath,
            line=line,
            message="function-scope `static const ...[] =` without PROGMEM lands "
                    "in .data (RAM). Add PROGMEM and read via pgm_read_*.",
        )


# ------------------------------------------------ rule: hot blood river trap --

# A `char buf[N]` followed within ~10 lines by a copy_pgm_table_entry() call
# is the canonical setup for the stack-smash documented in CLAUDE.md.
# copy_pgm_table_entry hardcodes TEXT_LINE_MAX (40) as its cap; any caller
# buffer < 40 B can be overflowed by a long-enough source string.

_BUF_RE   = re.compile(r"char\s+(\w+)\s*\[\s*(\d+|TEXT_LINE_MAX)\s*\]")
_COPY_RE  = re.compile(r"copy_pgm_table_entry\s*\([^)]*,\s*(\w+)\s*\)")
_TEXT_LINE_MAX = 40


def check_hot_blood_river_trap(relpath: str, text: str) -> Iterable[Finding]:
    """Detect the `char buf[N<40]` + `copy_pgm_table_entry(..., buf)` pattern.

    The bug: copy_pgm_table_entry's hardcoded TEXT_LINE_MAX=40 means it
    will write up to 40 bytes through the caller's pointer. A smaller
    local buffer overflows silently — sometimes by 1 byte (cap-1 NUL
    terminator), enough to smash a saved register or a global pointer.

    The "hot blood river" string ("HOT BLOOD RIVER.", 16 chars + NUL) was
    the canonical victim: it overflowed a 16-byte buf in
    draw_named_lookup_list and corrupted distant state.

    Fix: size the buffer to TEXT_LINE_MAX, or refactor the helper to take
    an explicit cap.
    """
    if _exempt(relpath, "hot-blood-river-trap"):
        return
    stripped = strip_noise(text)
    # Build a map of buffer name → (line_no, declared_size_int_or_None)
    bufs: dict[str, tuple[int, int | None]] = {}
    for m in _BUF_RE.finditer(stripped):
        name = m.group(1)
        size_token = m.group(2)
        if size_token == "TEXT_LINE_MAX":
            size: int | None = _TEXT_LINE_MAX
        else:
            try:
                size = int(size_token)
            except ValueError:
                size = None
        bufs[name] = (line_of(stripped, m.start()), size)
    # Match calls; check buffers used.
    for m in _COPY_RE.finditer(stripped):
        name = m.group(1)
        if name not in bufs:
            continue
        decl_line, decl_size = bufs[name]
        if decl_size is None or decl_size >= _TEXT_LINE_MAX:
            continue
        call_line = line_of(stripped, m.start())
        yield Finding(
            rule="hot-blood-river-trap",
            path=relpath,
            line=call_line,
            message=f"copy_pgm_table_entry called with buf '{name}' "
                    f"(declared {decl_size} B at line {decl_line}); helper "
                    f"writes up to TEXT_LINE_MAX={_TEXT_LINE_MAX} B and will "
                    f"smash the stack on long sources. Size buf[TEXT_LINE_MAX].",
        )


# ----------------------------------------- rule: extern PROGMEM mismatch --

# A forward decl `extern const T arr[];` MUST carry PROGMEM if the
# definition does. Without it AVR emits `ld` (RAM-load) instructions
# instead of `lpm` (flash-load) at the read site, returning garbage.
# The binary compiles AND links cleanly — it just hangs at boot.

_EXTERN_ARR_RE = re.compile(
    r"\bextern\s+const\b[^;]*\[[^\]]*\][^;]*;",
    re.MULTILINE,
)


def check_extern_progmem_mismatch(relpath: str, text: str) -> Iterable[Finding]:
    """Flag `extern const T arr[];` without PROGMEM.

    The decl and definition must agree on PROGMEM-ness. A mismatch causes
    AVR to emit RAM-load instructions for what's actually flash data,
    silently reading garbage. The CPU often hangs on such reads — this
    is the root cause of the early "hang at boot when you press A on
    SHADES" bug.

    Heuristic limit: this rule sees the decl line only and can't verify
    the definition has PROGMEM. We assume that any extern array decl in
    game/engine source is intended to point at PROGMEM data (it nearly
    always is for read-only tables) and require the annotation.
    """
    if _exempt(relpath, "extern-progmem-mismatch"):
        return
    stripped = strip_noise(text)
    for m in _EXTERN_ARR_RE.finditer(stripped):
        decl = m.group(0)
        if "PROGMEM" in decl:
            continue
        line = line_of(stripped, m.start())
        yield Finding(
            rule="extern-progmem-mismatch",
            path=relpath,
            line=line,
            message="`extern const T arr[]` decl without PROGMEM. If the "
                    "definition is PROGMEM, the mismatch causes AVR to emit "
                    "ld (RAM-load) instructions and read garbage at runtime.",
        )


# --------------------------------------------- rule: inline string-walk dup --

# Each `while (s[n]) ++n;` or `while (s[n]) { out[n] = s[n]; ++n; }` is a
# hand-rolled strlen / strcpy. Repeated, they become a real footprint
# concern AND a code-clarity loss. game.cpp's `strlen_` / `strcpy_` are
# the canonical helpers — every walk should go through them.

_WALK_RE = re.compile(
    r"while\s*\(\s*\w+\s*\[\s*\w+\s*\]\s*\)",
)


def check_duplicate_string_walk(relpath: str, text: str) -> Iterable[Finding]:
    """Flag inline `while (buf[n])` patterns that should use a shared helper.

    Hand-rolled `while (s[n]) ++n;` (strlen) and `while (s[n]) { out[n] =
    s[n]; ++n; }` (strcpy) bloated this codebase by ~150 B before the
    `strlen_` / `strcpy_` helpers in game.cpp swept them up. The helper
    inlines tighter than the duplicates and centralizes the idiom.

    Exemptions: the helpers themselves obviously contain the pattern. Add
    a `// walk-ok: <reason>` comment within 2 lines above for any genuine
    one-off that doesn't fit the helper signature.
    """
    if _exempt(relpath, "duplicate-string-walk"):
        return
    stripped = strip_noise(text)
    for m in _WALK_RE.finditer(stripped):
        line = line_of(stripped, m.start())
        # The strlen_/strcpy_ helpers themselves OF COURSE contain the
        # pattern. Detect by looking at the function name in the most-
        # recent `^name(` line above; if it's strlen_ or strcpy_, allow.
        lines = text.splitlines()
        # Look backward for the enclosing function signature (pragmatic: 30-line window).
        window_top = max(0, line - 30)
        recent     = "\n".join(lines[window_top:line])
        if re.search(r"\b(strlen_|strcpy_)\s*\(", recent):
            continue
        # Per-call justification.
        window_just = "\n".join(lines[max(0, line - 3):line])
        if "walk-ok:" in window_just:
            continue
        yield Finding(
            rule="duplicate-string-walk",
            path=relpath,
            line=line,
            message="inline `while (s[n])` walk — use strlen_() or strcpy_() "
                    "(defined in game.cpp). Add `// walk-ok: <reason>` above "
                    "if the call site really can't use the helper.",
            severity="warn",
        )


# --------------------------------------------- rule: orphan PROGMEM extern --

# Heuristic: any `extern const T NAME[N] PROGMEM;` whose NAME has zero
# referencing call sites across the source tree is dead. LTO usually
# eliminates the actual bytes from the binary, but the source clutter
# tempts future code to grow new uses (or to forget the array exists),
# AND the extern decl forces the compiler to emit it pre-LTO. The
# canonical fix is to delete both the decl and the definition.

_EXTERN_NAME_RE = re.compile(
    r"\bextern\s+const\b[^;]*?\b(\w+)\s*\[[^\]]*\]\s*PROGMEM\s*;",
)


def check_orphan_progmem_extern(relpath: str, text: str) -> Iterable[Finding]:
    """Flag PROGMEM extern decls with zero referencing call sites.

    Found by hand: TITLE_NATIVE_data and WOOD_data sat in images.cpp/h as
    1024 B of dead PROGMEM each — defined, declared, never referenced.
    LTO eliminated the bytes from the .hex but the source carried 130+
    lines of orphaned data plus extern decls. The cost is real (compile
    time, code clarity, temptation to flip them on without re-auditing).

    A dead extern is detected by reading every source file once into a
    union, then for each extern decl checking whether the name appears
    anywhere ELSE in that union. If not, it's dead.

    This rule operates per-file but needs cross-file knowledge; the
    runner pre-builds the corpus and passes it via lint.py's iter_sources
    if needed. Simple version: scan all sources in iter_sources at module
    init time and cache name occurrences.
    """
    # Lazy: scan ALL sources once when first called. Cache on the function.
    cache = getattr(check_orphan_progmem_extern, "_cache", None)
    if cache is None:
        from .lint import iter_sources
        all_text = ""
        for _, t in iter_sources():
            all_text += "\n" + t
        cache = strip_noise(all_text)
        check_orphan_progmem_extern._cache = cache  # type: ignore[attr-defined]

    if _exempt(relpath, "orphan-progmem-extern"):
        return
    stripped = strip_noise(text)
    for m in _EXTERN_NAME_RE.finditer(stripped):
        name = m.group(1)
        # Count occurrences of the name in the whole corpus. The decl AND
        # the definition each contain the name, so a live symbol will have
        # at least 3 (decl + def + 1 caller). Two = decl + def with no
        # caller = orphan.
        # Use word-boundary regex to avoid prefix matches.
        count = len(re.findall(r"\b" + re.escape(name) + r"\b", cache))
        if count <= 2:
            line = line_of(stripped, m.start())
            yield Finding(
                rule="orphan-progmem-extern",
                path=relpath,
                line=line,
                message=f"PROGMEM extern '{name}' has no referencing call sites "
                        f"in any source file ({count} occurrence(s) including "
                        f"decl + def). Delete the decl and the definition.",
                severity="warn",
            )


# --------------------------------------- rule: decode in always-dirty draw --

# Certain screens (PLAYING, GATE_CARD, animated cards) are marked
# always-dirty in game::draw()'s switch — they redraw every frame by
# design. Any expensive per-frame work inside those draw bodies compounds
# into 60/sec CPU burn. The canonical case (since deleted) was
# draw_wood_transition calling draw_main_menu() (a full lz77::decode of
# the forest image) every frame, spiking the chip to 100% for the
# duration of the ~1.5s transition.
#
# The rule: heavy calls (lz77::decode, tilemap::render_*, any "_pgm_*"
# bulk-read helper) inside a function whose name ends in _transition or
# _card must be preceded by an early-return guard in the same function
# body. A guard is any `if (...) return;` or `if (...) return false;`
# earlier in the function. The guard represents the "dirty check" — code
# that decides "nothing changed, skip the expensive work."

_HEAVY_CALLS = (
    "lz77::decode",
    "tilemap::render",
    "tilemap::draw",
    "fb::dither_invert",
    # Draw helpers known to themselves be full-frame repainters. These
    # fall under the same rule when called from an always-dirty parent:
    # their internal work (pgm reads, full clear_rects, etc.) compounds
    # 60/sec.
    "draw_main_menu(",
    "draw_gate_full(",
    "draw_gate_faded(",
)
# Suffixes that mark a function as called from an always-dirty draw
# branch in game::draw(). Anything ending in these names is presumed to
# render every frame and must self-cache.
_ALWAYS_DIRTY_SUFFIX = ("_transition", "_card", "_return")


def check_decode_in_always_dirty_draw(relpath: str, text: str) -> Iterable[Finding]:
    """Flag heavy per-frame calls in always-dirty draw functions.

    The trap: GATE_CARD, CIRCLE_CARD, and similar animated-card /
    transition states are always-dirty in the top-level game::draw()
    switch — their bodies run every frame by design. If the body calls
    lz77::decode or another draw function that does LZ77 / tilemap /
    bulk-PROGMEM work, the chip burns 100% CPU for the transition's
    full duration.

    The canonical instance (since deleted): draw_wood_transition() called
    draw_main_menu() every frame, which did a full LZ77 forest decode.
    ~87 decodes per 1.5-second transition. Cached to ~19 decodes with
    an internal dirty
    check (last_wood_level sentinel) — keeps the visual identical while
    cutting the per-transition CPU cost ~4.5x.

    Fix pattern: cache the state variables the draw depends on (level,
    flash phase, etc.); at the top of the draw, early-return when the
    cache says nothing changed. Exactly mirrors the per-screen dirty
    cache discipline that game::draw() applies to static screens.

    Exemption: `// always-dirty-ok: <reason>` on/above the call site.
    """
    if _exempt(relpath, "decode-in-always-dirty-draw"):
        return
    # Only scan the game source directory — the engine doesn't know about
    # always-dirty states; it's a game-level discipline.
    if not relpath.startswith("games/"):
        return
    stripped = strip_noise(text)
    # Find every function definition `<ret> <name>(...) {` and walk its body.
    # Pragmatic: split on `^<ident> <ident>(...) {` at column 0 or after a
    # `void ` / `bool ` etc. This misses templated / nested cases, fine.
    func_re = re.compile(
        r"^\s*(?:void|bool|u8|i8|u16|i16|static\s+\w+)\s+(\w+)\s*\([^)]*\)\s*\{",
        re.MULTILINE,
    )
    for m in func_re.finditer(stripped):
        fn_name = m.group(1)
        if not any(fn_name.endswith(suffix) for suffix in _ALWAYS_DIRTY_SUFFIX):
            continue
        # Find the function body's closing brace by depth-counting.
        body_start = m.end()
        depth = 1
        i = body_start
        while i < len(stripped) and depth > 0:
            c = stripped[i]
            if c == "{":
                depth += 1
            elif c == "}":
                depth -= 1
            i += 1
        body_end = i
        body = stripped[body_start:body_end]
        # A valid dirty-cache guard needs BOTH:
        #   1) An early-return keyed on a `last_*` cache variable:
        #      `if (... last_X ...) return;`
        #   2) A trailing assignment to some `last_*` variable that updates
        #      the cache.
        # A plain `if (flag) return` — like a flash-frame bypass — doesn't
        # qualify; it only skips on the trigger frame, not the 87 others.
        has_cache_guard  = bool(re.search(r"\bif\s*\([^)]*\blast_\w+[^)]*\)\s*(\{[^}]*?\s*)?return\b", body))
        has_cache_update = bool(re.search(r"\blast_\w+\s*=", body))
        is_dirty_cached  = has_cache_guard and has_cache_update
        # Look for heavy calls in the body.
        for heavy in _HEAVY_CALLS:
            idx = 0
            while True:
                idx = body.find(heavy, idx)
                if idx < 0:
                    break
                call_abs = body_start + idx
                # Per-call justification.
                lines_text = text.splitlines()
                call_line = line_of(stripped, call_abs)
                window = "\n".join(lines_text[max(0, call_line - 3):call_line])
                if "always-dirty-ok:" in window:
                    idx += len(heavy)
                    continue
                if not is_dirty_cached:
                    yield Finding(
                        rule="decode-in-always-dirty-draw",
                        path=relpath,
                        line=call_line,
                        message=f"'{heavy.rstrip('(')}' called in always-dirty draw "
                                f"function '{fn_name}' without a last_*-keyed dirty "
                                f"cache. Add a cache sentinel "
                                f"(`if (x == last_x) return;` + `last_x = x;`) or justify "
                                f"with `// always-dirty-ok: <reason>`.",
                    )
                idx += len(heavy)


# --------------------------------------------- rule: cursor-wrap-without-helper --

# The canonical menu cursor pattern is `X = (u8)((X + 1) % N); audio::play(SFX_MENU);`
# (and the up-direction mirror with `+ N - 1`). Open-coding this in every
# menu/list state branch produces ~60-80 B of duplicated code per site,
# which compounds across ~8 menus into ~500 B. The shared helper
# `menu_cursor_step(cursor, count)` (and its 4-direction sibling
# `menu_cursor_step4`) consolidates the pattern. Audited Apr 2026: 8
# call sites collapsed -> 544 B reclaimed.
#
# This rule flags any new occurrence of the pattern OUTSIDE the helper
# itself, ensuring future menu work routes through the helper.

_CURSOR_WRAP_RE = re.compile(
    r"\b\w+\s*=\s*\(u8\)\(\(\s*\w+\s*\+\s*1\s*\)\s*%\s*\w+\s*\)",
)


def check_cursor_wrap_without_helper(relpath: str, text: str) -> Iterable[Finding]:
    """Flag inline cursor-wrap patterns that should use menu_cursor_step.

    Trap: every menu screen open-codes the same `cursor = (cursor+1) % N`
    pattern with an SFX call, costing ~60-80 B per site. With 8 menus in
    the game that's ~500 B of duplication.

    Fix: call `menu_cursor_step(cursor, count)` (or `menu_cursor_step4`
    for the 4-direction sibling). Both helpers play SFX automatically and
    return whether the cursor moved.

    Exemption: `// cursor-wrap-ok: <reason>` on/above the line. Some
    cursor wraps are 4-direction or have non-standard SFX behavior — those
    legitimately need open-coding.
    """
    if _exempt(relpath, "cursor-wrap-without-helper"):
        return
    # Engine code never has menus; rule is game-only.
    if not relpath.startswith("games/"):
        return
    stripped = strip_noise(text)
    for m in _CURSOR_WRAP_RE.finditer(stripped):
        line = line_of(stripped, m.start())
        # Skip the helper itself (where this pattern lives by design).
        lines = text.splitlines()
        # Look upward ~30 lines to find the enclosing function name.
        window_top = max(0, line - 30)
        recent     = "\n".join(lines[window_top:line])
        if re.search(r"\bbool\s+menu_cursor_step\w*\s*\(", recent):
            continue
        # Per-call justification (look up to 6 lines above for marker —
        # the wrap line is often preceded by the input check, which is
        # itself preceded by the rationale comment).
        window_just = "\n".join(lines[max(0, line - 6):line])
        if "cursor-wrap-ok:" in window_just:
            continue
        yield Finding(
            rule="cursor-wrap-without-helper",
            path=relpath,
            line=line,
            message="open-coded cursor wrap detected. Use menu_cursor_step(cursor, "
                    "count) or menu_cursor_step4 (4-direction). Add "
                    "`// cursor-wrap-ok: <reason>` if open-coding is intentional.",
            severity="warn",
        )


# --------------------------------------------------- rule: duplicate-block --

# Catch-all duplication detector: scans for runs of >= MIN_DUP_LINES
# adjacent lines that appear in 2+ places across the source tree. Uses a
# rolling-hash window over normalized lines (whitespace-trimmed, comments
# stripped) so trivial reformatting doesn't mask matches.
#
# This is intentionally heuristic — false positives (e.g. similar table
# initializers) are filtered with the `// dup-ok: <reason>` marker. The
# rule's job is to surface *suspicious* repetition for human review.
#
# Tuned to MIN_DUP_LINES=8: smaller blocks have too many natural matches
# (loops, closing braces, switch cases) to be useful signal. The wood-
# transition + gate-card cache patterns we built this session would each
# fire as 12+ line duplicates if not consolidated.

_MIN_DUP_LINES = 8


def _norm_line(line: str) -> str:
    """Aggressive normalization for dup detection: strip whitespace,
    drop trailing comments, collapse spaces. Empty after = ignore."""
    s = re.sub(r"//.*$", "", line).strip()
    s = re.sub(r"\s+", " ", s)
    return s


def check_duplicate_block(relpath: str, text: str) -> Iterable[Finding]:
    """Flag runs of >= 8 source lines that appear in 2+ places repo-wide.

    The rule scans every source file once, builds a hash → locations map
    of every 8-line window (normalized), and yields a finding per file
    that contains a window appearing elsewhere.

    Caveats:
    - Heuristic only. False positives possible (sprite tables, ASCII
      art, sequential setup). Suppress per-block with `// dup-ok: <reason>`
      on or above the first line of the duplicate.
    - Detects exact-after-normalization matches only. Near-duplicates
      (one variable renamed) won't trigger. That's by design — full
      AST-based detection is a separate-tool problem.

    Wins-it-would-have-caught from the Apr 2026 session: the 8 menu
    cursor-wrap blocks (10 lines each), the wood/gate dirty-cache
    boilerplate before each was extracted, and the draw_sangue +
    draw_sangue_inverse near-duplicates.
    """
    if _exempt(relpath, "duplicate-block"):
        return
    cache = getattr(check_duplicate_block, "_cache", None)
    if cache is None:
        # Build the windows-to-locations map once, lazily.
        from .lint import iter_sources
        windows: dict[tuple[str, ...], list[tuple[str, int]]] = {}
        for src_relpath, src_text in iter_sources():
            lines = [_norm_line(L) for L in src_text.splitlines()]
            for i in range(len(lines) - _MIN_DUP_LINES + 1):
                window = tuple(lines[i:i + _MIN_DUP_LINES])
                # Skip windows containing only blank or trivial lines.
                non_trivial = sum(1 for L in window if len(L) > 4)
                if non_trivial < _MIN_DUP_LINES // 2:
                    continue
                windows.setdefault(window, []).append((src_relpath, i + 1))
        cache = windows
        check_duplicate_block._cache = cache  # type: ignore[attr-defined]

    # For this file, find windows that appear in cache with >= 2 locations.
    lines = text.splitlines()
    seen_starts = set()
    for i in range(len(lines) - _MIN_DUP_LINES + 1):
        window_norm = tuple(_norm_line(L) for L in lines[i:i + _MIN_DUP_LINES])
        non_trivial = sum(1 for L in window_norm if len(L) > 4)
        if non_trivial < _MIN_DUP_LINES // 2:
            continue
        locs = cache.get(window_norm, [])
        if len(locs) < 2:
            continue
        # Only yield once per duplicate region (skip adjacent overlapping
        # windows that all hash to the same bucket).
        if (i - 1) in seen_starts:
            seen_starts.add(i)
            continue
        seen_starts.add(i)
        # Find the OTHER locations (not this file at this offset).
        others = [(p, ln) for (p, ln) in locs if not (p == relpath and ln == i + 1)]
        if not others:
            continue
        # Per-block justification: dup-ok: in the 8 lines above the start
        # (often the marker lives on the enclosing function's comment
        # header, a few lines above the duplicate's first matching line).
        win_just = "\n".join(lines[max(0, i - 8):i])
        if "dup-ok:" in win_just:
            continue
        # Format the others list compactly.
        other_str = "; ".join(f"{p}:{ln}" for (p, ln) in others[:3])
        if len(others) > 3:
            other_str += f" (+{len(others) - 3} more)"
        yield Finding(
            rule="duplicate-block",
            path=relpath,
            line=i + 1,
            message=f"{_MIN_DUP_LINES}+ line block duplicated at: {other_str}. "
                    f"Consider extracting a shared helper. Add `// dup-ok: <reason>` "
                    f"above if intentional (e.g. PROGMEM table data).",
            severity="warn",
        )


# ---------------------------------------------- rule: u32-divide-not-justified --

# u32 / u32 divisions on AVR pull libgcc's __udivmodsi4 (~70 B) into the
# binary. One use is amortizable; multiple unjustified uses suggest an
# audit is overdue (we may be able to consolidate or demote).
#
# Apr 2026 session: audio::set_tone had `1000000UL / hz` which appeared
# to be the only u32 divide; turned out other consumers (e.g. perf logger
# u32 cycles) kept the helper alive regardless. Lesson: justify every
# u32 divide with a comment indicating WHY u32 math is needed AND
# whether the libgcc helper is shared with other call sites.

_U32_DIVIDE_RE = re.compile(
    r"\b[ui]32\b[^;{}]*\s/\s|/\s\(\s*[ui]32\s*\)|/\s*[a-zA-Z_]\w*UL\b",
)


def check_u32_divide_not_justified(relpath: str, text: str) -> Iterable[Finding]:
    """Flag u32/i32 divisions without a `// 32bit-ok:` justification.

    AVR has no hardware 32-bit divide. Each u32/i32 division pulls
    libgcc's __udivmodsi4 (~68 B) and emits ~12 cycles of register
    shuffling per call. One justified use is fine; multiple unjustified
    uses indicate the helper isn't free anymore — every divide costs
    extra cycles and code.

    Justify each u32 divide with `// 32bit-ok: <reason>` on the same
    line or up to 3 lines above. The reason should explain why u16
    isn't sufficient (e.g. "1_000_000 / hz exceeds u16 numerator").
    """
    if _exempt(relpath, "u32-divide-not-justified"):
        return
    stripped = strip_noise(text)
    for m in _U32_DIVIDE_RE.finditer(stripped):
        line = line_of(stripped, m.start())
        lines = text.splitlines()
        window_just = "\n".join(lines[max(0, line - 4):line])
        if "32bit-ok:" in window_just:
            continue
        yield Finding(
            rule="u32-divide-not-justified",
            path=relpath,
            line=line,
            message="u32/i32 division pulls libgcc __udivmodsi4 (~68 B). "
                    "Add `// 32bit-ok: <reason>` if the u32 width is required, "
                    "or rewrite to u16 math if the operands fit.",
            severity="warn",
        )


# --------------------------------------- rule: function-local-string-literal --

# Function-scope `const char* x = "literal";` patterns. The literal
# lands in .rodata which the AVR linker initializes-by-copy into .data
# (RAM) for any non-PROGMEM string accessed through a pointer. Each
# such literal eats N+1 bytes of RAM (string + NUL).
#
# Rule of thumb on this AVR: RAM is 2.5 KB total; even 30-byte literals
# in 5 spots is 150 B = 6% of total RAM. The death-screen audit
# (Apr 2026) reclaimed 24 B of RAM by moving two bare literals to
# PROGMEM. Cumulative cost across the codebase is meaningful.
#
# Fix: declare as `static const char NAME[] PROGMEM = "literal";` and
# read via `copy_pgm_str(NAME, buf, sizeof(buf))` before the render call.

_LOCAL_STRLIT_RE = re.compile(
    r'\bconst\s+char\s*\*\s*\w+\s*=\s*"[^"]+"\s*;',
)


def check_function_local_string_literal(relpath: str, text: str) -> Iterable[Finding]:
    """Flag function-scope `const char* x = "literal";` — lands in .data.

    The trap: a bare string literal assigned to a `const char*` pointer
    inside a function body lands in RAM (.data) on AVR, not flash. RAM
    is 2.5 KB total and these literals add up — the death-screen audit
    reclaimed 24 B by moving two such literals to PROGMEM.

    Fix: `static const char NAME[] PROGMEM = "...";` + `copy_pgm_str` at
    the use site. Costs ~5-15 B of flash per literal but saves N+1 B
    of RAM, which is the scarcer resource.

    Exemption: `// ram-literal-ok: <reason>` on or above the line.
    """
    if _exempt(relpath, "function-local-string-literal"):
        return
    # Run against ORIGINAL text — strip_noise erases string literals,
    # which is what we're trying to find. Use a comment-aware regex
    # that ignores `//` comments inline.
    nocomments = re.sub(r"//[^\n]*", "", text)
    nocomments = _BLOCK_COMMENT.sub("", nocomments)
    # Track brace depth manually so we only flag function-scope decls.
    depth = 0
    for m in _LOCAL_STRLIT_RE.finditer(nocomments):
        # Compute brace depth at this position by counting from start.
        prefix = nocomments[:m.start()]
        # Need to ignore braces inside string literals; cheap approximation:
        # mask string content first.
        masked = re.sub(r'"(?:\\.|[^"\\])*"', '""', prefix)
        depth  = masked.count("{") - masked.count("}")
        if depth <= 0:
            continue
        # Map char offset back to line number in ORIGINAL text by counting
        # newlines. Comments removed have the same line count via re.sub
        # because we kept newlines. Actually `re.sub("//...", "")` removes
        # newlines if the pattern matches up to but not including \n —
        # the `[^\n]*` ensures newlines are preserved.
        line = nocomments[:m.start()].count("\n") + 1
        lines = text.splitlines()
        window = "\n".join(lines[max(0, line - 3):line])
        if "ram-literal-ok:" in window:
            continue
        yield Finding(
            rule="function-local-string-literal",
            path=relpath,
            line=line,
            message="function-local `const char* x = \"...\"` lands in .data (RAM). "
                    "Declare `static const char NAME[] PROGMEM = \"...\"` and copy "
                    "via copy_pgm_str at the call site. Add `// ram-literal-ok:` "
                    "if the RAM cost is intentional.",
            severity="warn",
        )


# -------------------------------- rule: file-scope-struct-no-progmem --

# `const T NAME = {...};` or `constexpr T NAME = {...};` at file scope,
# where T is a non-primitive type (struct/class), without PROGMEM. The
# trap: when callers take its address (any `const T&` parameter, or the
# `&NAME` syntax), the linker is forced to emit it in .data, not as an
# inlined immediate. On AVR that costs RAM directly.
#
# Real example caught this session: 7 `constexpr audio::Sfx SFX_*`
# declarations, each ~5 B, totalling ~35 B of .data. Moving them to
# `const audio::Sfx SFX_* PROGMEM = {...}` and changing `play(const
# Sfx&)` to `play(const Sfx*)` (with pgm_read_byte copy) recovered 34 B
# of RAM at +6 B of flash.

# Identify: `const`/`constexpr` at file scope, followed by a non-primitive
# typename (capitalized or namespaced like `audio::Sfx`), an identifier,
# `=`, no PROGMEM marker. Skip primitive scalars (u8/u16/i8/i16/u32/i32/
# bool/char) — those usually inline. Also skip plain arrays handled by
# the function-static rule's array-aware sibling logic upstream.

_FILE_STRUCT_RE = re.compile(
    r"^\s*(?:const|constexpr)\s+([\w:]+)\s+(\w+)\s*=\s*\{",
    re.MULTILINE,
)
_PRIMITIVE_TYPES = {
    "u8", "u16", "u32", "i8", "i16", "i32",
    "uint8_t", "uint16_t", "uint32_t", "int8_t", "int16_t", "int32_t",
    "bool", "char", "int", "unsigned", "signed", "size_t",
    "float", "double",
}


def check_file_scope_struct_no_progmem(relpath: str, text: str) -> Iterable[Finding]:
    """Flag file-scope `const/constexpr <Struct> X = {...}` without PROGMEM.

    The trap: file-scope `const T x = {...}` for a non-primitive type
    forces .data emission whenever callers take its address (e.g.
    `play(const Sfx& sfx)` matches `play(SFX_SHOOT)`). Each such struct
    eats sizeof(T) bytes of RAM that flash could have held instead.

    Fix: `const T X PROGMEM = {...};` and change consumers to take a
    PROGMEM pointer (`const T* pgm_x`) and read via `pgm_read_byte`.

    Exemption: add `// ram-table-ok: <reason>` on or above the line if
    the struct is mutated, lives in .bss intentionally, or the type is
    too small for a PROGMEM round-trip to be worth it.
    """
    if _exempt(relpath, "file-scope-struct-no-progmem"):
        return
    stripped = strip_noise(text)
    for m in _FILE_STRUCT_RE.finditer(stripped):
        # Must be at file scope — check brace depth.
        prefix = stripped[: m.start()]
        depth  = prefix.count("{") - prefix.count("}")
        if depth > 0:
            continue
        type_name = m.group(1)
        # Skip primitives — those inline as immediates.
        bare = type_name.split("::")[-1]
        if bare in _PRIMITIVE_TYPES:
            continue
        # Look for PROGMEM in the rest of the declaration line (between
        # the type and the `=`, or right before `=`). If present, fine.
        # The pattern already excluded matches with PROGMEM before `{`,
        # but that wasn't enforced — re-check the declaration window.
        decl_window = stripped[m.start():m.start() + 200]
        if "PROGMEM" in decl_window.split("=")[0]:
            continue
        line = stripped[: m.start()].count("\n") + 1
        # Per-decl exemption check.
        lines = text.splitlines()
        window = "\n".join(lines[max(0, line - 3):line])
        if "ram-table-ok:" in window:
            continue
        yield Finding(
            rule="file-scope-struct-no-progmem",
            path=relpath,
            line=line,
            message=f"file-scope `const {type_name} {m.group(2)} = {{...}}` "
                    f"without PROGMEM lands in .data (RAM) when callers take "
                    f"its address. Add PROGMEM and change consumers to take a "
                    f"PROGMEM pointer + pgm_read_byte. "
                    f"Add `// ram-table-ok:` above to suppress.",
            severity="warn",
        )


# ----------------------------- rule: stdout-binary-without-setmode --

# Writing raw bytes to stdout/stderr on Windows requires switching the
# stream to binary mode first (_setmode(_fileno(stdout), _O_BINARY) or
# freopen(NULL, "wb", stdout)). Without it, every 0x0A in the byte stream
# becomes 0x0D 0x0A — silently shifting the alignment of every subsequent
# record by one byte per occurrence.
#
# Real bug caught (2026-04): platform/sdl/uart_log.cpp emitted a 7-byte
# perf-trace record per frame via fputc(stdout). On Windows this produced
# trace files with ~10 bare 0x0A bytes each preceded by injected 0x0D —
# perf_logger then read garbage 4M-cycle records and the frame-budget
# analysis was useless until we tracked it down.

_STDOUT_WRITE_RE = re.compile(
    # Match any fputc/fwrite/fputs/putc/putchar call whose argument list
    # mentions `stdout`. Use a lazy `.*?` instead of `[^)]*` because
    # casts like `(int)b` contain inner parens and would defeat a
    # paren-excluding character class.
    r"\b(?:std::)?(?:fputc|fwrite|fputs|putc|putchar)\s*\(.*?\bstdout\b",
)


def check_stdout_binary_without_setmode(relpath: str, text: str) -> Iterable[Finding]:
    """Flag stdout writes without binary-mode setup.

    Detect: any call that emits raw bytes to stdout (fputc/fwrite/fputs
    /putc/putchar with stdout). Pass: file contains `_setmode(...stdout`
    or `freopen(...stdout` or `_O_BINARY` somewhere — taken as evidence
    that the binary-mode switch is in place.

    Limitation: heuristic. A single-translation-unit check can't see init
    code in another file. Suppress with `// stdout-binary-ok: <reason>`
    on or above the offending line if the binary-mode setup lives
    elsewhere (e.g. in a shared init function).
    """
    if _exempt(relpath, "stdout-binary-without-setmode"):
        return
    stripped = strip_noise(text)
    # Pre-check: file already does binary-mode setup somewhere?
    if "_O_BINARY" in stripped or "_setmode" in stripped:
        return
    if re.search(r'freopen\s*\([^)]*"\w*b\w*"[^)]*\bstdout\b', stripped):
        return
    for m in _STDOUT_WRITE_RE.finditer(stripped):
        line = stripped[: m.start()].count("\n") + 1
        lines = text.splitlines()
        window = "\n".join(lines[max(0, line - 3):line])
        if "stdout-binary-ok:" in window:
            continue
        yield Finding(
            rule="stdout-binary-without-setmode",
            path=relpath,
            line=line,
            message="raw stdout write without binary-mode setup. On Windows, "
                    "stdio translates 0x0A to 0x0D 0x0A — corrupts any binary "
                    "byte stream. Call `_setmode(_fileno(stdout), _O_BINARY)` "
                    "in the init path (Windows-only, behind `#ifdef _WIN32`), "
                    "or add `// stdout-binary-ok:` if setup lives elsewhere.",
            severity="warn",
        )


# -------------------------- rule: tilemap-tile-zero-stored --

# Tile palettes (engine/tilemap.h) typically declare tile 0 as "empty" by
# convention. The renderer in engine/tilemap.cpp short-circuits index 0
# without indexing the palette, so storing 32 zero bytes for tile 0 is
# wasted flash AND wasted blit calls. Caught the GATE_PALETTE for 32 B
# in 2026-04 (commit bd29d5f); same pattern recurs for any future tile
# palette.

_PALETTE_RE = re.compile(
    r"\bconst u8 (\w*PALETTE\w*)_data\s*\[(\d+)\]\s*PROGMEM\s*=\s*\{(.*?)\};",
    re.DOTALL,
)


def check_tilemap_tile_zero_stored(relpath: str, text: str) -> Iterable[Finding]:
    """Flag tile palettes whose first tile is all zeros.

    A tile palette where tile 0 = 32 zero bytes is paying flash for data
    the renderer should skip. The fix: drop tile 0 from the palette
    storage and have the renderer short-circuit `tile_idx == 0`. See
    GATE_PALETTE / engine/tilemap.cpp for the canonical pattern.

    The check assumes 32-byte tiles (TILE_W=16 × 2 pages). If a palette
    uses a different tile size, exempt it explicitly with `// tile-size-ok`.
    """
    if _exempt(relpath, "tilemap-tile-zero-stored"):
        return
    for m in _PALETTE_RE.finditer(text):
        size = int(m.group(2))
        if size < 64 or size % 32 != 0:
            continue  # not a tilemap palette by shape
        bytes_text = m.group(3)
        nums = re.findall(r"0x([0-9A-Fa-f]+)", bytes_text)
        if len(nums) < 32:
            continue
        first_tile = [int(x, 16) for x in nums[:32]]
        if any(b != 0 for b in first_tile):
            continue
        line = text[: m.start()].count("\n") + 1
        yield Finding(
            rule="tilemap-tile-zero-stored",
            path=relpath,
            line=line,
            message=f"{m.group(1)}_data tile 0 (first 32 B) is all zeros. "
                    f"Drop it from the palette and have the renderer skip "
                    f"layout cells with tile_idx == 0 — saves 32 B flash + "
                    f"per-cell zero-blits. See engine/tilemap.cpp for the "
                    f"`if (tile_idx == 0) continue` + `(tile_idx - 1) * "
                    f"TILE_BYTES` pattern.",
            severity="warn",
        )


# -------------------------- rule: lz77-cache-undersized --

# `LZ77_CACHE[N]` (sprites.cpp) must be ≥ the largest decoded LZ77 sprite
# that uses it. Adding a new boss whose decoded size exceeds the cache
# silently corrupts RAM — there's no runtime check and the speaker /
# framebuffer / next .bss member just gets clobbered. Static enforcement:
# decode every `boss_*_LZ77` and `logo_*_LZ77` byte stream at lint time
# and assert max(decoded_size) ≤ LZ77_CACHE_SIZE.

_CACHE_SIZE_RE = re.compile(r"constexpr\s+u16\s+LZ77_CACHE_SIZE\s*=\s*(\d+)\s*;")
_LZ77_STREAM_RE = re.compile(
    r"\bconst u8 (\w+_LZ77)\s*\[(\d+)\]\s*PROGMEM\s*=\s*\{(.*?)\};",
    re.DOTALL,
)


def check_lz77_cache_undersized(relpath: str, text: str) -> Iterable[Finding]:
    """Flag if LZ77_CACHE_SIZE < largest decoded LZ77 sprite.

    Requires decoding the streams at lint time, so we import the project's
    own decoder from scripts/_lz77_bake. If that module isn't on
    PYTHONPATH the rule no-ops with a stderr note (rather than blocking
    the lint pipeline).
    """
    if _exempt(relpath, "lz77-cache-undersized"):
        return
    if "LZ77_CACHE_SIZE" not in text:
        return
    cache_m = _CACHE_SIZE_RE.search(text)
    if not cache_m:
        return
    cache_size = int(cache_m.group(1))

    # Decode each LZ77 stream in this file. We need the project's own
    # decoder for byte-exact decoded sizes.
    try:
        import importlib.util, pathlib
        # Walk up from this file to the repo root, then load scripts/_lz77_bake.py
        here = pathlib.Path(__file__).resolve()
        for parent in here.parents:
            cand = parent / "scripts" / "_lz77_bake.py"
            if cand.exists():
                spec = importlib.util.spec_from_file_location("_lz77_bake", cand)
                mod = importlib.util.module_from_spec(spec)
                spec.loader.exec_module(mod)
                break
        else:
            return
        decode = mod.decode
    except Exception:
        return

    biggest = 0
    biggest_name = ""
    biggest_line = 0
    for m in _LZ77_STREAM_RE.finditer(text):
        nums = [int(x, 16) for x in re.findall(r"0x([0-9A-Fa-f]+)", m.group(3))]
        try:
            decoded = decode(bytes(nums))
        except Exception:
            continue
        if len(decoded) > biggest:
            biggest = len(decoded)
            biggest_name = m.group(1)
            biggest_line = text[: m.start()].count("\n") + 1
    if biggest > cache_size:
        yield Finding(
            rule="lz77-cache-undersized",
            path=relpath,
            line=biggest_line,
            message=f"LZ77_CACHE_SIZE = {cache_size} but {biggest_name} "
                    f"decodes to {biggest} bytes — would overflow the cache "
                    f"and corrupt adjacent .bss at runtime. Bump "
                    f"LZ77_CACHE_SIZE to at least {biggest}, or stack-decode "
                    f"this sprite specifically.",
            severity="error",
        )


# -------------------------- rule: file-scope-array-no-progmem --

# Adjacent to function-static-no-progmem but for FILE-scope arrays.
# avr-gcc puts non-PROGMEM `const u8 X[N] = {...}` at file scope into
# .data (RAM-resident with a flash-init copy), NOT .rodata as one might
# assume. Easy mistake: a developer writing a new lookup table
# (`const u8 BOSS_HP_BY_CIRCLE[9] = {...}`) without PROGMEM ships RAM
# they didn't mean to.

_FILE_ARRAY_RE = re.compile(
    r"^\s*(?:static\s+)?const\s+(u8|u16|u32|i8|i16|i32|char)\s+(\w+)"
    r"\s*\[(\d+)\]\s*(?:PROGMEM\s*)?=",
    re.MULTILINE,
)


def check_file_scope_array_no_progmem(relpath: str, text: str) -> Iterable[Finding]:
    """Flag file-scope `const T X[N] = {...}` without PROGMEM.

    `function-static-no-progmem` covers function scope; this covers the
    file-scope twin. Same trap, same cost: flash-init bytes + RAM
    residence. Real on AVR — `.rodata` doesn't exist as a separate
    section; non-PROGMEM const data sits in `.data`.

    Skip arrays smaller than 4 B — the linker often inlines small const
    arrays as immediates and they're not worth flagging.
    """
    if _exempt(relpath, "file-scope-array-no-progmem"):
        return
    stripped = strip_noise(text)
    for m in _FILE_ARRAY_RE.finditer(stripped):
        if "PROGMEM" in m.group(0):
            continue
        # Must be at file scope.
        prefix = stripped[: m.start()]
        depth = prefix.count("{") - prefix.count("}")
        if depth > 0:
            continue
        size = int(m.group(3))
        if size < 4:
            continue
        line = stripped[: m.start()].count("\n") + 1
        lines = text.splitlines()
        window = "\n".join(lines[max(0, line - 3):line])
        if "ram-table-ok:" in window:
            continue
        yield Finding(
            rule="file-scope-array-no-progmem",
            path=relpath,
            line=line,
            message=f"file-scope `const {m.group(1)} {m.group(2)}[{size}]` "
                    f"without PROGMEM lands in .data (RAM, with a flash "
                    f"init copy). Add PROGMEM + read via pgm_read_byte. "
                    f"Add `// ram-table-ok:` above if intentional.",
            severity="warn",
        )


# -------------------------- rule: lz77-decode-into-scratch-then-copy --

# When decoding a 1024-byte full-screen LZ77 image, the right pattern is
# `lz77::decode(X_LZ77, fb::buffer)` — the framebuffer IS the destination,
# no scratch allocation needed. The wrong pattern is `decode into local
# buffer, then memcpy/draw_full_image into fb::buffer` — wastes ~1024 B
# of stack/heap and ~1024 cycles per frame. Easy to write the slow
# version reflexively.

_DECODE_RE = re.compile(r"lz77::decode\s*\(\s*[\w:]+\s*,\s*(\w+)\s*\)")
_FULL_IMG_RE = re.compile(r"fb::draw_full_image\s*\(\s*(\w+)\s*\)")


def check_lz77_decode_into_scratch_then_copy(relpath: str, text: str) -> Iterable[Finding]:
    """Flag `lz77::decode(X, scratch)` followed by `fb::draw_full_image(scratch)`.

    Two-step pattern wastes ~1024 B of scratch RAM and copies the bytes
    twice. Replace with `lz77::decode(X, fb::buffer)` directly.
    """
    if _exempt(relpath, "lz77-decode-into-scratch-then-copy"):
        return
    stripped = strip_noise(text)
    decode_dests = {}  # name -> line number of decode call
    for m in _DECODE_RE.finditer(stripped):
        dest = m.group(1)
        if dest == "fb::buffer" or dest.endswith("::buffer"):
            continue
        decode_dests[dest] = stripped[: m.start()].count("\n") + 1
    for m in _FULL_IMG_RE.finditer(stripped):
        src = m.group(1)
        if src in decode_dests:
            line = stripped[: m.start()].count("\n") + 1
            yield Finding(
                rule="lz77-decode-into-scratch-then-copy",
                path=relpath,
                line=line,
                message=f"`fb::draw_full_image({src})` follows "
                        f"`lz77::decode(..., {src})` at line "
                        f"{decode_dests[src]}. Decode straight into "
                        f"fb::buffer to skip the ~1 KB scratch allocation "
                        f"and per-frame copy.",
                severity="warn",
            )


# -------------------------- rule: non-utf8-source --

# A source file that isn't valid UTF-8 silently breaks any cross-file
# rule (orphan-progmem-extern, etc.) because iter_sources skips
# undecodable files. Caused a real diversion in 2026-04 when an em-dash
# got written as Windows-1252 0x97 instead of UTF-8 0xE2 0x80 0x94, and
# the orphan-extern rule false-positived on every PROGMEM image.
#
# This rule is awkward — by the time strip_noise/text-search runs, the
# file has already been decoded. We re-read the file as bytes and try a
# UTF-8 decode. If it fails, emit a finding.

def check_non_utf8_source(relpath: str, text: str) -> Iterable[Finding]:
    """Flag source files that aren't valid UTF-8.

    Cross-file rules silently skip non-UTF-8 files, leading to confusing
    false positives. Catch the encoding issue at the source.
    """
    if _exempt(relpath, "non-utf8-source"):
        return
    # Re-read bytes — `text` is already decoded so we can't tell from here
    # if it had to be decoded with replacement. Loud diagnostic via the
    # same path the user would hit.
    from pathlib import Path
    from .lint import ROOT
    try:
        raw = (ROOT / relpath).read_bytes()
        raw.decode("utf-8")
    except UnicodeDecodeError as e:
        # Compute line of first bad byte.
        bad_pos = e.start
        line = raw[:bad_pos].count(b"\n") + 1
        yield Finding(
            rule="non-utf8-source",
            path=relpath,
            line=line,
            message=f"non-UTF-8 byte 0x{raw[bad_pos]:02x} at offset {bad_pos}. "
                    f"Cross-file lint rules silently skip undecodable files. "
                    f"Re-save as UTF-8. (Common cause: Windows-1252 punctuation "
                    f"like 0x97 em-dash; should be UTF-8 0xE2 0x80 0x94.)",
            severity="error",
        )
    except OSError:
        pass


# ----------------------------- rule: large-stack-buffer --

# Function-local arrays >= 96 B are stack-smash candidates on AVR. The
# stock Arduboy has 2.5 KB total RAM; once .data + .bss + framebuffer +
# entity pool land, we have ~400 B of stack at boot. A single function
# allocating a 256 B buffer leaves ~140 B for everything else nested
# under it — call frames, perf-hook locals, register spills. Cand-1
# bit us hard: a 256 B `u8 buf[LOGO_DECODE_BUF_SIZE]` in
# draw_logo_lz77_inverse silently collided with the freshly-grown .bss
# (BOSS_CACHE) and corrupted state until we routed through a shared
# .bss buffer.
#
# Static lint can't measure exact stack peak (depends on call graph,
# inlining, register pressure). What it CAN do is yell at any single
# allocation big enough to be near the danger zone and ask the author
# to prove it's safe via a `// stack-buf-ok: <reason>` comment.
#
# Threshold 96 B — chosen so stride/glyph-shaped buffers (~16 B), small
# format buffers (~32 B), short message strings (~64 B) don't trip;
# anything 96+ deserves a justification.

_STACK_BUF_RE = re.compile(
    r"\b(?:char|u8|i8|uint8_t|int8_t)\s+(\w+)\s*\[\s*(\d+|[A-Z_][A-Z0-9_]*)\s*\]\s*[;=]",
)
_STACK_BUF_LIMIT = 96


def check_large_stack_buffer(relpath: str, text: str) -> Iterable[Finding]:
    """Flag function-scope arrays >= 96 B as stack-smash candidates.

    Threshold rationale: stock Arduboy stack headroom is ~400 B at boot.
    A 96 B local + ~32 B per call frame × deep nesting (renderer, font,
    fb::draw_sprite) chews real stack fast. Anything bigger needs an
    explicit assertion that the author thought about it.

    Suppression: `// stack-buf-ok: <reason>` on or above the line. The
    reason should describe why this size is safe — e.g. "leaf function,
    no nested calls" or "shared with .bss cache when not in combat".

    Limitation: pre-symbolic-size resolution. If the size is a constexpr
    name like LOGO_DECODE_BUF_SIZE, we don't know its value here. Treat
    any non-numeric size as a candidate for review (require a comment).
    """
    if _exempt(relpath, "large-stack-buffer"):
        return
    stripped = strip_noise(text)
    for m in _STACK_BUF_RE.finditer(stripped):
        # Must be inside a function body (not a namespace / file scope).
        # Brace-depth alone is insufficient: `namespace fb { u8 buf[N]; }`
        # has depth 1 but is file-scope. Walk back from the match to the
        # enclosing `{`, then look at the token immediately preceding it:
        # functions end in `)` (signature) or `{` (lambda body); namespaces
        # end in an identifier or `::`.
        prefix = stripped[: m.start()]
        depth = prefix.count("{") - prefix.count("}")
        if depth == 0:
            continue
        # Find the most recent unmatched `{`.
        idx = len(prefix) - 1
        bal = 0
        while idx >= 0:
            ch = prefix[idx]
            if ch == "}":
                bal += 1
            elif ch == "{":
                if bal == 0:
                    break
                bal -= 1
            idx -= 1
        if idx < 0:
            continue
        # Look at non-whitespace just before the `{`.
        pre = prefix[:idx].rstrip()
        if not pre:
            continue
        last_token = pre[-1]
        # Function body: `... ) {` or `... -> ret {` (less common). Lambda
        # capture also ends with `)`. Namespace / class / extern blocks
        # end with an identifier or `::` or `extern "C"`.
        # If the char before `{` is `)`, it's a function/lambda body.
        if last_token != ")":
            continue
        size_token = m.group(2)
        if size_token.isdigit():
            size = int(size_token)
            if size < _STACK_BUF_LIMIT:
                continue
        else:
            # Symbolic size: search this file for a constexpr decl. If
            # found and small, skip. If found and big, flag with the
            # numeric value. If not found, skip — we can't tell, and
            # conservative-flagging makes the rule too noisy on small
            # well-known constants like font::STRIDE that live in headers.
            # Use the lz77-cache rule's pattern (which DOES decode streams)
            # for the canonical "is this big" check; this rule covers
            # numeric sizes only.
            const_m = re.search(
                rf"\bconstexpr\s+(?:u8|u16|u32|i8|i16|i32|int|unsigned|uint8_t|uint16_t)\s+{re.escape(size_token)}\s*=\s*(\d+)\s*;",
                stripped,
            )
            if not const_m:
                continue  # unknown size in this file — don't false-positive
            size = int(const_m.group(1))
            if size < _STACK_BUF_LIMIT:
                continue
        line = stripped[: m.start()].count("\n") + 1
        # Per-decl exemption check — within 5 lines above OR same line.
        lines = text.splitlines()
        window = "\n".join(lines[max(0, line - 5):line])
        if "stack-buf-ok:" in window:
            continue
        size_str = f"{size} B" if size > 0 else f"`{size_token}` (size unknown)"
        yield Finding(
            rule="large-stack-buffer",
            path=relpath,
            line=line,
            message=f"function-local `{m.group(1)}[{size_token}]` is "
                    f"{size_str} on the stack. Stock Arduboy has only "
                    f"~400 B stack headroom; this is a stack-smash "
                    f"candidate. Route through a shared .bss cache, or "
                    f"add `// stack-buf-ok: <reason>` above proving it's "
                    f"safe (e.g. leaf function, no nested calls).",
            severity="warn",
        )


# --------------------------------------------- rule: high-frequency-line --

# Sibling of `duplicate-block`. duplicate-block fires only on >=8-line
# windows; that misses the *narrow but numerous* pattern: a 1-3 line
# call shape repeated 5+ times. Apr 2026 session caught these by hand:
#
#   font::draw_text_pgm(2, fb::HEIGHT - 7, LIT_X)  — 6 sites (footer slot)
#   fb::fill_rect(0, 8, fb::WIDTH, 1)              — 8 sites (header rule)
#   for (u8 i = 0; i < storage::NAME_LEN; ++i)
#     name_tmp[i] = meta.name[i];                  — 7 sites (Pilgrim name copy)
#
# Each site is short enough that LTO often de-dups, but not always —
# leftover 50-150 B is typical. The right fix is always "extract a
# named helper" (see draw_footer_pgm, draw_pilgrim_name_header).
#
# Threshold: 5+ occurrences of the same normalized non-trivial line.
# Below 5, false-positive rate is too high (closing braces, common
# loop bodies, etc.). At 5+ the discipline signal is real.

_HIGH_FREQ_THRESHOLD = 5

# A "load-bearing" line for the purposes of this rule is one that has:
#   1. a namespace-qualified call (font::draw_text_pgm) OR a draw_*
#      helper call OR a meta.* access, AND
#   2. at least one numeric literal in the call arguments — the
#      tell-tale sign of "magic positional layout code" that should
#      be extracted into a named helper.
# `fb::clear()` (no args) and `audio::play(&SFX_X)` (no numerics) are
# correctly excluded — they're tier-1 ops with nothing to factor.
# `fb::fill_rect(0, 8, fb::WIDTH, 1)` (positional layout numbers) is
# correctly included — the magic numbers are the helper-shaped part.
_HIGH_FREQ_LOAD_BEARING_RE = re.compile(
    r"(?:"
    r"\b(?:fb|font|sprites|storage|data_flash)::\w+\s*\("
    r"|\bdraw_\w+\s*\("
    r"|\bcopy_pgm_\w+\s*\("
    r")"
    # ...followed (eventually) by a numeric literal arg.
    r".*\b(?:\d+|0x[0-9A-Fa-f]+)\b",
)


def check_high_frequency_line(relpath: str, text: str) -> Iterable[Finding]:
    """Flag load-bearing source lines that appear 5+ times across the tree.

    Catches the "narrow but numerous" duplication pattern that
    duplicate-block (8+ line window) misses: a single load-bearing
    call repeated at many sites. Each instance leaves bytes on the
    table that LTO can't fully share — extract a helper, save flash.

    "Load-bearing" means the line contains a namespace-qualified call
    (`font::draw_text_pgm(...)`), a `draw_*` helper call, a `meta.*`
    field access, or another pattern that COULD be wrapped in a
    helper. Pure language structure (`for (...)`, `#include`,
    `return X;`) is excluded — those are syntax, not duplication.

    The rule scans every source file once, builds a normalized-line →
    locations map across the whole tree, and yields one finding per
    line per file when the line appears >= _HIGH_FREQ_THRESHOLD times
    GLOBALLY.

    Suppress per-line with `// hifreq-ok: <reason>` on the same line
    or within 2 lines above. Use it for genuinely-unhelperable
    repetition (e.g. an existing helper definition contains the
    pattern by necessity).
    """
    if _exempt(relpath, "high-frequency-line"):
        return

    # Templated normalization: replace identifiers that look like
    # constants (UPPER_SNAKE_CASE — the LIT_* / SFX_* / SP_* style of
    # this codebase) with a placeholder. This way the 6 footer call
    # sites that vary only by their LIT argument all hash to the
    # same template and the rule fires on the shared call shape.
    def _template(line: str) -> str:
        return re.sub(r"\b[A-Z][A-Z0-9_]{2,}\b", "ID", line)

    # Lazy-build the global line-template frequency table once.
    cache = getattr(check_high_frequency_line, "_cache", None)
    if cache is None:
        from .lint import iter_sources
        counts: dict[str, list[tuple[str, int]]] = {}
        for src_relpath, src_text in iter_sources():
            for i, raw in enumerate(src_text.splitlines()):
                norm = _norm_line(raw)
                if not norm or len(norm) <= 8:
                    continue
                if not _HIGH_FREQ_LOAD_BEARING_RE.search(norm):
                    continue
                tpl = _template(norm)
                counts.setdefault(tpl, []).append((src_relpath, i + 1, norm))
        cache = counts
        check_high_frequency_line._cache = cache  # type: ignore[attr-defined]

    lines = text.splitlines()
    seen_lines: set[str] = set()
    for i, raw in enumerate(lines):
        norm = _norm_line(raw)
        if not norm or len(norm) <= 8:
            continue
        if not _HIGH_FREQ_LOAD_BEARING_RE.search(norm):
            continue
        tpl = _template(norm)
        locs = cache.get(tpl, [])
        if len(locs) < _HIGH_FREQ_THRESHOLD:
            continue
        if tpl in seen_lines:
            continue
        seen_lines.add(tpl)
        # Per-line justification.
        window_just = "\n".join(lines[max(0, i - 2):i + 1])
        if "hifreq-ok:" in window_just:
            continue
        others = [(p, ln) for (p, ln, _) in locs if p != relpath]
        other_str = "; ".join(f"{p}:{ln}" for (p, ln) in others[:3])
        if len(others) > 3:
            other_str += f" (+{len(others) - 3} more)"
        elif not others:
            other_str = f"{len(locs)} sites in this file"
        msg = (f"{len(locs)}x repetition of `{tpl[:60]}`. "
               f"Consider extracting a helper. Other sites: {other_str}. "
               f"Add `// hifreq-ok: <reason>` if intentional.")
        yield Finding(
            rule="high-frequency-line",
            path=relpath,
            line=i + 1,
            message=msg,
            severity="warn",
        )


# --------------------------------------------- rule: self-recursion --

# Function body that calls itself. On AVR with no stack overflow detection,
# infinite recursion silently overflows the 256 B-ish stack, corrupts
# adjacent .bss / globals, and the CPU eventually jumps to garbage and
# freezes (or holds the last audio sample as a single-tone drone).
#
# Real incident (Apr 2026 session): a sed rewrite of header-rule call
# sites accidentally matched the rewrite's *own* body line, producing
# `inline void draw_header_rule() { draw_header_rule(); }`. The build
# succeeded; flash dropped 5 KB (LTO marked everything reachable from
# the recursion as dead); title-A press freeze with a stuck audio tone.
# Cost ~30 minutes of bisecting before I looked at the helper body.
#
# This rule is intentionally narrow: only catches a function whose body
# (between its opening `{` and closing `}`) contains a call to itself
# AND has no terminating condition (no early `return`, no `if (...)`
# wrapping the call). Mutual recursion (foo→bar→foo) is out of scope —
# parsing call graphs requires real C++ analysis. Tail-recursive helpers
# with a guard ARE a real pattern (see strlen_) and we don't want to flag
# those — hence the "no early-exit branch above the call" filter.

# Match the start of a function definition: a line beginning with
# (optionally inline/static/whatever) `void NAME(...) {` OR `T NAME(...) {`.
_FUNC_DEF_RE = re.compile(
    r"^(?:inline\s+|static\s+|constexpr\s+)*"
    r"\b(?:void|u8|u16|u32|i8|i16|i32|bool|char|float|const\s+\w+\s*\*?)\s+"
    r"(\w+)\s*\([^)]*\)\s*\{",
    re.MULTILINE,
)


def check_self_recursion(relpath: str, text: str) -> Iterable[Finding]:
    """Flag function bodies that call themselves with no early exit.

    AVR has no stack-overflow detection. An accidental self-recursion
    (e.g. from a sed rewrite that matched its own definition) corrupts
    the stack silently and freezes the device. This rule scans every
    function definition for unconditional self-calls in the body.

    Heuristic: a self-call is "unconditional" if there's no `if (...)`,
    `for (...)`, `while (...)`, or `return ...;` line between the
    function's opening brace and the recursive call. That filters out
    real recursive helpers (which always have a base case).

    Suppress with `// recursion-ok: <reason>` on the same line as the
    self-call or within 2 lines above.
    """
    if _exempt(relpath, "self-recursion"):
        return
    lines = text.splitlines()
    for m in _FUNC_DEF_RE.finditer(text):
        fn_name = m.group(1)
        # Find the line number where the function starts.
        start_line = text[:m.start()].count("\n")
        # Walk forward, tracking brace depth, looking for self-calls
        # before any terminating control flow.
        depth = 0
        in_body = False
        body_ended = False
        saw_terminator = False
        for ln in range(start_line, len(lines)):
            if body_ended:
                break
            line = lines[ln]
            stripped = re.sub(r"//.*$|/\*.*?\*/", "", line)
            for ch in stripped:
                if ch == "{":
                    depth += 1
                    in_body = True
                elif ch == "}":
                    depth -= 1
                    if depth == 0 and in_body:
                        body_ended = True
                        break
            if body_ended or not in_body:
                continue
            # Has a base-case branch? `if`/`for`/`while`/`return` at
            # statement start counts.
            if re.match(r"^\s*(if|for|while|return\b)", stripped):
                saw_terminator = True
            # Self-call check: token-bounded match of fn_name followed by `(`.
            call_re = re.compile(rf"\b{re.escape(fn_name)}\s*\(")
            if call_re.search(stripped) and ln != start_line:
                # Skip if the line is the function's own definition line
                # (already handled by ln != start_line guard above).
                # Per-line justification.
                window_just = "\n".join(lines[max(0, ln - 2):ln + 1])
                if "recursion-ok:" in window_just:
                    continue
                if not saw_terminator:
                    yield Finding(
                        rule="self-recursion",
                        path=relpath,
                        line=ln + 1,
                        message=(
                            f"function `{fn_name}` calls itself with no "
                            f"early-exit branch above the call. On AVR this "
                            f"silently overflows the stack. If intentional, "
                            f"add `// recursion-ok: <reason>` and a base case "
                            f"BEFORE this line."),
                        severity="error",
                    )


# --------------------------------------- rule: scene-root needs SCENE_ROOT --
#
# Scene-root draw/update functions — the ones dispatched directly from
# `case STATE_X:` in `game::draw()` and `game::update()` — MUST be marked
# `SCENE_ROOT` (which expands to `__attribute__((noinline))`).
#
# Without it, `-flto -Os` happily inlines them into `main()`. We learned
# this the hard way running `tools/scene_audit/`: a 28 KB ROM had a
# 10,052 B `main` and a 5,244 B `game::update`, with EVERY screen's draw
# code mashed inside those two symbols. Per-scene sizing was unmeasurable.
# The audit declared "86% of code is CORE" — false; the code was hidden,
# not actually shared.
#
# After tagging 21 scene roots SCENE_ROOT, CORE dropped from 86.2% to
# 59.8%, and 7.5 KB of pageable code became visible to the audit. The
# noinline cost was +414 B flash (one prologue/epilogue per de-inlined
# function), which is a tiny price for getting accurate measurement.
#
# The rule: any function whose name matches a scene-root pattern must
# have `SCENE_ROOT` on its definition line. The pattern list is the
# union of MENUS / CUTSCENES / PLAYING dispatch targets, mirrored from
# `tools/scene_audit/__main__.py`'s SCENE_RULES (kept in sync by hand —
# both files are tiny and rarely change).

_SCENE_ROOT_NAMES = frozenset({
    # PLAYING update roots — dispatched per-frame from game::update()
    "update_player",
    "update_enemy",
    "update_bullet",
    "update_pickup",
    # MENUS draw roots — dispatched from game::draw() and the post-world
    # overlay (draw_pause_menu).
    "draw_main_menu",
    "draw_name_entry",
    "draw_pause_menu",
    "draw_upgrade_menu",
    "draw_stats_screen",
    "draw_shades",
    "draw_numerals",
    "draw_lexicon",
    "draw_text_screen",
    "draw_tutorial",
    "draw_guide",
    # CUTSCENES draw roots
    "draw_title",
    "draw_gate_card",
    "draw_circle_card",
    "draw_second_death",
})

# Match a function definition (NOT a forward decl) that starts with
# `void <NAME>(...)` followed by `{` (after the param list and optional
# attributes). Forward decls end in `;` and are excluded.
# group(1) is the prefix (may be empty); group(2) is the function name.
_SCENE_ROOT_DEF_RE = re.compile(
    r"^([\w \t]*?)"                     # prefix qualifiers (possibly empty)
    r"\bvoid\s+(\w+)\s*"                # `void NAME`
    r"\([^)]*\)\s*"                     # `(args)`
    r"(?:[\w :()]*)\{",                 # optional trailing attrs, then `{`
    re.MULTILINE,
)


def check_scene_root_without_attribute(relpath: str, text: str) -> Iterable[Finding]:
    """Flag scene-root draw/update functions defined without `SCENE_ROOT`.

    Scene roots are the functions dispatched directly from `case STATE_X:`
    in `game::draw()` / `game::update()`. They MUST carry `SCENE_ROOT`
    (= `__attribute__((noinline))`) so LTO doesn't fold them into `main`
    and silently destroy scene_audit's per-scene measurements.

    See `tools/scene_audit/` for why this matters: a single missing
    SCENE_ROOT can move thousands of bytes from MENUS / CUTSCENES / PLAYING
    into the unmeasurable CORE bucket, and the audit will report the
    paging-viable codebase as "86% always-resident" (i.e. doomed) when
    it's actually paging-viable. The reverse — false negative — is
    invisible until you try to do scene-paging and the architecture
    can't pin down what each bank holds.

    Suppress with `// scene-root-ok: <reason>` on the same line or within
    2 lines above the definition (e.g. for a small leaf where inlining
    is preferable to call overhead and the function isn't a real scene
    boundary).
    """
    if _exempt(relpath, "scene-root-without-attribute"):
        return
    lines = text.splitlines()
    clean = strip_noise(text)
    for m in _SCENE_ROOT_DEF_RE.finditer(clean):
        name = m.group(2)
        if name not in _SCENE_ROOT_NAMES:
            continue
        prefix = m.group(1)
        # Already tagged?
        # SCENE_ROOT (legacy bare-noinline) and SCENE_ENTRY(BUCKET)
        # (noinline + section) both satisfy the rule. SCENE_FN(BUCKET)
        # is section-only (lets GCC inline if profitable) and does NOT
        # satisfy this rule — entry points called via volatile fnptr
        # must be noinline so scene_audit can measure the per-scene
        # call graph.
        if "SCENE_ROOT" in prefix or "SCENE_ENTRY(" in prefix or "noinline" in prefix:
            continue
        line_no = clean[:m.start()].count("\n") + 1
        # Per-line justification (same line or up to 2 lines above).
        window = "\n".join(lines[max(0, line_no - 3):line_no])
        if "scene-root-ok:" in window:
            continue
        yield Finding(
            rule="scene-root-without-attribute",
            path=relpath,
            line=line_no,
            message=(
                f"function `{name}` is a scene root (dispatched from "
                f"game::draw() / game::update()) but is defined without "
                f"`SCENE_ROOT`. Without it, LTO inlines it into main() "
                f"and tools/scene_audit/ can't measure its per-scene "
                f"size. Add `SCENE_ROOT` before the return type. If "
                f"intentional (small leaf, not really a scene boundary), "
                f"add `// scene-root-ok: <reason>` above the definition."),
            severity="error",
        )


# -------------------------------------------------------------- registry --

# Each entry: (rule_name, function, short_doc)
ALL_RULES = [
    ("unjustified-32bit-cast",      check_unjustified_32bit_cast,      "(i32)/(u32) casts in function bodies without `// 32bit-ok:` justify."),
    ("function-static-no-progmem",  check_function_static_no_progmem,  "Function-scope `static const T[]` without PROGMEM (lands in .data)."),
    ("hot-blood-river-trap",        check_hot_blood_river_trap,        "char buf[<40] + copy_pgm_table_entry — stack-smash setup."),
    ("extern-progmem-mismatch",     check_extern_progmem_mismatch,     "extern const T arr[] without PROGMEM (silent boot hang)."),
    ("duplicate-string-walk",       check_duplicate_string_walk,       "Inline `while (s[n])` strlen/strcpy walks — use strlen_/strcpy_."),
    ("orphan-progmem-extern",       check_orphan_progmem_extern,       "PROGMEM extern decls with zero callers — dead source clutter."),
    ("decode-in-always-dirty-draw", check_decode_in_always_dirty_draw, "Heavy per-frame work (lz77 decode, etc.) in always-dirty draw functions without a dirty-cache guard."),
    ("cursor-wrap-without-helper",  check_cursor_wrap_without_helper,  "Open-coded `cursor=(cursor+1)%N` — use menu_cursor_step helper."),
    ("duplicate-block",             check_duplicate_block,             "8+ line block duplicated elsewhere in the source — candidate for shared helper."),
    ("u32-divide-not-justified",    check_u32_divide_not_justified,    "u32/i32 divide without `// 32bit-ok:` justification (pulls libgcc helper)."),
    ("function-local-string-literal", check_function_local_string_literal, "function-scope `const char* x=\"...\"` lands in .data — move to PROGMEM."),
    ("file-scope-struct-no-progmem",   check_file_scope_struct_no_progmem,  "file-scope `const/constexpr <Struct> X = {...}` lands in .data — move to PROGMEM."),
    ("stdout-binary-without-setmode",  check_stdout_binary_without_setmode, "Raw stdout write without `_setmode(_O_BINARY)` — corrupts binary streams on Windows."),
    ("tilemap-tile-zero-stored",       check_tilemap_tile_zero_stored,      "Tile palette stores all-zero tile 0 — drop it; renderer skips index 0."),
    ("lz77-cache-undersized",          check_lz77_cache_undersized,         "LZ77_CACHE_SIZE smaller than the largest decoded LZ77 sprite — silent RAM corruption at runtime."),
    ("file-scope-array-no-progmem",    check_file_scope_array_no_progmem,   "file-scope `const T X[N] = {...}` without PROGMEM lands in .data on AVR."),
    ("lz77-decode-into-scratch-then-copy", check_lz77_decode_into_scratch_then_copy, "Decode-then-copy wastes ~1 KB scratch — decode straight into fb::buffer."),
    ("non-utf8-source",                check_non_utf8_source,               "Source file isn't valid UTF-8 — cross-file rules silently skip it."),
    ("large-stack-buffer",             check_large_stack_buffer,            "Function-local array >= 96 B — stack-smash candidate; route through .bss or justify."),
    ("high-frequency-line",            check_high_frequency_line,           "Non-trivial source line repeated 5+ times across the tree — candidate for shared helper."),
    ("self-recursion",                 check_self_recursion,                "Function calls itself with no early-exit branch — silent stack overflow on AVR."),
    ("scene-root-without-attribute",   check_scene_root_without_attribute,  "Scene-root draw/update function defined without `SCENE_ROOT` — LTO inlines it into main() and breaks scene_audit measurement."),
]
