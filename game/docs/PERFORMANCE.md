# Prison Break Game — Performance Architecture

Running log of performance decisions, trade-offs, and benchmarks.
Updated as new decisions are made.

---

## Goal

Smooth 60 Hz on low-end hardware with enough headroom that enemy count never becomes a
bottleneck. The game design is souls-like (small groups of meaningful enemies, not swarms),
so actual enemy counts will be modest — but the engine should be efficient enough that this
is never a question. See CLAUDE.md "Performance Philosophy" for first principles.

---

## Decision Log

### [Issue #10] Tile Map — No ECS Wall Entities

**Problem:** The map is ~80x60 tiles. ~70-90% of tiles are walls (~3000-4300 entities).
The original design spawned one ECS `Collider + Transform + Tag` entity per wall tile.
Three systems all iterated these entities:

| System | Loop | Cost per frame |
|---|---|---|
| CollisionSystem (pair loop) | O(n_total^2 / 2) | ~8M iterations (~freeze) |
| MovementSystem (statics scan) | O(n_dyn x n_static) | ~20K AABB checks |
| FlowFieldSystem (BFS wall mark) | O(n_static) | ~4K lookups (rare, BFS-rebuild only) |

CollisionSystem was the critical failure: the pair loop visited all entity pairs including
static-static pairs (wall vs wall). With ~4000 walls, that's ~8 million pairs per frame at
60 Hz — the game froze within seconds.

**Fix:** The tile map is a 2D spatial index. Query it directly.

- `MovementSystem`: convert entity AABB to tile range → 2-4 direct `at(col, row)` lookups.
- `CollisionSystem`: depenetration pass does the same tile-range lookup.
- `FlowFieldSystem`: iterate `em.tile_map.tiles` instead of ECS view during BFS rebuild.
- `TileMapLoader::generate()`: `spawnWallEntities()` removed — no wall ECS entities created.
- ECS statics fallback retained for unit tests that set up explicit wall entities without
  a tile map, and for future non-tile collidables (doors, pressure plates, etc.).

**Result:**

| System | Before | After |
|---|---|---|
| CollisionSystem pair loop | ~8M iterations/frame (freeze) | ~O(n_dyn^2) — handful of pairs |
| MovementSystem wall check | O(n_dyn x 4000) = ~20K | O(n_dyn x 4) = ~20 |
| FlowFieldSystem BFS mark | O(4000) entity scan | O(4800) tile scan (no ECS overhead) |
| ECS entity count | ~4000 wall entities | 0 wall entities |

Scales to any map size — a 200x200 map costs the same per-entity as 80x60.

---

### [Issue #8] CollisionSystem — Bucket Split (static vs dynamic)

**Problem (latent, exposed by tile map issue):** Even after fixing the wall entity bloat,
the pair loop checked all pairs including static-static. With future non-tile ECS statics
(doors, etc.), this could re-emerge.

**Fix:** At the start of `CollisionSystem::update()`, split the entity list into two buckets:
`dynamics` (have `Velocity`) and `statics` (no `Velocity`). Only iterate:
- dynamic-vs-dynamic (MTV resolution)
- dynamic-vs-static (event emission + tile-map depenetration)

Static-static pairs are never visited. Cost scales with `O(n_dyn * n_all)` not `O(n_all^2)`.

---

### [Issue #8] FlowField — BFS Debounce (STABILITY_FRAMES)

**Problem:** Rapid player oscillation across a cell boundary caused the BFS to rebuild
every frame, making enemy directions flip each frame.

**Fix:** BFS only rebuilds after the player has been in the same cell for
`STABILITY_FRAMES` consecutive frames. During oscillation, enemies use the last stable
field — they make no net progress, but they don't thrash direction either.

---

### [Issue #8] RenderSystem — NOT YET Instanced

**Status:** Tracked as Issue #28.

Current: one draw call per entity. At 1000 enemies = 1000 draw calls/frame.
Tracy trace: render = 88% of frame time, mean ~15 ms.

Plan: instanced rendering — one draw call per texture sheet for all entities sharing
that texture. Expected to cut render time by 10-50x at 1000+ enemies.

---

### [Issue #29] CollisionSystem — Spatial Grid Broad-Phase

**Problem:** Dynamic-vs-dynamic collision was O(n_dyn^2). At 1000 enemies, the naive pair
loop would hit ~500K pair checks per frame.

**Fix:** Uniform spatial grid (64px cells, 2x typical 32px entity diameter). Two-pass
count+scatter algorithm into a flat array — no per-cell heap allocation. Vectors persist
across frames via `static` local (grow-only, no realloc after warmup).

Deduplication for multi-cell entities: for each candidate pair, compute the first shared
cell (top-left of the overlap of their cell ranges). Only process the pair in that cell.
O(1) per pair — 4 int comparisons + 1 multiply. No hash sets or bitsets.

