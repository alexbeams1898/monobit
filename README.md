# Monobit

A multi-game / multi-engine workspace. Custom C++ engine, multiple games
built on top.

> Early development. Source is private; binary releases for individual games
> are published to per-game distribution repos
> (e.g. [prison-escape-game-releases](https://github.com/alexbeams1898/prison-escape-game-releases)).

## Stack

C++17 · SDL2 · OpenGL 3.3 · entt (ECS) · FMOD · CMake · Ninja

## Layout

```
engines/
  engine/             main custom engine -- long-term home for everything
  arduboy-legacy/     archived 1-bit Arduboy engine (read-only reference)
games/
  prison-escape-game/ top-down action roguelike
  selva-oscura/       3D soulslike (early)
  rpg-arduboy/        archived original Arduboy RPG (read-only reference)
cmake/                shared CMake infrastructure
vendor/               vendored dependencies (miniz, stb)
```

Each engine and each game declares its own `project(... VERSION X.Y.Z)` and
versions independently. The arduboy-legacy directories are not part of the
build.

## Build

Requires MSYS2 MinGW64 toolchain on Windows.

```bash
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build
./build/bin/prison-break-game.exe   # the prison-escape game
./build/bin/selva-oscura.exe        # the selva-oscura game (when present)
```

In VS Code, use the CMake Tools status bar at the bottom to pick which
target F7 builds and F5 runs.

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
  (imported from the archived mono repo)

### Per-engine

- engine: [ENGINE](engines/engine/docs/ENGINE.md)
  · [3D extension plan](engines/engine/docs/3D-EXTENSION.md)
- arduboy-legacy: [ARCHIVED.md](engines/arduboy-legacy/ARCHIVED.md)
  (do not extend; merge target is the main engine)
