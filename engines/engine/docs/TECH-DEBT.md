# Engine — Tech Debt

Known structural issues to fix at the root, not paper over. Add to this list
any time something is identified as architecturally wrong at the engine
level (i.e. not specific to one game).

For game-specific tech debt see each game's
`games/<game>/docs/TECH-DEBT.md`.

---

## Engine pipeline assumes 2D / sprite-sheet shape

Several engine entry points unconditionally invoke 2D-specific subsystems
that selva-oscura (a 3D game) doesn't use. Currently benign because each
target gracefully no-ops when its data is empty, but the dependency edge is
backwards: the engine should not assume any rendering paradigm.

Symptoms found during the post-rename leak hunt (2026-05-05):

- **`Engine::init` calls `TileMapRenderer::init()` unconditionally**
  (`engines/engine/src/Engine.cpp:74`). Allocates GL resources for a tilemap
  rendering pipeline that 3D games never use. `Engine::render` similarly
  calls `TileMapRenderer::render` (line 316), which early-outs on
  `sTMVertexCount == 0` so it's currently free at runtime. Same shape for
  `Engine::shutdown` (line 342).
- **`TileMapRenderer::cpp:175` hardcodes `tileF = 32.0f`** — assumes 32px
  tile size. prison-escape-game's calibration leaked into the engine.
- **`Engine::run` calls `AnimationSystem::update` unconditionally**
  (line 148). Sprite-sheet animation pipeline. Iterating an empty
  `view<Animation>` is cheap but the design is wrong: 3D games have skeletal
  animation, which would want a different system.
- **`Engine::render` writes `jitter.csv` when `TRACY_ENABLE=ON`** (line 279).
  Engine-level debug diagnostic shouldn't write to cwd unconditionally.

### Fix shape

Two paths for #1 and #3:

- **Opt-in flags on `Engine`** — `setUseTileMapRenderer(false)`,
  `setUseAnimationSystem(false)`. Cheap, mirrors `setClearColor`.
- **Move 2D-specific systems into per-game registration** — game `main`
  initializes the systems it wants and registers them with the engine via
  the existing render/update callback hooks. More work; reshapes engine API.

The right call depends on how much else needs the same treatment. Defer
both until the 3D path needs to actively *not* run a 2D system (i.e. when
selva-oscura has its own 3D rendering and the AnimationSystem would have to
no-op a non-trivial workload). For now: documented; not blocking.

For #2 (hardcoded tile size): when TileMapRenderer eventually fixes #1, also
take its tile size from configuration / per-tilemap data rather than a
constant.

For #4 (jitter.csv): engine should never write to cwd unconditionally. Move
to Tracy-only debug code that respects an opt-in flag.

---

## Engine tests depend on prison-escape-game's tile config

`engines/engine/tests/tilemap_test.cpp` loads `config/tilemap.json` and
`config/rooms` from prison-escape-game's directory (its `WORKING_DIRECTORY`
in CMake points at `games/prison-escape-game/`). Engine tests should be
self-contained; either inline a fixture tilemap into the test, or extract
a minimal test fixture into `engines/engine/tests/fixtures/`.

Cross-references the engine/game boundary doctrine in `ENGINE.md`.

---

## TileMapLoader belongs in game/, not engine/

`engines/engine/src/TileMapLoader.cpp` contains room placement, corridor
carving, and the full procgen pipeline — all game-specific logic. Engine
should only keep `TileMap` (data struct) and `TileMapRenderer` (rendering).
Move `TileMapLoader` to a per-game ops directory and expose only the data
structures from engine.

(Originally tracked in `games/prison-escape-game/docs/TECH-DEBT.md` —
duplicated here because it's an engine-side issue.)
