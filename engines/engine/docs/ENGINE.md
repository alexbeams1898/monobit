# Engine Architecture

How the engine is structured, where things belong, and the hard-won patterns
that shouldn't be relearned. Reference before designing any system touching
movement, collision, or AI.

This file owns **engine** doctrine — applicable to every game on this engine.
Per-game architectural notes (helper trees, dialog templates, registry
singletons, content-specific conventions) live in each game's
`docs/ENGINE.md`, e.g. [games/prison-escape-game/docs/ENGINE.md](../../games/prison-escape-game/docs/ENGINE.md).

---

## Stack

C++17/20 · SDL2 (windowing/input) · OpenGL (rendering) · entt (ECS) · FMOD (audio) · CMake

**ECS:** Everything is an entity. Components are pure data. Systems are logic.
Adding content = writing a config file. Non-negotiable.

Goals: data-driven, config-driven, moddable from day one.

---

## Engine/game boundary (hard rule)

**`engines/engine/`** is a reusable library with zero knowledge of any specific
game. It provides:

- Core loop, windowing, input, GL context (SDL2 + OpenGL 3.3 core)
- ECS registry, generic components (Transform, Velocity, Sprite, Animation,
  Collider, etc.)
- Generic systems available to games: RenderSystem (sprite quads, Y-sort),
  AnimationSystem (sprite-sheet rows), MovementSystem, CollisionSystem,
  FlowFieldSystem, CameraSystem, SteeringSystem, TileMapRenderer.
  These ship from the engine but are **driven by the game**, not the
  engine — see "Render callback model" below.
- Infrastructure: TextureManager, AudioSystem, UIRenderer (engine-driven),
  TileMap / TileConfig data structs, FontManager, SpriteCompositor.

**`games/<game>/`** is everything specific to that game:

- Game components (Stats, Weapon, AIController, Inventory, etc.)
- Game systems (DamageSystem, WaveSystem, LevelingSystem, CombatSystem, etc.)
- All config loading, balance, world generation, screens, menus
- Map generation logic (room placement, corridor carving, procgen pipeline)

**Rules:**

- `engines/engine/` must never `#include` any `games/<game>/` header.
- No game-specific constants, magic numbers, or balance values in the engine.
- Engine components used as game-engine bridges (e.g.
  `FacingDirection.sprinting`, `.backpedaling`, `.walk_anim_speed`,
  `.aim_dx/aim_dy`) must be **generic**: game sets values, engine reads them.
  The engine must not know *why* these values are set.
- If a value is game-tunable, it must come from config JSON or be set by game
  code on a component — never hardcoded in the engine.
- When in doubt: if it references game concepts (waves, items, crafting,
  scoring, map generation, room templates), it goes in the game directory.

When working with multiple games on this engine, leaks of game-specific logic
into the engine surface immediately as visible artifacts in the second
consumer. Fix at root — move to the game directory or expose as a setter on
`Engine` — rather than papering over with flags. See `feedback_engine_leaks`
in memory for the locked pattern.

---

## Render callback model

The engine owns the **frame**: window, GL context, input pump, fixed-step
update loop, render-tick interpolation, framebuffer clear (color + depth),
camera-entity interpolation, UIRenderer setup/teardown, SwapWindow. It does
**not** own world rendering. The game registers callbacks the engine
invokes at the right moments:

- `setRenderWorld(fn)` — called between framebuffer clear and the UI pass.
  This is the only world-render callsite. 2D games call `TileMapRenderer`
  + `RenderSystem` here; 3D games run their own pipeline (geometry pass,
  optional dither / threshold / outline post-process, blit).
  Receives `Engine&`, `EntityManager&`, plus the engine-interpolated
  `camX`/`camY`/`alpha` (useful only for games that use the 2D `Camera`
  component; 3D games ignore them and use their own camera state).
- `setRenderUI(fn)` — screen-space overlay drawn after world content,
  inside `UIRenderer::beginFrame()` / `endFrame()`. HUD, menus,
  notifications.
- `setRenderDebug(fn)` — debug overlays, drawn between UI begin and the UI
  callback. `DebugDraw::setCamera()` is configured automatically beforehand.
