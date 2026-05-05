# Pre-commit pipeline

Until proper CI/CD is wired up, this is the manual checklist to run
before every commit. Each entry tells you the command, what it catches,
and what to do when it fails.

> **TL;DR**: `make audit-all && make format` before every commit.
> If both pass, commit. The full reasoning for each step is below.

## The checklist

| Step | Command | What it catches | Fail action |
|---|---|---|---|
| 1 | `make format` | clang-format drift | Re-stage the now-formatted files |
| 2 | `make` | Compile errors, RAM > 85%, flash > 100% | Fix the underlying budget overrun |
| 3 | `make efficiency-lint` | 19 AVR-specific traps (32-bit math, .data leaks, stack-smash candidates, …) | Add a justify comment OR fix the cause |
| 4 | `make sdl` | SDL build still compiles | Fix the cross-platform regression |
| 5 | `make sdl-perf-check` | Any frame > 266,667 sim-cycles (would drop on Arduboy) | Profile and trim hot paths |
| 6 | `make audit-all` | Steps 2–5 chained in one command — the canonical pre-commit gate | Use individual targets to find the failing check |
| 7 (occasional) | `make stack-report` | Worst-case stack peak from main vs RAM headroom | Reduce per-function stack frames or route through .bss |

In practice, **`make audit-all` covers steps 2–5 in one invocation.**
Pair it with `make format` and you're done. Run `make stack-report`
after any change that adds nontrivial render code or grows .bss — it
takes ~30 s (full no-LTO rebuild) so it's not in the default gate.

## What each step actually does

### `make format`

Runs `clang-format -i` over every `.cpp/.h/.hpp/.c` under
`engine/`, `platform/`, `games/`. Modifies files in place.

If it changes anything, **re-stage those files** and re-run the commit.
A separate `make format-check` does a dry-run and exits non-zero if
anything would change — useful in scripts.

The pre-commit hook (`scripts/pre-commit.sh`, installed via
`make setup-hooks`) enforces this automatically: refuses to commit if
any staged file is unformatted.

### `make` (default target)

1. Compiles the Arduboy release build (`-Os -flto -fno-jump-tables
   -mcall-prologues`).
2. Runs `flash-gate`: refuses if program > 28,672 B (the Caterina
   bootloader-reserved ceiling — past 100 % bricks USB-flash on real
   hardware). Override with `make FLASH_CEILING_PCT=N` only when shipping
   intentionally over.
3. Runs `ram-gate`: refuses if `.data + .bss > 85 %` of 2,560 B.
   Past 85 % the AVR stack starts colliding with .bss. Override with
   `make RAM_CEILING_PCT=N`, but only after measuring stack peaks.

### `make efficiency-lint`

19 rules enforcing AVR-specific discipline (`tools/efficiency_lint/`).
Highlights:

- `unjustified-32bit-cast` — every `(i32)/(u32)` cast pulls libgcc helpers
  (~70 B each); justify with `// 32bit-ok:`.
- `function-static-no-progmem` / `file-scope-array-no-progmem` — file-
  or function-scope const arrays without `PROGMEM` land in `.data` (RAM)
  on AVR.
- `hot-blood-river-trap` — `char buf[N<40] + copy_pgm_table_entry`
  pattern; classic stack-smash setup.
- `large-stack-buffer` — function-scope `char/u8 buf[N >= 96]`; route
  through `.bss` cache or justify with `// stack-buf-ok:`.
- `lz77-cache-undersized` — runs the project's LZ77 decoder at lint time;
  refuses if any boss/logo's decoded size > `LZ77_CACHE_SIZE`.
- `non-utf8-source` — non-UTF-8 source files silently break cross-file
  rules; flagged loudly.
- `stdout-binary-without-setmode` — Windows binary-stream pipes need
  `_setmode(_O_BINARY)` or 0x0A bytes get CRLF-translated and corrupt
  the stream.
- `decode-in-always-dirty-draw` — heavy LZ77 decode in always-dirty
  draws is a 60×/sec CPU burn unless cached.
- `tilemap-tile-zero-stored` — tile palette wasting 32 B on an all-zero
  tile-0; renderer should skip index 0 instead.
- `lz77-decode-into-scratch-then-copy` — decode + memcpy is wasteful;
  decode straight into `fb::buffer`.
