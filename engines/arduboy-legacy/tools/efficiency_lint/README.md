# efficiency_lint — 1-bit Arduboy code-discipline linter

Custom static analyzer that catches the project-specific footprint and
correctness traps documented in `.claude/CLAUDE.md`. Runs over `engine/`
and `games/` source.

Generic linters (`clang-tidy`, `-Wconversion`) don't catch these — they're
AVR / PROGMEM / 1-bit-discipline specific.

## What it catches today

| Rule | Severity | Cost when triggered | Why it's nasty |
|---|---|---|---|
| **`unjustified-32bit-cast`** | error | ~70 B per libgcc helper pulled in | A stray `(i32)` cast quietly imports `__udivmodsi4` etc. and binary grows by hundreds of bytes. Empirically caused 258 B of growth in this codebase before manual cleanup. |
| **`function-static-no-progmem`** | error | RAM bytes equal to table size | Function-scope `static const T[] = {...}` lands in `.data` (RAM) instead of flash. On 2.5 KB of RAM that's load-bearing. |
| **`hot-blood-river-trap`** | error | 1+ byte stack smash, silent until distant code reads corrupted state | `char buf[<40]` paired with `copy_pgm_table_entry()` (which writes up to TEXT_LINE_MAX=40 B). Caused a real cross-screen black-screen bug — see CLAUDE.md "PROGMEM stack-buffer trap". |
| **`extern-progmem-mismatch`** | error | Boot hang or garbage reads | `extern const T arr[];` decl without PROGMEM compiles AND links cleanly but emits RAM-load instructions for what's actually flash data. Hangs the CPU at first dereference. |
| **`duplicate-string-walk`** | warn | ~10-20 B per duplicate | Inline `while (s[n])` strlen/strcpy patterns proliferate; codebase already has `strlen_` / `strcpy_`. Caught 6 sites worth ~150 B before manual sweep. |
| **`orphan-progmem-extern`** | warn | Source clutter, ~150 B compile time, no flash impact (LTO eats it) | PROGMEM extern decls with zero referencing callers. Found 2KB of dead `TITLE_NATIVE_data` and `WOOD_data` carrying around in source by hand audit. |
| **`high-frequency-line`** | warn | Variable — sometimes single helper saves 5 KB by changing LTO inlining decisions | Load-bearing source line (namespace-qualified call with positional layout numbers) repeated 5+ times after templating away constants. The 8-line `duplicate-block` rule misses these 1-3 line "narrow but numerous" patterns. Apr 2026 caught `fb::fill_rect(0, 8, fb::WIDTH, 1)` (8 sites, header rule); extracting `draw_header_rule()` recovered **5,398 B internal flash** because LTO subsequently moved `update()` and `draw_engraved_portrait` out-of-line. Sometimes a small helper is the leverage point. |
| **`scene-root-without-attribute`** | error | Thousands of bytes of pageable code disappear into an unmeasurable monolithic `main` | Scene-root draw/update functions (the ones dispatched from `case STATE_X:` in `game::draw()` / `game::update()`) MUST carry the `SCENE_ROOT` macro (= `__attribute__((noinline))`). Without it, `-flto -Os` folds them into `main()` and `tools/scene_audit/` can't measure per-scene size. Apr 2026: tagged 21 scene roots SCENE_ROOT and 7.5 KB of pageable code surfaced — `main` shrank from 10,052 B to 8,274 B. The audit went from "86% always-resident, paging doomed" (false signal — code was hidden, not shared) to "60% always-resident, 11 KB pageable, viable." |

## Running

```sh
python -m efficiency_lint               # check all rules, fail on findings
python -m efficiency_lint --list        # list rules + descriptions
python -m efficiency_lint --rules a,b   # only run named rules
python -m efficiency_lint --warn-only   # report but don't fail (informational)
```

For the runtime to find the package, run from the repo root or set
`PYTHONPATH=tools`. The Makefile `audit` target wires this up.

## Adding a new rule

The whole point of this tool is to **codify discipline** — every time we
hit a footprint or correctness trap by hand, we should encode the pattern
here so future-us catches it automatically.

1. Open `rules.py`
2. Write a function `def check_FOO(relpath, text) -> Iterable[Finding]:`
3. The docstring is load-bearing — explain the trap, the cost, and the
   fix. Future maintainers (including future Claude) read these to
   understand WHY the rule exists.
4. Add `(name, check_FOO, short_doc)` to `ALL_RULES` at the bottom.
5. Add a test in `tests/test_rules.py` with both a hit case and a clean
   case so regressions can't slip in.

If a rule needs to skip specific files (e.g. `engine/fixed.h` is allowed
to use `(i32)` casts as its core idiom), add the file → rule-name mapping
to `_RULE_EXEMPT_FILES` in `rules.py`.

## Justifying intentional traps

Some patterns this linter flags are *correct* in context — the fixed-point
math header genuinely needs `(i32)` widening for its 8.8 multiply chain,
for example. Two ways to silence the linter for legitimate cases:

1. **File-level exemption** — add to `_RULE_EXEMPT_FILES` in `rules.py`.
   Use this for headers/files where the pattern is structural.
2. **Per-call comment** — add `// 32bit-ok: <reason>` on the same line OR
   within 3 lines above. Use this for one-off legitimate uses inside
   otherwise-clean code.

Both keep the linter green while making the intent explicit in code.

## Cost

Pure Python, runs over a few thousand lines of source. **<1 second.**
Zero runtime / build-time impact on the game itself.

## Files

- `__init__.py` — package marker
- `__main__.py` — CLI entry (`python -m efficiency_lint`)
- `lint.py` — shared scan infrastructure (file walker, Finding type)
- `rules.py` — rule implementations + registry
- `tests/test_rules.py` — per-rule hit/clean cases (TODO — not yet written)

## Why this is in tools/, not scripts/

`scripts/` is for one-off maintenance scripts (build helpers, asset
converters). `tools/` is for first-class developer tooling that the
project depends on continuously — same tier as `tools/spritebake/` and
`tools/crash_boundary/`. The efficiency linter belongs here because it's
the codified version of CLAUDE.md's discipline rules.