- `setPreRender(fn)` — wall-clock-rate hook after `render_alpha` is computed
  but before rendering. Sprite-sheet animation advance lives here (game
  calls `AnimationSystem::update(em, engine.frameDt())`), as do any
  per-frame interpolations that must match render alpha (lock-on aim, etc.).
- `setOnResize(fn)` — invoked on `SDL_WINDOWEVENT_RESIZED` after the engine
  updates window dimensions and resizes UIRenderer. Game resizes any
  game-owned render targets here (e.g. `RenderSystem::resize` for the
  offscreen FBO).

**Lifecycle of game-owned render subsystems.** Whatever the game registers
in `setRenderWorld` it must also `init()` after `Engine::init()` returns
and `shutdown()` after `Engine::run()` returns but before the destructor
fires. `RenderSystem` and `TileMapRenderer` are typical examples:
prison-escape-game inits/shuts them down explicitly in its `main.cpp`.

**Why callbacks rather than CMake module split.** The previous draft of
3D-EXTENSION.md proposed splitting `engine/` into `engine/render2d/`,
`engine/render3d/`, etc. CMake-level modules. The callback model gets the
same boundary (engine has no opinion about render paradigm) with one tenth
the plumbing. The CMake split is deferred indefinitely; revisit only if a
game starts paying for unused engine code in a measurable way.

---

## Engine patterns — hard-won lessons

### Collision architecture

Two separate responsibilities — keep them separate:

- **MovementSystem** — *prevents* static penetration via axis-split velocity
  projection. Tests X then Y independently before integrating. Never corrects
  after the fact.
- **CollisionSystem** — emits `CollisionEvent` for every overlapping pair.
  Corrects position *only* for dynamic-vs-dynamic pairs. Does not touch
  static-dynamic — that would double-correct.
- **Static depenetration pass** — after dyn-vs-dyn MTV resolution, a second
  pass sweeps each dynamic against all statics. Without this, MTV can push a
  dynamic into a wall and MovementSystem will zero its velocity permanently
  next frame.

### Corner sticking / movement inset

**Problem:** Entity freezes at doorways — adjacent tile clips it 1-2 px,
zeroing velocity.
**Fix:** `MOVEMENT_INSET = 2.0f` in `MovementSystem.cpp`. Projects a
`(width-2)×(height-2)` box. Absorbs micro-clips; cannot phase through 32px
walls. Increase inset if sticking returns; decrease if thin-geometry clipping
appears. Calibrated for 32×32 entities on a 32px tile grid.

### Flow field navigation

**Problem:** Direct-vector AI gets stuck behind any wall.
**Pattern:** BFS from player cell outward; each cell stores normalized
direction back toward player. AI does O(1) table read per frame. Rebuild only
when player crosses into a new cell.
**Implementation:** `FlowFieldSystem` + `ChaseSystem` (game-side). Grid:
128×128 cells, 16px/cell (CELL_SIZE=16 aligns tile edges to boundaries).
8-directional BFS — diagonal steps allowed when both cardinal intermediaries
are routable (no wall, no clearance). Safe because 8-way clearance guarantees
routable cells have no wall within 1 diagonal step.
**Scalability:** BFS = O(16384 cells), once per cell crossing. Per-enemy
cost = O(1).

### Flow field clearance zones and direction fill

**Problem:** BFS paths along wall edges cause movement zeroing. Clearance
cells have no BFS direction and fall back to direct-vector, which points into
walls at corners.
**Clearance zone:** Before BFS, mark every open cell adjacent to a wall as
clearance (impassable to BFS). Adjacency is **8-directional (cardinal +
diagonal)**. Cardinal catches cells beside wall faces; diagonal catches cells
at wall corners. Without diagonal, a cell just outside a door jamb corner
passes the 4-way test but a 32px entity still clips the corner →
MovementSystem blocks it → entity freezes. This manifests as a
direction-specific bug ("works south, breaks north") with a
direction-independent root cause. Always use 8-way adjacency in the clearance
pass.
**Fill pass:** After BFS, flood-fill directions from routable cells into
clearance cells. Gives clearance cells valid "toward player via nearest
known-good path" direction.
**Key rule:** Corridors should be wide enough that two entity-sized objects
can pass each other (in prison-escape-game, 5 tiles = 160px for 32px
entities). At flow-field resolution (16px cells), 5 tiles = 10 cells;
clearance eats 1 each side → 8 routable cells across. Room doors must be
≥3 flow-field cells wide (48px in this calibration).

