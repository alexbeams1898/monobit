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

### [Issue #81] Paper-Doll Sprite Compositing

**Problem:** The original issue #32 player used two child entities (lower + upper body) to
get twin-stick facing. This scaled badly for customization (N layers = N child entities = N
animation ticks + N draws per frame) and interacted awkwardly with equipment visibility.

**Fix:** `SpriteCompositor` (`engine/include/SpriteCompositor.h`) builds the final character
texture once, CPU-side, by alpha-blending a stack of LPC layer PNGs into a single GL
texture. The character is then one entity with one Sprite and one Animation. WASD-locked
facing (docs/CLAUDE.md) handles the twin-stick feel that the split was supposed to provide.

**Pipeline:**
1. `fetch_lpc.py` pulls per-layer, per-animation PNGs from the upstream LiberatedPixelCup
   repo into `game/assets/sprites/lpc/raw/`.
2. `bake_palettes.py` reads upstream palette definition JSONs and bakes per-palette color
   variants of the grayscale master PNGs (skin tones + clothing colors).
3. `assemble_spritesheet.py` stitches per-animation PNGs into the 2048x384 character-sheet
   layout (rows = Idle/Walk/Attack/Hit/Death/Run; columns = direction blocks x frames).
4. At character creation / entity load, `AppearanceOps::resolveAppearance` calls
   `SpriteCompositor::composite(layers)` which returns a cached GL texture ID. The entity's
   `Sprite.texture_id` is set to that ID and the `AppearanceDef` component is removed.

**Cache:** SpriteCompositor keys on the joined path list of all layers. Two characters with
identical layer stacks share one GL texture. Swapping equipment will call composite again
with the new layer list; if the new combination is already cached the lookup is O(1).

**Cost:**
- **Composite call:** O(W*H*L) CPU blend, where L = layer count (~9-11 for current
  characters). Runs once per unique layer combination. Negligible (sub-ms) at character
  load; never during steady-state frames.
- **Runtime:** zero extra cost vs. a single-sheet character. One entity, one animation, one
  draw. This is strictly cheaper than the old split-body design.
- **Memory:** one RGBA texture per unique character combination. With cache sharing, this
  stays bounded -- all cops share one texture, the player's composited sheet is one texture,
  etc.

**Dead -> Hit animation fix:** `AnimStateSystem` routes `Dead` to the `Hit` row (the LPC
"hurt" pose reads as a death reaction). `AnimationSystem::advanceAnimation` freezes the
last frame for both `Death` and `Hit` so the pose holds instead of looping. `DamageSystem`
sizes the `Dead.timer` from the `Hit` row duration so the corpse lingers for exactly the
animation length before `DeathSystem` destroys the entity.

**Enemy sprites:**
- **Cops** go through the same AppearanceDef pipeline as the player, with layers fully
  pre-resolved in `config/entities/cop.json`. Cached in the compositor so every cop shares
  one texture.
- **Skeletons** still use a single hand-composited LPC skeleton sheet. No AppearanceDef, no
  compositor call. Kept as the baseline for entities that don't need customization.

**Removed:** `BodyPart` component and its position-sync system. Old `player_lower` /
`player_upper` animation sidecars and PNG masks deleted. The `BODY_SPLIT_Y=35` vertical
alpha mask is gone -- LPC bodies now composite at full resolution.

**See also:** `docs/SPRITE-UPGRADE-NOTES.md` has the full implementation log, including
which layers ship today, what diverged from the original issue #81 plan, and the layer
draw order.

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

### TileMapRenderer — Column Culling

**Problem:** Row frustum culling (previous fix) still drew every column in each visible row.
On wide maps (160 columns at wave 40), most columns are off-screen.

**Fix:** Compute visible column range from camera X position and window width, same as existing
row culling. Instead of one `glDrawArrays` per visible row, each row draws only the visible
column span. Vertices per tile = 6 (two triangles), so offset/count math maps directly from
tile coordinates.

**Result (Tracy, 160x120 map):** TileMapRenderer avg dropped from 14.71ms to 0.00ms. On a
1080p screen (~34 visible columns out of 160), draws ~21% of tiles per row. Combined with row
culling, total visible tiles ≈ 6% of map.

---

### [Issue #65] AI Navigation — LoS-Gated Attack Velocity

**Problem:** Slotted enemies (Attack state with assigned slot_angle) used direct navigation
exclusively toward their slot position. When a wall or pillar blocked the straight-line path,
they walked into the wall, got stuck, drifted beyond deaggro_radius, and disengaged.

**Fix:** `computeAttackVelocity` checks `TileMap::hasLineOfSight` (DDA grid traversal) from
the enemy to its slot goal position. If LoS is clear, direct nav spreads enemies around the
player. If a wall blocks it, falls back to flow field navigation to get around the obstacle.

