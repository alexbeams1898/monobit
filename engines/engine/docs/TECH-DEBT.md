# Engine — Tech Debt

Known structural issues to fix at the root, not paper over. Add to this list
any time something is identified as architecturally wrong at the engine
level (i.e. not specific to one game).

For game-specific tech debt see each game's
`games/<game>/docs/TECH-DEBT.md`.

---

## (No open engine-level tech debt at this time.)

The previous post-rename leak hunt items (engine pipeline assumed 2D /
sprite-sheet shape; engine tests depended on prison-escape-game's tile
config; TileMapLoader lived in engine/) were resolved on
`chore/engine/121-doctrine-leak-quad`:

- `Engine::setRenderWorld(fn)` callback added; engine no longer calls
  `RenderSystem`, `TileMapRenderer`, or `AnimationSystem` directly. Games
  drive their own world rendering. 2D systems run only when the game
  registers them.
- `TileConfig::atlas_tile_size` (loaded from `tilemap.json`, default 32)
  replaces the hardcoded `tileF = 32.0f` in `TileMapRenderer`.
- `TileMapLoader` and the `Room` struct moved to
  `games/prison-escape-game/`. The `tilemap_test` moved with them. Engine
  tests are now self-contained and no longer need a `WORKING_DIRECTORY`
  override pointing at a game directory.
- `jitter.csv` debug-write block removed from `Engine::render`. Engine no
  longer writes diagnostic files to cwd; Tracy's own profiling covers the
  use case.

When new engine-level structural issues are identified, add them here with
**Symptom**, **Root cause**, and **Fix shape** sections so the next
engine-cleanup pass has clear inputs.