### Flow field staircase / turn smoothing

**Problem:** BFS assigns one cardinal direction per cell — entities snap
direction at every cell crossing, visible as a jerky staircase path.
**Fix:** Velocity blending in game-side ChaseSystem:
`vel += (target − vel) × (1 − exp(−turnSpeed × dt))`
High turnSpeed (≥15) ≈ snappy. Low (≤4) ≈ sluggish. 0 = instant snap (used
in tests). Config: `"turn_speed"` in entity JSON (`ai_controller` block).
Default 8.

### Coordinate convention (critical)

**Transform is CENTER-based.** `Transform.x/y` = entity center — confirmed by
`RenderSystem: transform.x - srcW * 0.5f`. All systems (CollisionSystem,
MovementSystem, FlowFieldSystem) use center coordinates. Any new spatial
system must follow this.
**FlowFieldSystem wall marking** uses `(t.x ± col.width*0.5f) / CELL_SIZE` —
center-based. Wrong convention causes enemy-stuck-on-wall bugs.
**Debug instinct:** When a spatial system misbehaves, read `RenderSystem.cpp`
and `CollisionSystem.cpp` first to verify coordinate convention. Manual
tracing wastes context.

### SteeringSystem wall repulsion

`REPULSION_STRENGTH` must be < 1.0. At ≥1.0, a lateral wall at full proximity
can flip velocity direction (entity bounces). Keep at 0.5. Max deflection ≈
arctan(0.5) ≈ 27°.

### Attack token system + slot-based positioning (enemy turn-taking)

Only N enemies (default 2, configured per-game) hold attack tokens and can
approach the player + swing. Token holders navigate directly toward the
player at 75% of attack_radius. Tokens freed on death or state change.

**Non-holders use slot-based positioning:** Each non-holder is assigned a
`slot_angle` at its current bearing from the player on entering Attack
state. It navigates to the world position
`player_pos + direction(slot_angle) * wait_radius` where wait_radius =
`attack_radius * wait_radius_mult`. Slot angles slowly rotate (CW for even
entity IDs, CCW for odd) at `SLOT_ROTATION_SPEED * orbit_speed` rad/s,
creating the "circling/sizing up" look. When a token is granted, the slot is
released (`slot_angle = -1`).

The pattern is generic — applicable to any game where multiple enemies
coordinate around a single target. Tuning (token count, rotation speed,
radii) is per-game config.

### ChaseSystem: always use flow field

ChaseSystem uses the flow field exclusively when it has a non-zero direction.
There is no direct-vector blend. A previous version blended the direct
vector in at long range (dist > 320px, directWeight → 1.0). This caused
enemies to charge through walls: the flowDotDirect suppression only caught
dot ≤ 0 (opposite vectors), not near-perpendicular cases (e.g. flow=south,
direct=east, dot=+0.14 → blend fires → enemy charges into right wall and
freezes). Direct vector is kept ONLY as a fallback when
`cell.dx == 0 && cell.dy == 0` (no BFS path).

### Animation system

Sprite sheet animation via `Animation` component + `AnimationSystem`.
**Sheet layout:** Rows = states (Idle/Walk/Attack/Hit/Death). Columns =
direction blocks x frames. Column =
`dir_index * max_frames_per_state + frame_index`. Direction enum order:
South=0, West=1, East=2, North=3.
**State resolution priority:** game-side AnimStateSystem owns this; the
engine just plays whatever row + frame the game writes into the Animation
component. Game logic (e.g. `Dead > DamageFeedback > AttackLocked > moving >
Idle` in prison-escape-game) stays game-side.
**Direction snapping:** Dominant axis wins. Ties go vertical (South). Uses
`FacingDirection.render_dx/dy`.
**Config:** JSON sidecar files in the game's `config/animations/` referenced
by entity JSON `"animation": { "sheet": "..." }`. Game-side ConfigLoader
reads the sidecar, populates Animation, and sets Sprite.texture_path.
**Timing:** Runs at wall-clock frame rate in `Engine::render()` before
RenderSystem. Not fixed-step.
**Death persistence:** `Dead.timer` set from Animation death state
(frames * duration). DeathSystem (game-side) ticks timer; entity destroyed
when timer <= 0. No-animation entities destroy instantly.
**RenderSystem guard:** Flip logic (flip_x/flip_y from FacingDirection)
skipped for entities with Animation component — direction is handled by
sheet column selection instead.