**Cost:** One DDA raycast per slotted enemy per frame (~10-15 tile checks). Negligible vs
existing flow field and collision costs.

---

### [Issue #65] AI Combat — Waiter Speed Scaling

**Problem:** All enemies in Attack state moved at full speed regardless of token status,
causing non-attackers to sprint to their slot positions and crowd the player.

**Fix:** Non-token-holders move at a fraction of their normal speed (configurable via
`formulas.json` `combat_ai.waiter_speed_scale`, default 0.15). They drift slowly toward their
slot instead of sprinting — "waiters" circling at the outer ring rather than rushing in.

**Cost:** One float multiply per non-holder per frame.

### Camera Interpolation — Own Prev Position

**Problem:** Camera render interpolation used the player entity's `PreviousTransform` to blend
between ticks. This works when the camera sits exactly on the player, but breaks when game code
offsets `Camera.x/y` from the entity (e.g. lock-on camera blend toward an enemy). The offset
isn't captured in `PreviousTransform` (which tracks physical position), so the camera offset
snaps at tick boundaries instead of interpolating smoothly — visible as jitter during lock-on.

**Fix:** Added `prev_x/prev_y` fields to the `Camera` component, snapshotted alongside
`PreviousTransform` before each tick. Render interpolation now uses:
```
camX = camera.prev_x + (camera.x - camera.prev_x) * alpha
```
This captures any game-applied offset (lock-on blend, screenshake, etc.) and interpolates it.

**Pre-render callback:** Positions that must match render interpolation (e.g. crosshair on a
lock-on target) are computed in `gamePreRender`, which runs after the tick loop with the final
`render_alpha`. This ensures the crosshair tracks the target's interpolated visual position,
not the stale fixed-step position.

**Engine callback order:**
1. `per_frame_update` (mouse aim, input) — before tick loop
2. Fixed-step tick loop (game systems)
3. `pre_render` (interpolation-dependent state) — after tick loop, before render
4. `render` (world + UI)

**Cost:** 2 extra floats per Camera entity (one entity). One extra loop over cameras per tick
(single entity — negligible).

### Lock-On Camera Blend — Additive Offset Architecture

**Previous approach (replaced):** `Camera.override_active` flag suppressed CameraSystem's
entity-position snap, letting lock-on blend own `camera.x/y` directly. This caused two
problems: the blend never accumulated (CameraSystem reset it each tick), and stale offsets
persisted after release (required hard snap-on-release which could cause jumps).

**Current approach:** Lock-on camera displacement stored as a separate additive offset
(`Camera.offset_x/y`) with its own interpolation pair (`prev_offset_x/y`). CameraSystem
always snaps `camera.x = transform.x` (never suppressed). The offset is computed in
`gameUpdate` after CameraSystem: `goalOff = (enemy - player) * 0.3`, blended at
`CAM_BLEND_SPEED = 8`. At render time, base and offset are interpolated separately with the
same alpha, then summed. This ensures camera and player sprite share the exact same
interpolation base — no step-size mismatch.

**Release:** When lock-on ends, the offset decays smoothly via exponential blend
(`DECAY_SPEED = 12`) in `gameUpdate`. No hard-zero — zeroing both offset and prev_offset
simultaneously kills interpolation and causes a one-frame camera jump equal to the full
offset magnitude.

**Pixel rounding:** The interpolated offset is rounded to integers before adding to the
camera base. Without this, the sprite position (independently rounded) and camera position
(independently rounded) have rounding errors that don't cancel, causing 1-2px screen-space
oscillation visible as player vibration during lock-on.

**Jitter CSV diagnostic:** Per-frame CSV logging of camera/player positions, gated behind
`TRACY_ENABLE`. Columns: frame, alpha, camX/Y, offsetX/Y, prevOffsetX/Y, drawX/Y,
screenX/Y, frameDtMs. Essential for diagnosing interpolation bugs — the interpolated values
alone mask whether the issue is in the raw position or the interpolation math.

---

### SoundConfig -- Map-Based Lookup

**Before:** SoundConfig had ~25 hardcoded `SoundEntry` fields plus parallel `std::vector<std::string>`
fields for variations. Adding a new sound key required: adding a field to the struct, adding to
`soundByKey()` and `variationsByKey()` if-chains, adding parsing to `ConfigLoader::loadSounds()`,
and updating every consumer. ~100 lines of boilerplate per sound key.

**After:** Single `std::unordered_map<std::string, SoundEntry>` where `SoundEntry` contains
`{path, volume, variations}`. `ConfigLoader::loadSounds()` iterates the JSON object in a 10-line
loop -- any new key in sounds.json is automatically available. Consumers call `snd.get("key")`
which returns a static empty entry on miss (safe).

