# Prison Break Game

A top-down action roguelike. Custom C++ engine.

> Early development. Source is private; binary releases are published to [prison-escape-game-releases](https://github.com/alexbeams1898/prison-escape-game-releases).

## Stack

C++17 · SDL2 · OpenGL · entt (ECS) · CMake · Ninja

## Build

Requires MSYS2 MinGW64 toolchain on Windows.

```bash
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build
./build/bin/prison-break-game.exe
```

## Documentation

- [game/docs/DESIGN.md](game/docs/DESIGN.md) — game design
- [game/docs/ENGINE.md](game/docs/ENGINE.md) — engine architecture and patterns
- [game/docs/PERFORMANCE.md](game/docs/PERFORMANCE.md) — performance philosophy and decisions
- [game/docs/TECH-DEBT.md](game/docs/TECH-DEBT.md) — known structural issues
- [game/docs/RESEARCH.md](game/docs/RESEARCH.md) — non-urgent topics to revisit
- [CHANGELOG.md](CHANGELOG.md) — release history
