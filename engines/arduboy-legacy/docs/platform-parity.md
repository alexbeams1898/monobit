# Platform parity — Arduboy is the source of truth

**Doctrine:** Every platform must reproduce Arduboy behavior identically.
Only the *mechanism* may differ. Never the player-facing result.

This is the single most important rule for cross-platform work in this
codebase. A platform that "approximates" Arduboy behavior, optimizes
away constraints, or stubs out features is broken — even if the binary
runs.

## Why this rule exists

Three reasons it has to be airtight:

1. **The Arduboy is the constrained target.** Designing against it
   forces every decision (animation length, audio cadence, scene
   transitions) to fit the small machine. If SDL silently runs faster
   or smoother, devs iterate against a fiction; the Arduboy player
   gets a different game than the PC tester saw.

2. **Future platforms (iOS, web) need a single rulebook.** "Match the
   Arduboy" is a one-line spec any new platform implementer can
   verify. "Match SDL, which approximates Arduboy" is a recipe for
   drift.

3. **Bugs caused by divergence are nightmare-class.** When SDL works
   and Arduboy doesn't (or vice versa), the dev loses days finding
   *which* platform is wrong. Better to have one bug to fix on the
   shared code path than two builds with different behaviors.

## What this means in practice

- **Cover/uncover transitions, idle animations, screen wipes, audio
  ducking, frame timing — all of it must match across platforms.**
- **Adding a feature on Arduboy is a one-fer-all commitment:** SDL
  gets it the same patch, not "later as a follow-up."
- **A platform implementation that is "empty stub for now" is a bug**,
  not a placeholder. Either implement properly or hold the feature
  back on Arduboy too.
- **SDL/PC is not a "dev convenience" build** with skipped animations
  and fast transitions. It's a faithful preview. Devs who want fast
  iteration use a build flag (`--fast-transitions`), not a different
  platform.
- **`#ifdef PLATFORM_X` around player-perceptible logic is a code
  smell.** Player-facing logic lives in shared code. Platform-
  conditionals only gate hardware-mechanism details.

## When divergence IS legitimate

Only when a platform genuinely cannot produce the matching behavior.
Cases that have come up so far:

| What | Arduboy mechanism | SDL mechanism | Why divergence is OK |
|---|---|---|---|
| Audio output | Timer1 PWM ISR → speaker pin | SDL_Audio callback → host audio | Chip has no DAC; SDL has no PWM peripheral. Audible result identical. |
| Input | PINF register reads | SDL_KEYDOWN/UP events | Different input hardware. Button-down/up model identical. |
| Save storage | EEPROM + FX SPI flash | Files in working dir | PCs have no memory-mapped EEPROM. Persistence semantics (durable, atomic-on-power-loss) identical. |
| Scene paging | SPM trampoline + bank rewrite | (no paging — banks all resident) | Chip has 32 KB; PC has all the RAM. Player-visible cover/uncover/timing identical. |

The pattern: the *implementation* differs because the platforms have
different hardware. The *result the player perceives* doesn't differ.

## Where parity-critical code lives

| Layer | Goes in | Examples |
|---|---|---|
| Engine substrate | `engine/` | Audio sequencer, framebuffer, entity pool, fixed-point math. Platform-agnostic by rule (`engine/` has zero platform includes). |
| Game logic | `games/<game>/` | RPG state machine, wood menu, vestigia screen, **transition content** (which sigil, how many frames, what animation). |
| Hardware mechanism | `platform/<name>/` | Display driver, input driver, audio output, SPI / file backends, scene-paging trampoline. |

**Anti-pattern:** A 250-line "scene_paging.cpp" in `platform/arduboy/`
that holds both the SPM trampoline (legitimately platform-specific) AND
the cover/uncover *visual content* (NOT platform-specific). The visual
content silently has no SDL counterpart, and the gap goes unnoticed
until a user reports "the animation doesn't play on PC."

**Right pattern:** Visual transitions live in shared code. The
platform-specific file is thin — just the SPM call + a callback that
invokes the shared cover/uncover routines around it.

## Verification protocol

Two layers — one mechanical, one visual. Both required for any change
that touches transition timing, animation frame counts, audio behavior,
or scene flow.

### Mechanical (enforced by pre-commit hook)

```sh
make verify-parity
```

Builds BOTH Arduboy and SDL targets. Fails the commit if either build
breaks. Runs in <30 s on a warm tree.

This is wired into `scripts/pre-commit.sh` and fires automatically on
any commit that touches `engine/`, `games/`, `platform/`, the Makefile,
or CMakeLists.txt. Pure-docs / pure-tools changes skip it.

The gate catches the silent-rot case: a commit that compiles for
Arduboy but breaks SDL (or vice versa) because someone added a new
function to one platform's directory without the matching shared
implementation. It does **not** prove behavior matches — both builds
can compile while doing different things.

### Visual (still on the dev)

```sh
make ardens       # Arduboy emulator
make sdl-run      # SDL window
```

Run the same scenario in both. Watch the same beat (e.g. press A on
TITLE, observe the Beatrice transition). Confirm: same animation
frames, same total duration, same audio, same return-to-wood timing.

If they don't match, the platform layer is silently diverging. Don't
"fix it later" — fix it now, before any other change layers on top.

## Anti-checklist (signs of drift in PR review)

- A new function declared in `engine/scene.cpp` as `extern "C"` but
  only defined in `platform/arduboy/`. Check for SDL definition.
- A `#ifdef SCENE_PAGING_ENABLED` (or any platform symbol) around code
  that calls `display::*`, `audio::*`, or `data_flash::*`. Player-
  facing.
- A new sprite animation, scene transition, or audio cue added without
  testing both `make ardens` AND `make sdl-run`.
- "TODO: implement on SDL" in a commit message. There is no
  TODO — implement now.

## See also

- `.claude/CLAUDE.md` — concise rule statement at the project root.
- `docs/business-model.md` — why Arduboy is the lead platform.
- `docs/scene-paging.md` — the technical mechanism that has the
  most history of accidental divergence (banks, SPM, FX).
- `docs/bootloader.md` — bootloader-region traps that surface ONLY on
  Arduboy (linker layout). Reading the doc is part of the parity
  checklist when touching scene.ld or check_flash.py.