**Performance:** Map lookup is O(1) amortized (hash). Sound lookups happen at event time
(attack, footstep, UI click) -- never in hot per-entity loops. The overhead vs direct field
access is negligible. The real win is maintainability: zero C++ changes to add a sound.

---

### Lock-On Camera — Smooth Offset Decay on Release

**Problem (jitter.csv analysis):** When lock-on ended (target died, player toggled off, or no
replacement found), `gamePerFrame` hard-zeroed both `camera.offset_x/y` AND
`camera.prev_offset_x/y` simultaneously. This killed render interpolation — both endpoints
became zero instantly. The camera jumped the full offset distance (up to ~55px) in one frame.
Visible as position discontinuities at frames 1880, 1939, 1960, 2021 in the CSV trace.

**Existing smooth decay (unused):** `gameUpdate` already had an exponential decay path
(`DECAY_SPEED = 12`) that smoothly blends offsets back to zero when no lock-on is active,
with a 0.1px threshold snap at the end. This path was never reached because `gamePerFrame`
(which runs before `gameUpdate`) already zeroed everything.

**Fix:** Removed the hard-zero block from `gamePerFrame`. The existing smooth decay in
`gameUpdate` now handles the transition. The `hadLockOn` tracking variable was removed since
it's no longer needed.

---

### Lock-On Player Vibration — Backpedal Hysteresis + Offset Pixel-Rounding

**Problem:** Player sprite visibly vibrated when locked on to an enemy, especially when
strafing or positioned diagonally relative to the target.

**Root cause 1 — backpedal flicker:** `updateFacingAndBackpedal()` in MovementSystem used a
hard `dot < 0.0` threshold to toggle backpedaling. When strafing (movement perpendicular to
facing), the dot product hovered near zero and oscillated across the threshold tick-to-tick.
Each toggle flipped speed between 100% and 70% (`backpedal_multiplier`), creating a velocity
oscillation visible as physical vibration.

**Fix:** Hysteresis dead zone. Backpedal activates at `dot < -0.15` and deactivates at
`dot > +0.15`. The ~17-degree dead zone on each side of perpendicular prevents flicker during
strafing while still triggering correctly for actual backward movement.

**Root cause 2 — independent pixel rounding:** RenderSystem rounds both the camera position
(`std::round(camX)`) and each sprite position (`std::round(drawX)`) independently to the
pixel grid. Without lock-on, `camX == drawX` (player-centered camera), so rounding errors
cancel perfectly. With lock-on, the camera offset displaces them, and independent rounding can
push them in opposite directions -- one rounds up while the other rounds down, creating 1-2px
oscillation frame-to-frame.

**Fix:** Round the interpolated camera offset to integers before adding it to the camera base
position (`Engine::render()`). This forces the offset onto the pixel grid so sprite and camera
rounding errors stay in sync.

**Known residual:** Fixed-timestep interpolation creates a subtle tile-scrolling sawtooth when
the tick rate (60 Hz) and display refresh aren't perfectly phase-locked. The accumulator alpha
cycles in a repeating pattern (e.g. 0.40/0.42/0.44) creating SHORT-MEDIUM-LONG step sizes.
This is inherent to fixed-timestep interpolation and most visible on large static backgrounds.
Options if revisited: higher tick rate (120 Hz), or sub-pixel tile rendering (trade sharpness
for smoothness).

---

### UIRenderer — GL Pipeline Stall Elimination + Shared Font Atlas

**Problem (Tracy trace jitter4):** UIRenderer averaged **15.45 ms/frame** — consuming 93% of
the 16.67ms frame budget. Not the cause of lock-on jitter, but a separate performance issue
that caused frame time spikes and reduced overall headroom.

**Root causes and fixes:**

| Problem | Fix | Impact |
|---|---|---|
| `glGetUniformLocation` called per-frame + per-batch | Cache locations at `init()` in static `GLint`s | Eliminates synchronous driver string lookups |
| `glBufferSubData` without orphaning stalls CPU | `glBufferData(nullptr)` before sub-data upload | GPU reads old buffer while CPU writes new one |
| Ortho projection rebuilt every frame | Rebuild only on `resize()`, cache in static array | Eliminates redundant matrix math |
| 3 separate font atlas textures (same .ttf, 3 sizes) | `loadFontGroup()` packs all sizes into one 1024x1024 atlas via `stbtt_PackFontRanges` | All sizes share one GL texture; no batch breaks |
| `vector::insert` per quad | `resize()` + direct pointer writes | Eliminates per-quad function call overhead |

**FontManager API addition:** `FontManager::loadFontGroup(path, {size1, size2, ...})` returns
a vector of FontHandles sharing a single atlas texture. `shutdown()` tracks deleted textures
to avoid double-freeing shared atlases.

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
