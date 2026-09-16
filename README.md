# Monobit

[![CI](https://github.com/alexbeams1898/monobit/actions/workflows/ci.yml/badge.svg?branch=master)](https://github.com/alexbeams1898/monobit/actions/workflows/ci.yml)

One custom C++ engine, four games, and the build and release infrastructure
that ships them.

## Build and release infrastructure

The part worth reading first. One engine and four games share one pipeline,
and every scope releases independently.

**Release automation** — [`release.yml`](.github/workflows/release.yml) reads
the branch prefix `<bump>/<scope>/<issue>-<desc>` and derives the rest:

| from the branch | what happens |
|---|---|
| `patch` `minor` `major` | bump that scope's version, tag, build, publish |
| `chore` `docs` | changelog only, no version bump |
| `<scope>` | picks which `CMakeLists.txt` and `CHANGELOG.md` move |

Changelog bullets come from the merged PR body, are validated before merge, and
are prepended to the scope's `[Unreleased]` section — then promoted to a real
version on a bump. Each game declares its own target, artifact name, and
distribution repo in [`.release-config.yml`](games/selva-oscura/.release-config.yml);
releases are mirrored to per-game public repos, and a scope with no distribution
repo publishes the tag and skips the mirror. A guard stops the bot's own bump
commit from retriggering the workflow.

**CI** — [`ci.yml`](.github/workflows/ci.yml), two stages. Everything that needs
no toolchain runs first and gates the rest, so a formatting slip costs thirty
seconds rather than twenty runner-minutes.

| stage | job | what it enforces |
|---|---|---|
| 1 | Code standards | formatting, comment density and content, deferred-work markers, complexity limits, per-game gates, script tests |
| 2 | Build and test | Debug build, unit tests, and a boot smoke test that launches the game |
| 2 | Lint | clang-tidy, scoped to what the change touched |
| 2 | Static analysis | cppcheck against a first-party-only compile database |
| 2 | Tests (sanitized) | the suite again under ASan + UBSan |

A pull request and the push that merges it lint the same file set, so nothing
can pass the gate and break master a second later. Whole-tree analysis runs
nightly and on demand, where a half-hour job belongs.

**Custom linters** live in [`scripts/`](scripts/) and catch what a compiler
cannot: comment density and content, deferred-work markers, vocabulary drift,
config keys nothing reads, and authored map entities with no builder. They share
one derived definition of which trees they scan, because three hand-written
copies of that list had each silently stopped covering a game.

## Stack

C++17 · SDL2 · OpenGL 3.3 · entt (ECS) · miniaudio · Jolt · ozz-animation ·
Dear ImGui · CMake · Ninja

## Layout

```
engines/
  engine/             main custom engine -- long-term home for everything
  arduboy-legacy/     1-bit Arduboy backend, not yet folded into the engine
games/
  prison-escape-game/ top-down action roguelike
  selva-oscura/       3D soulslike
  point-of-entry/     2D action RPG, mouse-aimed combat
  wayworn-hush/       2D exploration, turn-based
  rpg-arduboy/        archived original Arduboy RPG (read-only reference)
cmake/                shared CMake infrastructure
scripts/              workspace tooling: changelog, linters
vendor/               vendored dependencies (miniz, stb)
```

Each engine and each game declares its own `project(... VERSION X.Y.Z)` and
versions independently.

`arduboy-legacy` is an earlier pass at running the engine on a 32KB
microcontroller -- the constrained-platform target, kept for the parts worth
folding back in. It is not in the build.

Binaries for individual games are published to per-game distribution repos —
e.g. [prison-escape-game-releases](https://github.com/alexbeams1898/prison-escape-game-releases).

## Build

Requires MSYS2 MinGW64 toolchain on Windows.

```bash
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build
./build/bin/prison-escape-game/prison-break-game.exe
./build/bin/selva-oscura/selva-oscura.exe
./build/bin/point-of-entry/point-of-entry.exe
./build/bin/wayworn-hush/wayworn-hush.exe
```

In VS Code, use the CMake Tools status bar at the bottom to pick which
target F7 builds and F5 runs.

### Editor / language server

Install the **clangd** VS Code extension (not the default C/C++ IntelliSense —
run one or the other, not both). A committed `.clangd` at the repo root points it
at `build/compile_commands.json` (CMake exports this), which gives working
go-to-definition, find-references, and — the one that saves real pain —
**rename-symbol (F2)** that propagates a type or function rename across every file
correctly, instead of a manual find-and-replace. Configure the build once (F7) so
`build/compile_commands.json` exists before opening a C++ file.

## Documentation

### Per-game

- prison-escape-game: [DESIGN](games/prison-escape-game/docs/DESIGN.md)
  · [ENGINE](games/prison-escape-game/docs/ENGINE.md)
  · [PERFORMANCE](games/prison-escape-game/docs/PERFORMANCE.md)
  · [TECH-DEBT](games/prison-escape-game/docs/TECH-DEBT.md)
  · [RESEARCH](games/prison-escape-game/docs/RESEARCH.md)
  · [CHANGELOG](games/prison-escape-game/CHANGELOG.md)
  · [CREDITS](games/prison-escape-game/CREDITS.md)
- selva-oscura: [design canon](games/selva-oscura/docs/design/)
- point-of-entry: [PITCH](games/point-of-entry/docs/design/PITCH.md)

### Per-engine

- engine: [ENGINE](engines/engine/docs/ENGINE.md)
  · [3D extension plan](engines/engine/docs/3D-EXTENSION.md)
- arduboy-legacy: [ARCHIVED.md](engines/arduboy-legacy/ARCHIVED.md)
  (frozen until it is folded into the engine as a platform target)