### FacingDirection: visual vs aim direction

**Problem:** Player sprite always faced the mouse cursor, making movement
look unnatural (e.g. sliding sideways when walking north while aiming east).
**Fix:** `FacingDirection` has two direction pairs:

- `dx/dy` — visual facing (drives sprite direction via `render_dx/render_dy`)
- `aim_dx/aim_dy` — targeting direction (mouse / lock-on for player; AI mirrors `dx/dy`)

Game code resolves visual facing each frame based on its own rules (e.g.
prison-escape-game has WASD-locked facing with backpedal hysteresis;
soulslike likely tracks lock-on differently). Combat systems use
`aim_dx/aim_dy`; rendering uses `dx/dy` (or `render_dx/dy` after smoothing).

The component is render-agnostic and game-agnostic — both 2D top-down twin-
stick and other input paradigms can populate it.

### Top-down depth sorting (Y-sort)

**Problem:** In top-down 3/4 view, entities further north (lower Y) should
appear behind entities further south. Without Y-sorting, overlapping sprites
look like they're standing on top of each other.
**Fix:** RenderSystem sorts draw list by: layer > foot Y > sub_layer.

- **Layer** separates categories (ground=0, campfire=1, characters=2).
- **Foot Y** (`worldY + collider.height * 0.5`) gives depth within a layer.
- **Sub-layer** breaks ties for body-part siblings (lower body=0, upper body=1).

**Critical gotcha:** All entities that should Y-sort against each other
MUST share the same layer value. If the player's body parts are on layers
2/3 and enemies are on layer 1, the layer sort takes priority and the player
always draws on top — Y-sort never fires. This manifests as the player
appearing to stand on top of enemies when north of them. The fix is putting
all character sprites (player body parts + enemies) on the same layer.

### Render interpolation (fixed-timestep wobble)

**Problem:** Fixed-timestep accumulator remainder causes entities to render
at stale positions. The remainder fluctuates each frame → visible wobble on
moving entities and camera.
**Fix:** `PreviousTransform` component snapshots position before each tick.
At render time, `render_alpha = accumulator / FIXED_TIMESTEP` blends:
`prev + (curr - prev) * alpha`. Camera uses its own `prev_x/prev_y` (not
entity PreviousTransform) so game-applied offsets (lock-on blend,
screenshake) interpolate correctly. Lock-on camera offset stored in
`Camera.offset_x/y` (separate from base position) so camera and player
share the same interpolation base. Offset interpolated and **rounded to
integers** at render time to prevent independent pixel-rounding errors
between camera and sprite from causing 1-2px oscillation. On lock-on
release, offset decays smoothly via exponential blend (`DECAY_SPEED = 12`)
in game update — never hard-zeroed (hard-zero kills interpolation →
one-frame jump).
**Critical:** Player facing is updated per-frame in the per-frame callback,
NOT in the fixed-step loop. Mouse position is a per-frame input — tying it
to the fixed tick rate causes 0-update frames where facing goes stale
(visible as wobble when mouse+WASD simultaneously).
**Pre-render callback:** Any position that must match render interpolation
(crosshair on lock-on target) must be computed in `gamePreRender`, which
runs after the tick loop with the final `render_alpha`. Engine callback
order:

1. `per_frame_update` — input/aim (before ticks)
2. Fixed-step tick loop — game systems
3. `pre_render` — interpolation-dependent state (after ticks, before render)
4. `render` — world + UI

### Multi-region terrain

Terrain is a multi-instance system. Each region has its own heightmap PNG,
world position, material, lighting environment, and footstep bank. The
game declares N regions in `terrain/config.json`; the engine builds one
mesh per region, registers each as a Jolt static trimesh body, and
applies modifiers at mesh-build time.

**Why per-region (rather than one big heightmap):** different layers of a
game's world have fundamentally different lighting + materials + sounds.
Selva's surface is outdoor warm dirt under sunlight; its underground Limbo
layer is dim grey stone in cavern darkness. Encoding both in one heightmap
forces compromises (one global PNG, one global material, one footstep
bank). Per-region keeps each layer authored independently.

**Region anatomy** (`TerrainRegion` in `world/Terrain.h`):