- `cursor-wrap-without-helper` — open-coded `cursor=(c+1)%N` should use
  `menu_cursor_step`.
- `duplicate-string-walk` — inline `while (s[n])` walks should use the
  shared `strlen_/strcpy_`.
- `duplicate-block` — 8+ line block duplicated elsewhere; extract or
  add `// dup-ok: <reason>`.
- `extern-progmem-mismatch`, `orphan-progmem-extern`,
  `function-local-string-literal`, `file-scope-struct-no-progmem`,
  `u32-divide-not-justified` — see source for each rule's docstring.

Most rules support a per-line suppression comment (e.g. `// 32bit-ok:`,
`// stack-buf-ok:`, `// ram-table-ok:`, `// dup-ok:`).

### `make sdl-perf-check` (and the perf instrumentation it relies on)

Cross-platform safety net. Flow:

1. Builds `build-sdl-perf/` with `PERF_UART_LOG=ON`. Engine hot paths
   (`fb::clear`, `fb::draw_sprite`, `lz77::decode`, etc.) call
   `perf_hook::charge(op, units)`, which on SDL forwards to a
   sim-cycle counter calibrated to AVR cycle costs.
2. Runs the perf-instrumented SDL exe for ~10 s on TITLE.
3. Asserts every frame's sim-cycles < 266,667 (= 16 MHz / 60 Hz).

A frame over budget on SDL would drop a frame on real Arduboy. This
catches "looks fine in SDL, drops frames in Ardens" regressions —
without it, work like the cand-1 boss-LZ77 regression slipped in
silently because PC always hits 60 FPS regardless.

The perf hook is a **no-op on Arduboy** (the linker GC strips it
entirely) — zero shipping cost.

### `make audit-all`

The umbrella target. Chains:

```
make all efficiency-lint sdl-perf-check
```

Ends with a final summary line confirming all targets green and the
flash + SDL binary sizes. **This is the one to run before every commit.**

## What this pipeline does NOT cover

- **Real-hardware bugs**: USB timing, EEPROM wear, RF, battery droop.
  Flash an actual Arduboy occasionally before any release. Ardens is
  cycle-accurate but doesn't model every quirk.
- **Visual regressions**: nothing in the pipeline checks pixel output.
  After UI changes, eyeball the affected screen in Ardens before
  committing.
- **Stack peak measurement isn't in the gate by default.** Run
  `make stack-report` periodically — it builds a parallel no-LTO
  variant with `-fstack-usage`, walks the call graph, and reports the
  worst-case stack peak from `main`. Slow (full rebuild) so it's not
  in `audit-all`. Run after any change that adds a function with a
  big stack frame (sprite blits, text rendering, etc.). The
  `large-stack-buffer` lint rule catches individual big buffers
  cheaply; `make stack-report` catches the cumulative depth bug that
  cand-1 hit (BOSS_CACHE shrinking the stack until a 256 B buffer
  collided with .bss).
- **Audio recordings**: SFX timing differences vs Ardens reality
  aren't caught by the perf gate. Use
  `build-sdl/bin/mono-sdl.exe --record-audio=test.wav` and listen.

## Quick recovery: most common failures

- **"Not clang-format clean"** → `make format`, `git add -A`, retry commit.
- **"FLASH gate failed"** → over 28,672 B. Find the recent grower
  (`git diff` against last commit, `avr-nm --size-sort` to spot the
  newly fat symbol). Check `docs/footprint-audit.md` for compression
  candidates.
- **"RAM gate failed"** → over 85 %. Most common cause is a literal
  string passed to `font::draw_text` (PROGMEM forgotten) — see the
  cand-3 work and `LIT_*` table in `games/rpg/game.cpp` for the fix
  shape. Run `avr-objdump -s -j .data build/.../rpg.elf | head -30`
  to see what's leaking.
- **"sdl-perf-check OVER budget"** → a hot path got more expensive.
  Find the perf-trace via `python -m tools.perf_logger trace_perfcheck.bin
  --top 10`, identify the hot frame, profile.
- **"efficiency-lint findings"** → fix or add a per-line suppression
  comment with reason.

## When CI/CD lands

This doc becomes the spec for the GitHub Action. Each step maps 1:1 to
a CI check. Until then: `make audit-all && make format` before every
commit. The pre-commit hook enforces the format part automatically.
