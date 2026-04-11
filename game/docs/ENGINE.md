# Prison Break Game — Engine Architecture

How the engine is structured, where things belong, and the hard-won patterns that
shouldn't be relearned. Reference before designing any system touching movement,
collision, or AI.

---

## Stack

C++17/20 · SDL2 (windowing/input) · OpenGL (rendering) · entt (ECS) · FMOD (audio) · CMake

**ECS:** Everything is an entity. Components are pure data. Systems are logic. Adding
content = writing a config file. Non-negotiable.

Goals: data-driven, config-driven, moddable from day one.

---

## Engine/game boundary (hard rule)

**engine/** is a reusable library with zero knowledge of this game. It provides:
- Core loop, windowing, input, rendering pipeline (SDL2 + OpenGL)
- ECS registry, generic components (Transform, Velocity, Sprite, Animation, Collider, etc.)
- Generic systems (RenderSystem, AnimationSystem, MovementSystem, CollisionSystem,
  FlowFieldSystem, CameraSystem, SteeringSystem)
- Infrastructure (TextureManager, AudioSystem, UIRenderer, TileMap data struct, TileMapRenderer)

**game/** is everything specific to this game:
- Game components (Stats, Weapon, AIController, Inventory, etc.)
- Game systems (DamageSystem, WaveSystem, LevelingSystem, CombatSystem, etc.)
- All config loading, balance, world generation, screens, menus
- Map generation logic (room placement, corridor carving, procgen pipeline)

**Rules:**
- engine/ must never `#include` any game/ header
- No game-specific constants, magic numbers, or balance values in engine/
- Engine components used as game-engine bridges (FacingDirection.sprinting, .backpedaling,
  .walk_anim_speed, .aim_dx/aim_dy) must be generic: game sets values, engine reads them.
  The engine must not know *why* these values are set.
- If a value is game-tunable, it must come from config JSON or be set by game code on a
  component -- never hardcoded in engine/
- When in doubt about where something belongs: if it references game concepts (waves, items,
  crafting, scoring, map generation, room templates), it goes in game/

---

## Engine patterns — hard-won lessons

### Collision architecture
Two separate responsibilities — keep them separate:
- **MovementSystem** — *prevents* static penetration via axis-split velocity projection.
  Tests X then Y independently before integrating. Never corrects after the fact.
- **CollisionSystem** — emits `CollisionEvent` for every overlapping pair. Corrects position
  *only* for dynamic-vs-dynamic pairs. Does not touch static-dynamic — that would double-correct.
- **Static depenetration pass** — after dyn-vs-dyn MTV resolution, a second pass sweeps each
  dynamic against all statics. Without this, MTV can push a dynamic into a wall and MovementSystem
  will zero its velocity permanently next frame.

### Corner sticking / movement inset
**Problem:** Entity freezes at doorways — adjacent tile clips it 1-2 px, zeroing velocity.
**Fix:** `MOVEMENT_INSET = 2.0f` in `MovementSystem.cpp`. Projects a `(width-2)×(height-2)` box.
Absorbs micro-clips; cannot phase through 32px walls. Increase inset if sticking returns; decrease
if thin-geometry clipping appears. Calibrated for 32×32 entities on a 32px tile grid.

### Flow field navigation
**Problem:** Direct-vector AI gets stuck behind any wall.
**Pattern:** BFS from player cell outward; each cell stores normalized direction back toward
player. AI does O(1) table read per frame. Rebuild only when player crosses into a new cell.
**Implementation:** `FlowFieldSystem` + `ChaseSystem`. Grid: 128×128 cells, 16px/cell (CELL_SIZE=16
aligns tile edges to boundaries). 8-directional BFS — diagonal steps allowed when both cardinal
intermediaries are routable (no wall, no clearance). Safe because 8-way clearance guarantees
routable cells have no wall within 1 diagonal step.
**Scalability:** BFS = O(16384 cells), once per cell crossing. Per-enemy cost = O(1).

### Flow field clearance zones and direction fill
**Problem:** BFS paths along wall edges cause movement zeroing. Clearance cells have no BFS
direction and fall back to direct-vector, which points into walls at corners.
**Clearance zone:** Before BFS, mark every open cell adjacent to a wall as clearance (impassable
to BFS). Adjacency is **8-directional (cardinal + diagonal)**. Cardinal catches cells beside wall
faces; diagonal catches cells at wall corners. Without diagonal, a cell just outside a door jamb
corner passes the 4-way test but a 32px entity still clips the corner → MovementSystem blocks it
→ entity freezes. This manifests as a direction-specific bug ("works south, breaks north") with a
direction-independent root cause. Always use 8-way adjacency in the clearance pass.
**Fill pass:** After BFS, flood-fill directions from routable cells into clearance cells. Gives
clearance cells valid "toward player via nearest known-good path" direction.
**Key rule:** Corridors are 5 tiles wide (160px) so two 32px entities can pass each other
comfortably. At flow-field resolution (16px cells), 5 tiles = 10 cells; clearance eats 1 each
side → 8 routable cells across. Room doors must be ≥3 flow-field cells wide (48px).

### Flow field staircase / turn smoothing
**Problem:** BFS assigns one cardinal direction per cell — entities snap direction at every cell
crossing, visible as a jerky staircase path.
**Fix:** Velocity blending in `ChaseSystem`:
`vel += (target − vel) × (1 − exp(−turnSpeed × dt))`
High turnSpeed (≥15) ≈ snappy. Low (≤4) ≈ sluggish. 0 = instant snap (used in tests).
Config: `"turn_speed"` in entity JSON (`ai_controller` block). Default 8.

### Coordinate convention (critical)
**Transform is CENTER-based.** `Transform.x/y` = entity center — confirmed by
`RenderSystem: transform.x - srcW * 0.5f`. All systems (CollisionSystem, MovementSystem,
FlowFieldSystem) use center coordinates. Any new spatial system must follow this.
**FlowFieldSystem wall marking** uses `(t.x ± col.width*0.5f) / CELL_SIZE` — center-based.
Wrong convention causes enemy-stuck-on-wall bugs.
**Debug instinct:** When a spatial system misbehaves, read `RenderSystem.cpp` and
`CollisionSystem.cpp` first to verify coordinate convention. Manual tracing wastes context.

### SteeringSystem wall repulsion
`REPULSION_STRENGTH` must be < 1.0. At ≥1.0, a lateral wall at full proximity can flip
velocity direction (entity bounces). Keep at 0.5. Max deflection ≈ arctan(0.5) ≈ 27°.

### Attack token system + slot-based positioning (enemy turn-taking)
Only N enemies (default 2, configured via `combat_ai.max_attack_tokens`) hold attack tokens and
can approach the player + swing. Token holders navigate directly toward the player at 75% of
attack_radius. Tokens freed on death or state change.

**Non-holders use slot-based positioning:** Each non-holder is assigned a `slot_angle` at its
current bearing from the player on entering Attack state. It navigates to the world position
`player_pos + direction(slot_angle) * wait_radius` where wait_radius =
`attack_radius * combat_ai.wait_radius_mult`. Slot angles slowly rotate (CW for even entity IDs,
CCW for odd) at `SLOT_ROTATION_SPEED * orbit_speed` rad/s, creating the "circling/sizing up" look.
When a token is granted, the slot is released (`slot_angle = -1`).

**Components:** `AttackTokenPool` (ctx singleton), `AIController.token_cooldown`,
`AIController.slot_angle` (-1 = no slot), `AIController.orbit_speed` (slot rotation multiplier).
**Config:** `config/balance/formulas.json` `"combat_ai"` block, loaded into `FormulaConfig.combat_ai`.
**Per-entity rotation speed:** `AIController.orbit_speed` (default 0.5, skeleton 1.0) controls
slot rotation speed. Set in entity JSON `ai_controller.orbit_speed`. Higher values = faster
repositioning. Foundation for Flank ability (issue #53).

### ChaseSystem: always use flow field
ChaseSystem uses the flow field exclusively when it has a non-zero direction. There is no
direct-vector blend. A previous version blended the direct vector in at long range (dist >
320px, directWeight → 1.0). This caused enemies to charge through walls: the flowDotDirect
suppression only caught dot ≤ 0 (opposite vectors), not near-perpendicular cases (e.g.
flow=south, direct=east, dot=+0.14 → blend fires → enemy charges into right wall and freezes).
Direct vector is kept ONLY as a fallback when `cell.dx == 0 && cell.dy == 0` (no BFS path).

### Animation system
Sprite sheet animation via `Animation` component + `AnimationSystem`.
**Sheet layout:** Rows = states (Idle/Walk/Attack/Hit/Death). Columns = direction blocks x frames.
Column = `dir_index * max_frames_per_state + frame_index`. Direction enum order: South=0, West=1,
East=2, North=3.
**State resolution priority:** Dead > DamageFeedback(Hit) > AttackLocked(Attack) > velocity!=0(Walk) > Idle.
**Direction snapping:** Dominant axis wins. Ties go vertical (South). Uses `FacingDirection.render_dx/dy`.
**Config:** JSON sidecar files in `config/animations/` referenced by entity JSON `"animation": { "sheet": "..." }`.
`ConfigLoader::loadAnimation()` reads the sidecar, populates Animation, and sets Sprite.texture_path.
**Timing:** Runs at wall-clock frame rate in `Engine::render()` before RenderSystem. Not fixed-step.
**Death persistence:** `Dead.timer` set from Animation death state (frames * duration). `DeathSystem`
ticks timer; entity destroyed when timer <= 0. No-animation entities destroy instantly.
**RenderSystem guard:** Flip logic (flip_x/flip_y from FacingDirection) skipped for entities with
Animation component — direction is handled by sheet column selection instead.

### WASD-locked facing (player visual direction)
**Problem:** Player sprite always faced the mouse cursor, making movement look unnatural (e.g.
sliding sideways when walking north while aiming east).
**Fix:** `FacingDirection` has two direction pairs: `dx/dy` (visual facing, drives sprite
direction via `render_dx/render_dy`) and `aim_dx/aim_dy` (targeting direction, always tracks
mouse or lock-on). `gamePerFrame()` in GameLoop.cpp resolves visual facing each frame:
- **Moving normally:** sprite faces the WASD direction or its exact opposite (backpedal).
  No sideways facing. Dot product of WASD vs aim determines forward vs backpedal
  (`BACKPEDAL_DOT = -0.15`). `render_dx/render_dy` snaps instantly (no blend delay).
- **Backpedaling:** aim is roughly opposite to movement → sprite faces opposite of WASD
  direction. Walk animation plays in reverse via `facing.backpedaling` flag.
- **Attacking** (`AttackLocked`/`CriticalAttacking`), **held ranged fire**, **lock-on**,
  or **stationary:** sprite faces aim direction (mouse/lock-on). `render_dx/render_dy`
  blends smoothly (25%/frame) to prevent mouse jitter.
**Combat systems** (CombatSystem, DamageSystem) use `aim_dx/aim_dy` for hitbox placement,
projectile direction, dodge, shield frontal arc, and backstab detection.
**AI entities** keep `aim_dx/aim_dy` synced to `dx/dy` (no separate aim concept).
**Keyboard polling:** `gamePerFrame()` polls `SDL_GetKeyboardState` directly for WASD instead
of reading `PlayerActions.move_x/move_y` (which lags by one tick from InputMappingSystem).

### Top-down depth sorting (Y-sort)
**Problem:** In top-down 3/4 view, entities further north (lower Y) should appear behind
entities further south. Without Y-sorting, overlapping sprites look like they're standing
on top of each other.
**Fix:** RenderSystem sorts draw list by: layer > foot Y > sub_layer.
- **Layer** separates categories (ground=0, campfire=1, characters=2).
- **Foot Y** (`worldY + collider.height * 0.5`) gives depth within a layer.
- **Sub-layer** breaks ties for body-part siblings (lower body=0, upper body=1).
**Critical gotcha:** All entities that should Y-sort against each other MUST share the same
layer value. If the player's body parts are on layers 2/3 and enemies are on layer 1, the
layer sort takes priority and the player always draws on top — Y-sort never fires. This
manifests as the player appearing to stand on top of enemies when north of them. The fix is
putting all character sprites (player body parts + enemies) on the same layer (2).
**Layer assignments:** wall=0, campfire/rest_spot=1, all characters (player parts + enemies)=2.

### Render interpolation (fixed-timestep wobble)
**Problem:** Fixed-timestep accumulator remainder causes entities to render at stale positions.
The remainder fluctuates each frame → visible wobble on moving entities and camera.
**Fix:** `PreviousTransform` component snapshots position before each tick. At render time,
`render_alpha = accumulator / FIXED_TIMESTEP` blends: `prev + (curr - prev) * alpha`.
Camera uses its own `prev_x/prev_y` (not entity PreviousTransform) so game-applied offsets
(lock-on blend, screenshake) interpolate correctly. Lock-on camera offset stored in
`Camera.offset_x/y` (separate from base position) so camera and player share the same
interpolation base. Offset interpolated and **rounded to integers** at render time to prevent
independent pixel-rounding errors between camera and sprite from causing 1-2px oscillation.
On lock-on release, offset decays smoothly via exponential blend (`DECAY_SPEED = 12`) in
`gameUpdate` — never hard-zeroed (hard-zero kills interpolation → one-frame jump).
Facing dot position integer-rounded.
**Critical:** Player facing is updated per-frame in `gamePerFrame()`, NOT in the fixed-step
loop. Mouse position is a per-frame input — tying it to the fixed tick rate causes 0-update
frames where facing goes stale (visible as wobble when mouse+WASD simultaneously).
**Pre-render callback:** Any position that must match render interpolation (crosshair on
lock-on target) must be computed in `gamePreRender`, which runs after the tick loop with the
final `render_alpha`. Engine callback order:
1. `per_frame_update` — input/aim (before ticks)
2. Fixed-step tick loop — game systems
3. `pre_render` — interpolation-dependent state (after ticks, before render)
4. `render` — world + UI

---

## Code Organization

### Where does this helper go?

```
Is this helper used by exactly 1 system?
  YES -> anonymous namespace in that system's .cpp
  NO  -> Is it pure math/geometry (no registry access)?
    YES -> engine/include/utils/<Domain>Utils.h  (namespace engine::<domain>)
    NO  -> Is it engine-level (no game types)?
      YES -> engine/include/utils/ or engine/src/utils/
      NO  -> game/include/ (like SpawnUtils.h, InventoryOps.h)
```

### Conventions
- **Utils = per-domain files**, not a monolithic Utils.h. Named by what they do: `DirectionUtils`,
  `ShaderUtils`, not `Helpers` or `Misc`.
- **Header-only** for small pure functions (<10 lines, constexpr/inline).
  **.h/.cpp pairs** for anything with non-trivial implementation or heavy includes.
- **Namespaces mirror directories**: `engine::direction` for `engine/include/utils/DirectionUtils.h`,
  `engine::gl` for `engine/include/gl/ShaderUtils.h`.
- **Systems stay thin**: ECS queries + dispatch to helpers. When a system file exceeds ~250 lines,
  look for pure-computation helpers to extract.
- **Components = pure data**: No logic, no methods beyond trivial read-only accessors.
- **Anonymous namespace** for file-local helpers in .cpp files. Promote to named-namespace utils
  the moment a second file needs the same function.

### Existing utils
- `engine::direction` (`engine/include/utils/DirectionUtils.h`) -- direction snapping, hysteresis,
  sprite column mapping. Used by AnimationSystem.
- `engine::gl` (`engine/include/gl/ShaderUtils.h`) -- compileShader, buildOrtho, buildModel.
  Used by RenderSystem, TileMapRenderer, UIRenderer.

### Game-side helpers (in `game/include/ops/` + `game/src/ops/`)
- `InventoryOps` (`ops/InventoryOps.h`) -- inventory add/remove, equip/unequip,
  item counting, material consumption, evolution execution (canEvolve, evolveWeapon).
- `SpawnUtils` (`ops/SpawnUtils.h`) -- enemy spawn logic.
- `CraftingOps` (`ops/CraftingOps.h`) -- crafting recipe execution.

### Reusable dialog utilities (in `game/include/screens/`)
Two auto-sizing dialog templates -- measure content first, compute panel dimensions from
measurements. Never hardcode panel sizes.

- **ConfirmDialog** (`screens/ConfirmDialog.h`) -- centered Yes/No confirmation popup.
  `Options`: title, body_lines (vector), title_font, body_font, selection pointer, min_width.
  Returns `Result::Yes`, `Result::No`, or `Result::None`. Handles keyboard (Y/N, Left/Right,
  Enter, Escape) and mouse.
- **MenuDialog** (`screens/MenuDialog.h`) -- centered option-list dialog with labels + descriptions.
  `Options`: title, items (vector of `{label, description, enabled}`), hint text, title_font,
  body_font, selection pointer, close_on_escape, close_on_rmb, darken_background.
  Returns `Result{selected, dismissed}`. Handles keyboard (Up/Down, Enter, Escape) and mouse
  (hover to select, click to activate, RMB to dismiss).

**Pattern:** caller builds an `Options` struct, calls `render()`, reads the result.
The dialog measures all text content and sizes the panel to fit. Used by SanctuaryScreen
(MenuDialog) and LoadGameScreen (ConfirmDialog).

**When building new dialogs:** Always use these templates or follow the same measure-first
pattern. Never hardcode pixel dimensions for dialog panels.

**Footer button standard:** All footer buttons use bottom-up layout anchored to the panel edge:
- 20px clearance from panel bottom to button bottom edge
- Button internal padding: 30px horizontal (each side), 10px vertical (each side)
- `btn_h = text.height + 20.0f`, `btn_w = text.width + 60.0f`
- `btn_y = panel_bottom - 20.0f - btn_h`
- Text drawn at `(btn_x + 30, btn_y + 10)`
- Never position footer buttons top-down from hardcoded offsets (e.g. `panel_h - 60`).

### GameConfig.h split (in `game/include/ecs/`)
`GameConfig.h` is an umbrella header. New code should include only what it needs:
- `BalanceConfig.h` -- FormulaConfig, SoundConfig, MusicConfig, WaveConfig, WaveState
- `ItemConfig.h` -- ItemDef, ItemRegistry, RecipeRegistry, WeaponTierRegistry,
  EvolutionRegistry, Compendium, Rarity, qualityName
- `AppState.h` -- UIState, GameState, RunStats, ScoringConfig, SaveData

### Registry / config singletons (in `entt::registry::ctx()`)
- `FormulaConfig` -- all balance constants (formulas.json)
- `SoundConfig`, `MusicConfig` -- audio mappings
- `WaveConfig`, `WaveState` -- wave rules and runtime state
- `ItemRegistry` -- all item definitions keyed by config_path
- `RecipeRegistry` -- crafting recipes
- `WeaponTierRegistry` -- per-class growth defaults (dagger, sword, club)
- `EvolutionRegistry` -- weapon evolution trees + reverse lookup (weapon → family + node)
- `Compendium` -- discovered items (persistent across runs)
- `UIState` -- active screen/tab tracking
- `GameState` -- top-level app state (MainMenu, Playing, GameOver, etc.)
- `RunStats` -- accumulated run statistics
- `ScoringConfig` -- score formula weights
- `SaveData` -- persistent save data (characters, runs)

### Evolution tree file conventions
- One file per weapon class: `config/evolution/blades.json`, `bludgeons.json`, `ranged.json`
- Named by weapon class, not by root weapon
- Cross-tree evolution via optional `"target_tree"` field (not yet implemented)
- ConfigLoader scans the directory -- adding a tree = adding a JSON file