**Memory:** ~70 KB total for an 80x60 cell grid (counts + offsets + entries arrays).

**Result (Tracy):**

| Metric | Before (trace 14) | After (trace 29) | 1000-enemy stress test |
|---|---|---|---|
| CollisionSystem avg | 0.46 ms | 0.03 ms | 1.18 ms |
| CollisionSystem max | — | — | 1.74 ms |

15x improvement at normal enemy counts. Scales near-linearly with entity count.

---

### Render Interpolation (Fixed-Timestep Wobble Fix)

**Problem:** The fixed-timestep loop (60 Hz) leaves an accumulator remainder after all ticks.
Entities render at their last-updated positions, but that position is stale by up to 16ms.
The remainder fluctuates semi-randomly each frame (vsync vs tick-rate drift), causing visible
wobble on all moving entities — especially the facing dot indicator.

**Fix:** Classic Gaffer-on-Games interpolation. Each fixed tick, `PreviousTransform` snapshots
the entity's position before systems update it. At render time, `render_alpha = accumulator /
FIXED_TIMESTEP` blends between previous and current:

```
displayPos = prev + (curr - prev) * alpha
```

Applied to: sprite draw positions, camera position, facing dot anchor.

**Cost:** One `PreviousTransform` copy (2 floats) per entity per tick. O(n) with negligible
constant. No allocations — `get_or_emplace` after the first frame is a pure get.

**Trade-off:** Adds ~16ms of visual latency (rendering a blend of past and present). At 60Hz,
imperceptible for a top-down 2D game.

**Additional fixes applied alongside:**
- Player facing moved out of fixed-step loop into the game's per-frame callback
  (`gamePerFrame`) — mouse position is a per-frame input, not physics. Eliminates
  stale-facing on 0-update frames.
- Facing dot position rounded to integer pixels — eliminates sub-pixel oscillation caused by
  camera integer-snapping vs unsnapped dot position.

---

### [Issue #32] GPU Readback Fix — TextureManager Dimension Cache

**Problem:** `RenderSystem` called `glGetTexLevelParameteriv` per entity per frame to get
texture dimensions for UV normalization. This is a GPU pipeline sync — the CPU stalls waiting
for the GPU to respond. At 1000 enemies, that's 4000 GL sync calls per frame.

**Fix:** `TextureManager` now stores a `TextureInfo` struct (GL handle + width + height) in its
cache, populated at load time from `stbi_load` results. New `getDimensions()` method does an
O(1) hash lookup. `RenderSystem` replaced 4 GL calls per entity with one hash lookup.

**Cost:** 8 extra bytes per cached texture (two ints). Negligible.

---

### [Issue #32] Animation System

**Architecture:** Sprite sheet animation driven by ECS components.

- `Animation` component: 76 bytes per entity (state data array, frame index, timer, dimensions)
- `AnimationSystem::update()` runs once per frame at wall-clock dt (not fixed-step)
- State resolution is O(1) per entity — priority check against existing components
- Direction snapping is O(1) — dominant axis comparison
- Frame advance is O(1) — timer comparison + modulo
- Sprite src rect update is O(1) — two multiplications

**Sheet layout:** Rows = animation states (idle/walk/attack/hit/death). Columns = direction
blocks (South/West/East/North) x frames. Column = `dir * max_frames_per_state + frame_index`.

**Death animation persistence:** `Dead` component carries a timer derived from the animation's
death state (frames x duration). `DeathSystem` ticks the timer and only destroys the entity
when it reaches zero. No-animation entities still destroy instantly (timer = 0).

**Cost at 1000 entities:** ~1000 hash lookups for state resolution + ~1000 src rect updates.
No allocations, no branching hot paths. Negligible vs render cost.

---

### [Issue #32] TileMapRenderer — Textured Tiles

**Change:** TileMapRenderer upgraded from flat-color triangles to textured tiles using a
dungeon tileset atlas. Dual-mode shader supports both textured and color-only rendering via
`uUseTexture` uniform. UVs baked into the static VBO at map generation time — zero per-frame
UV computation.

**Vertex layout:** 8 floats per vertex (pos.xy, uv.uv, color.rgba) vs previous 6 (pos.xy,
color.rgba). 33% more VBO memory but still a single static upload — no per-frame cost.

---

### [Issue #32] Split-Body Rendering (Player)

**Problem:** Player sprite faces movement direction, but in a twin-stick game the upper body
should face aim direction (mouse). Standard single-sprite animation can't represent two
facing directions simultaneously.

**Fix:** Player rendered as two child entities (lower body + upper body), each with independent
Animation and Sprite components. Lower body faces velocity direction with walk/idle states;
upper body faces FacingDirection (mouse aim) with attack/idle states.

**Architecture:** Child entities linked via `BodyPart` component (`parent`, `direction_from_facing` flag).
Parent entity keeps all gameplay components (Transform, Health, Stats, etc.) but has no
Sprite or Animation. Children inherit position from parent each frame.

**State resolution per body part:**
- Lower body (`direction_from_facing=false`): Dead > Hit > Attack > Walk > Idle. Direction from parent Velocity (or FacingDirection when backpedaling).
- Upper body (`direction_from_facing=true`): Dead > Hit > Attack > Idle. Direction from parent FacingDirection.

**Backpedal:** When `FacingDirection.backpedaling` is true (movement opposes aim), the lower
body switches to aim direction and walk frames advance in reverse. Speed multiplier
(`backpedal_multiplier`) applied in MovementSystem. Animation speed slowed by 1.4x to match
reduced movement. No new components or per-frame allocations -- single bool check in existing
AnimationSystem loops.

**Sprite split method:** LPC body base is a full-body silhouette. Both sheets include the body
base but apply a vertical alpha mask at `BODY_SPLIT_Y=35` within each 64x64 frame. Lower body
keeps only rows >= 35 (legs/feet), upper body keeps only rows < 35 (head/shoulders).

**Limitation (accepted):** LPC sprites have no torso-twist frames. The split at y=35 means
only head and slight shoulders visually rotate. Full torso rotation requires custom art
(planned for future). This is a known LPC asset limitation, not an engine limitation.

**Cost:** Two extra entities per split-body character. Two extra Animation state lookups and
Sprite draws per frame. Position sync loop is O(n_body_parts) — negligible. No impact on
enemies or other single-sprite entities.

**Enemy sprites:** Enemies use a pre-composited LPC skeleton universal sheet (832x1344),
remapped at build time to our 5-row format. No layer compositing or split-body — single
Animation + Sprite entity per enemy.

---

### TileMapRenderer — Row Frustum Culling

**Problem:** TileMapRenderer drew ALL tiles every frame (one `glDrawArrays` for the entire
map). At wave 40 (160x120 = 19,200 tiles), Tracy showed 14.37ms avg — 86% of the 60Hz
frame budget spent rasterizing off-screen tiles.

**Fix:** Compute visible row range from camera position and window size. Single `glDrawArrays`
call with offset + count covering only visible rows. Map width/height/tile size stored during
`upload()`, used in `render()` for the calculation.

**Result:** On a 1080p screen (34 visible rows out of 120), draws ~28% of tiles instead of
100%. Saves ~10ms per frame on large maps. Row-only culling (not per-column) keeps it as a
single draw call.

---

### Heavy Synchronous Operations — Timing Reset

**Problem:** Map generation (600ms+), world create/destroy, and bulk entity spawns block the
main thread. Two side effects:
1. The fixed-step accumulator sees hundreds of ms of "missed" time → fires dozens of catch-up
   ticks in one burst → entities teleport.
2. The EMA-smoothed frame timer absorbs the spike → FPS counter shows ~30 for seconds even
   though actual frame rate is fine.

**Fix:** `Engine::requestTimingReset()` — called after any heavy synchronous operation. Zeros
the accumulator (no catch-up ticks), snaps `previousTime` to current time (next frame sees a
clean ~16ms delta), and resets the EMA to 1/60s.

**Loading overlay pattern:** For wave-start map regen, `showLoadingOverlay()` in GameLoop.cpp
renders a "Wave N" screen and calls `engine.swapBuffers()` before the heavy work. The overlay
stays visible on screen during the freeze.

**Room file caching:** `TileMapLoader::generate()` caches `loadRooms()` results in a static.
Room files never change at runtime — eliminates repeated disk I/O on every wave start.

**Wave 1 optimization:** `WorldInit::createWorld()` already generates the map, so
`commitWaveStart()` skips `needs_map_regen` for wave 1 to avoid a redundant second generation.

---

## Tracy Profiling Notes

Profiling setup: `cmake.configureArgs: ["-DTRACY_ENABLE=ON"]` in `.vscode/settings.json`.
Launch Tracy GUI: `scripts/tracy.bat` (Windows) or `scripts/tracy.sh` (MSYS2).

Initial trace (Issue #9, pre tile-map, ~5 enemies):
- `render` zone: 88% of frame time, mean ~15.3 ms
- `update` zone: 7.7%, mean ~1.3 ms
- Only 2 Tracy zones visible — per-system instrumentation still TODO (Issue #27)

Only 2 Tracy zones visible because instrumentation is shallow (top-level render/update
only). Issue #27 will add `ZoneScoped` to individual systems.

1000-enemy stress test (Issue #29, spatial grid):
- `CollisionSystem`: 1.18 ms avg, 1.74 ms max
- `RenderSystem`: 6.49 ms avg (draw-call-per-entity bottleneck, Issue #28)
- `TileMapRenderer`: 5.86 ms avg (Issue #31)
- `WaveSystem`: 75.5 ms max spike (alive-count scan)