- **Footprint**: `world_origin: [x, z]` + `world_extent` (square XZ AABB).
- **Heightmap**: PNG sampled into `height_range_min/max` Y range, plus
  `y_offset` added at mesh-build time so the encoded range can be tiny
  and the region placed anywhere in world Y.
- **Material**: `base_color`, `tone_dark`, `tone_light` drive the
  terrain shader's per-fragment palette.
- **Lighting environment**: `sun_multiplier` (0 = no direct sun reaches),
  `sky_ambient` + `ground_ambient` (hemispheric ambient blend).
- **Footstep bank**: `footstep_sound_id` — name of an `audio.json` sound
  played when an actor's foot lands on this region.

**TerrainModifier registry is region-aware.** Modifiers have an optional
`region_name` field. When the mesh builder asks "what's Y at this XZ
vertex," it filters to modifiers matching the building region (or with
no region tag → applies to any). Lets a feature like a river trench
register once, only deforming its target region's mesh.

**Per-body lookups, not per-XZ.** Foot raycasts return the body they
hit, and each terrain body's `debug_name` is its region's name. The
footstep-bank picker reads the hit body's name and looks up the
region's `footstep_sound_id`. No XZ → region lookup, no "first region
whose AABB contains XZ wins" heuristic (which fails when regions
overlap, e.g. a smaller underground layer inside a larger surface
heightmap). Every walkable surface owns its sound bank by being the
specific body the foot hit. No default fallback sound — if a surface
plays nothing, it's an unauthored bank (bug), not "fall back to grass."

**Build-time tool**: `gen_terrain_heightmap.py --region NAME` bakes
one region's PNG from a `REGIONS` dict. Each region's `kind` selects a
generator function (`selva_surface` builds colle + wake-zone basin;
`flat` builds an empty plain whose features come entirely from runtime
modifiers).

**Real-physics doctrine applies.** Terrain isn't a separate query
system: ground Y comes from a Jolt downward raycast that sees every
terrain body equally. Whichever region's mesh the ray hits at a given
XZ is the ground.

---

## Code Organization

### Where does this helper go?

```
Is this helper used by exactly 1 system?
  YES -> anonymous namespace in that system's .cpp
  NO  -> Is it pure math/geometry (no registry access)?
    YES -> engines/engine/include/utils/<Domain>Utils.h  (namespace engine::<domain>)
    NO  -> Is it engine-level (no game types)?
      YES -> engines/engine/include/utils/ or engines/engine/src/utils/
      NO  -> games/<game>/include/ (per-game helper trees)
```

### Conventions

- **Utils = per-domain files**, not a monolithic Utils.h. Named by what they
  do: `DirectionUtils`, `ShaderUtils`, not `Helpers` or `Misc`.
- **Header-only** for small pure functions (<10 lines, constexpr/inline).
  **.h/.cpp pairs** for anything with non-trivial implementation or heavy
  includes.
- **Namespaces mirror directories**: `engine::direction` for
  `engines/engine/include/utils/DirectionUtils.h`, `engine::gl` for
  `engines/engine/include/gl/ShaderUtils.h`.
- **Systems stay thin**: ECS queries + dispatch to helpers. When a system
  file exceeds ~250 lines, look for pure-computation helpers to extract.
- **Components = pure data**: No logic, no methods beyond trivial read-only
  accessors.
- **Anonymous namespace** for file-local helpers in .cpp files. Promote to
  named-namespace utils the moment a second file needs the same function.

### Existing engine utils

- `engine::direction` (`engines/engine/include/utils/DirectionUtils.h`) —
  direction snapping, hysteresis, sprite column mapping. Used by
  AnimationSystem.
- `engine::gl` (`engines/engine/include/gl/ShaderUtils.h`) — `compileShader`,
  `buildOrtho`, `buildModel`. Used by RenderSystem, TileMapRenderer,
  UIRenderer.

---

## See also

- [3D-EXTENSION.md](3D-EXTENSION.md) — plan for how the engine grows to
  support 3D coexisting with the existing 2D path.
- [games/prison-escape-game/docs/ENGINE.md](../../games/prison-escape-game/docs/ENGINE.md)
  — that game's per-game architecture (helpers, dialogs, GameConfig split,
  registry singletons, evolution conventions).
