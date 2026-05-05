# Engine 3D Extension Plan

How this engine grows to support a 3D soulslike (**Selva Oscura**) alongside
the existing 2D top-down action roguelike (**prison-escape-game**). Living
document — design, not implementation. Update as decisions firm up or get
overturned by reality.

> Sibling docs:
> - [engines/engine/docs/ENGINE.md](./ENGINE.md) — engine doctrine,
>   engine/game boundary, render-callback model.
> - [engines/engine/docs/TECH-DEBT.md](./TECH-DEBT.md) — open structural
>   issues at the engine level.
> - [games/prison-escape-game/docs/ENGINE.md](../../../games/prison-escape-game/docs/ENGINE.md)
>   — that game's per-game engine notes (helper trees, dialog templates,
>   evolution tree conventions).

## Visual direction (locked 2026-05-05)

Selva Oscura is **pure 3D** — Souls/Elden Ring camera, locomotion, combat —
with a **1-bit visual treatment delivered by shaders**, not by sprite art:

- Real 3D meshes with skeletal animation. **No billboarded sprites, no
  HD-2D, no 2.5D.**
- Dither-pattern shading (Bayer / blue-noise) quantizes lighting to discrete
  levels, à la *Return of the Obra Dinn*.
- Optional threshold post-process for true 1-bit B&W or 2-bit 4-level.
- Optional depth+normal edge-detection pass for woodcut-style outlines —
  the Doré / Botticelli illustration tradition the *Commedia* belongs to.
- Render at native resolution (the 1-bit feel comes from shader math, not
  from rendering small and upscaling).

Reference points: *Return of the Obra Dinn* (closest match), *Lorn's Lure*,
*World of Horror* (visual register only — austere monochrome composition).

This direction collapses what would otherwise have been a permanent
skeletal-vs-spritesheet schism in the engine: both games converge on real
3D / skeletal animation as the long-term path; only the **shader** differs.

## Status (as of 2026-05-05)

What's already in place beyond milestone 2 below:

- **Repo restructured to multi-game / multi-engine layout** (PR #119). The
  doc's milestone-0 "promote `game/` to multi-game repo" predicted this.
- **`Engine::setRenderWorld(fn)` callback** (PR #121). Replaces the doc's
  milestone-0 "split engine into render2d/render3d/etc. CMake modules"
  plan. A single callback owned by the game gives full control of world
  rendering — 2D games call `TileMapRenderer` + `RenderSystem` here; 3D
  games run their own pipeline (geometry → dither/threshold/outline
  post-process → blit). The engine no longer has any opinion about render
  paradigm. CMake-level module split deferred indefinitely; the callback
  shape gets us the same boundary clarity with one tenth the plumbing.
- **Engine clears color + depth buffers**, depth test enabled at startup.
  2D paths write `Z=0` so depth test is a no-op for them; 3D paths get
  depth-buffering for free.
- **Selva Oscura exe boots and renders a tumbling 3D cube** via custom
  shader, VBO, EBO, VAO with glm-driven MVP — the milestone-2 cube target.
  No glTF loader yet; geometry hard-coded.
- **glm fetched** as a header-only system dep. cgltf, Jolt, ozz-animation,
  Recast/Detour — all still pending per the relevant milestones.

Sections 2 (the A/B/C architecture choice) and 6 (milestone 0–6) below are
the historical plan and remain mostly accurate, but milestone 0's specific
"split into engine/core, engine/render2d, engine/render3d CMake modules"
shape is **superseded by the renderWorld callback**. Re-evaluate before
acting on milestone 0 specifics.

---

## 1. Premise and constraint

The engine already supports the **combat backend** for a soulslike: stamina,
hurtbox/hitbox with multi-shape priority, hit-pause (Hitstop), lock-on, dodge
with i-frames, parry → RiposteWindow → critical, poise, staggered, attack
tokens with slot-based circling, weapon scaling (STR/DEX), per-hand weapon XP,
equipment, crafting, save manager. None of that is 2D-coupled. It's all data on
ECS components and logic in stateless systems that consume that data.

What's coupled to 2D:

- Rendering: `Sprite`, `RenderSystem`, `TileMapRenderer`, `UIRenderer`, sprite
  Y-sort, sprite paper-doll compositor, `SpriteCompositor`.
- Spatial primitives: `Transform.x/y/rotation`, `Collider.width/height` (AABB),
  `CollisionShape` (2D AABB/Circle/Capsule), `MovementSystem` axis-split.
- Navigation: `FlowField` (320×240 grid, BFS on a tilemap).
- Animation: spritesheet rows × columns × cardinal directions; `AnimationSystem`
  has zero notion of skeletons.
- Camera: orthographic, top-down, single-axis scrolling with screen-shake/lock-on
  offset.
- Input → world projection: mouse-aim is a 2D world-space coordinate.

So the question is **how the engine accommodates a second render path**,
character controller, and animation pipeline, while keeping every shared
combat/RPG/AI system intact and unaware of which path is active.

The hard rule is unchanged: `engines/engine/` never `#include`s any
`games/*/` header. That rule extends to: each game
(`games/prison-escape-game/`, `games/selva-oscura/`) only includes the
engine, never each other.

---

## 2. Architectural shape (the big choice)

Three plausible options. I'm picking option **C** with a caveat from B.

### A. One binary, both render paths registered, runtime-selected

Engine ships both `Renderer2D` and `Renderer3D`. A startup config selects which
one's systems get registered.

- **Upside:** One build artefact. Easy to share dev tooling between games.
- **Downside:** Game binaries that will never need 3D still link a 3D renderer
  (Jolt, ozz, glTF, glm, all pulled in). Bloat creeps and conditional code
  multiplies.
- **Reject.** Violates the "every system is built for the worst case from day
  one" principle by accreting both worst cases into one binary. Bad smell.

### B. Renderer-as-module, game's CMake links the renderer it needs

Engine becomes a set of CMake targets:
`engine_core`, `engine_render2d`, `engine_render3d`, `engine_combat`,
`engine_navigation_2d`, `engine_navigation_3d`. Each game's `CMakeLists.txt`
links exactly the modules it needs.

- **Upside:** Clean dependency graph. prison-break-game still links only what
  it links today. Selva Oscura doesn't pay for `RenderSystem`'s sprite quad
  pipeline.
- **Downside:** More CMake plumbing. Modules need explicit interface headers.
- **Strong candidate.** This is the right long-term shape if there will be more
  than one game. The engine becomes a kit.

### C. Multi-game repo with per-game executable, shared engine library

Promote the repo from "prison-break-game with engine inside" to "engine + N
games":

```
prison-break-game/             # repo root keeps the name for now
  engine/                      # the kit
    core/                      # ECS, EntityManager, fixed-step loop, math
    render2d/                  # Sprite, RenderSystem, UIRenderer, TileMapRenderer
    render3d/                  # Mesh, Material, Camera3D, Renderer3D (NEW)
    nav2d/                     # FlowField, FlowFieldSystem
    nav3d/                     # navmesh + agent system (NEW, much later)
    combat/                    # Hurtbox, hit resolver, hitstop, attack tokens
    rpg/                       # Stats, Experience, Inventory, Equipment
    audio/                     # AudioSystem, FMOD glue
    input/                     # PlayerActions schema, InputMappingSystem
  game-prison-break/           # the existing game, content-only consumer
  game-selva-oscura/           # the new game (NEW)
  cmake/
  tests/
```

Each game is a separate executable. Each game `target_link_libraries` only the
engine modules it needs. This is option B, made concrete by also separating the
games at the directory level.

- **Upside:** Forces clean engine module boundaries. Two games living side-by-side
  catches "engine quietly assumes 2D" leaks immediately. Game-specific test
  suites stay scoped. Eventual open-source story: extract `engine/` as its own
  repo when ready, with example games as submodules or separate repos.
- **Downside:** Requires a moving day. Existing `game/` tree gets renamed to
  `game-prison-break/`. A bunch of paths in CMake and scripts shift. Not hard,
  just mechanical. Save data path (`%APPDATA%/PrisonBreakGame/`) is unaffected
  because it's set in code.
- **Caveat from option A I'm keeping:** the engine `core` module is still one
  library. Don't fragment ECS, math, fixed-step loop, or `EntityManager` —
  those are the spine. Modularity is at the *system* and *component family*
  level, not the foundational ECS level.

**Decision: option C.** The kit shape with a multi-game repo. We don't have to
do the `game/` → `game-prison-break/` rename in step one — milestone 1 can keep
the current layout and just introduce engine submodules. But the design assumes
this end state.

---

## 3. Component evolution

Three buckets: **stays shared**, **evolves in place**, **forks into 2D/3D variants**.

### Stays shared (no change)

Everything that's pure data about combat, RPG, items, AI, audio, lifecycles:

| Component | Why unchanged |
|---|---|
| `Health`, `Stats`, `Experience`, `Body`, `Stamina`, `Poise`, `Essence` | Pure scalars |
| `Hurtbox`, `HurtShape`, `HitRecord` | `CollisionShape` evolves; the wrapper is fine |
| `Weapon`, `LeftWeapon`, `Shield`, `WeaponHitboxShape`, `RangedState` | Weapon-local space; only the local→world transform changes |
| `Hitbox`, `Dodging`, `AttackLocked`, `Staggered`, `Parrying`, `RiposteWindow`, `CriticalAttacking`, `CriticalTarget`, `LockOnTarget` | Pure timers + flags |
| `DamageFeedback`, `AttackFeedback` | Pure timers |
| `Hitstop`, `AttackTokenPool`, `AttackIdCounter` | ctx singletons |
| `AIController` | Soulslike-shaped already; slot-based circling carries over to 3D directly |
| `Inventory`, `Equipment`, `ItemInstance`, `Wallet`, `Loot`, `DropEntry`, `Pickup` | RPG state |
| `Tag`, `Dead`, `PendingDestroy` | Lifecycle |
| `HitSound`, `DeathSound`, `AmbientSound`, `AggroSound` | Audio |
| `PlayerActions` | The input *contract* doesn't change; what populates `mouse_world_x/y` changes |

### Evolves in place (same component, new fields, 2D games keep using a subset)

| Component | Change | 2D game behavior |
|---|---|---|
| `Transform` | Add `z`, `pitch`, `roll`. `rotation` becomes yaw. `scale` becomes `vec3 scale`. | 2D systems read `x/y` and yaw; ignore `z`/pitch/roll. Default `z=0`, scale stays scalar via accessor. |
| `PreviousTransform` | Mirror the same fields | Same |
| `Velocity` | Add `dz` | 2D systems ignore `dz` |
| `MovementIntent` | Add `dz` (jump intent) | 2D ignores |
| `FacingDirection` | `dx/dy/dz` for visual facing; `aim_dx/dy/dz`. Render-direction snapping (`render_dx/dy`) becomes 2D-only and lives in a dedicated `Sprite2DFacing` component | 2D code reads `Sprite2DFacing` for cardinal snapping; 3D code reads the raw 3D vector |
| `CollisionShape` | Promote to 3D primitives: `AABB3D`, `Sphere`, `Capsule3D` (line + radius). 2D primitives stay as `AABB2D`, `Circle`, `Capsule2D`. Tagged union grows from 3 to 6. | 2D code only emits/consumes 2D primitives. Discriminator enum keeps it honest. |
| `Camera` | Becomes a base struct; `Camera2D` and `Camera3D` carry mode-specific fields (zoom vs FOV, ortho size vs near/far planes, screen offset vs follow-target offset). The base keeps `prev_x/y/z` and `active`. | 2D code emplaces `Camera2D` only |

The "evolves in place" bucket is where most of the design care goes. The
temptation is to fork everything, but `Transform` getting a `z` is a small
honest change; forking it into `Transform2D`/`Transform3D` would duplicate
every system that reads position. Better to widen the type and let 2D games
ignore the extra fields.

The exception is `CollisionShape`. The 2D primitives have no meaningful 3D
extension (a capsule in 2D is a swept circle; a capsule in 3D is a swept
sphere — different math entirely), and the resolver needs different code
paths. So `CollisionShape` is widened with new tags but the *resolver* code
is split. See §4.

### Forks into 2D-only / 3D-only

| 2D-only | 3D-only |
|---|---|
| `Sprite`, `Sprite2DFacing`, `Glow`, `SolidColor`, `TileMap`, `FlowField`, `CameraPan` (probably 3D-friendly later) | `Mesh`, `Material`, `SkeletonInstance`, `AnimationGraph`, `NavMeshAgent`, `RigidBody3D` |
| `Animation` (spritesheet) | `SkeletalAnimation`, `AnimationClip` |
| `WeaponSprite` | `WeaponMesh` (analogous: a child entity with a mesh, parented to a hand bone) |
| `AppearanceState` (paper-doll) | `MaterialOverrideSet` (per-bone material swaps for armor) |

The shared idea — "an entity has a visual representation that needs runtime
overrides" — survives, but the components are different shapes because the
data is genuinely different (UV rects vs bone weights, sheet rows vs animation
clips).

---

## 4. System fork list

Three buckets again: **unchanged**, **extended (one body, dispatches on
component presence)**, **forked (2D and 3D versions are different files)**.

### Unchanged

Combat, RPG, AI, audio, lifecycle, input. Everything in `game/src/systems/` that
isn't movement, collision, or rendering ports forward as-is. Specifically:

- `DamageSystem`, `HitboxResolverSystem` (the hitbox→hurtbox resolution math
  changes shape with 3D primitives, but the *system* is still "iterate active
  hitboxes, resolve against hurtboxes, emit damage, dedup by attack_id")
- `AggroSystem`, `ChaseSystem` (3D version uses navmesh agent instead of
  flow field, but the *behavior* is identical: "produce a velocity toward
  target through walkable space")
- `CombatSystem`, `EquipmentSystem`, `WeaponXPSystem`, `LevelingSystem`
- `DeathSystem`, `PickupSystem`, `LootSystem`
- `WaveSystem`, `SpawnerSystem` (game-specific anyway)
- `AudioSystem`, `AmbientSoundSystem`
- `InputMappingSystem` (different inputs per game, but the abstraction holds)

### Extended (single system, dispatches on data)

- `HitboxResolverSystem`. Takes a `WeaponHitboxShape` in weapon-local space and
  transforms to world. The transform path forks:
  - 2D: rotate by yaw around weapon origin, translate to attacker + grip.
  - 3D: transform by attacker world matrix × hand-bone matrix × weapon-local
    matrix.
  Resolution against a `Hurtbox` similarly forks at the primitive-test level.
  This is the hardest "extended" call. It might end up forking. Decide during
  implementation.
- `AnimationSystem`. The current sprite-row state machine isn't a 3D fit. But
  the *idea* — game writes "play X" into a component, engine ticks it — holds.
  Likely we end up with two systems sharing an interface: `SpriteAnimationSystem`
  (current) and `SkeletalAnimationSystem` (new). Both read from a game-side
  state component (`AnimRowConfig` for 2D, an analogous `AnimGraphConfig` for
  3D) and write into a render-facing component. Treat as forked.

### Forked (separate files, separate engine modules)

| 2D | 3D |
|---|---|
| `MovementSystem` (axis-split AABB-vs-tilemap) | `MovementSystem3D` (capsule vs static collision via physics) |
| `CollisionSystem` (AABB pairs + dyn-vs-dyn MTV) | physics solver step (Jolt-driven) |
| `RenderSystem` (sprite quads, Y-sort) | `Renderer3D` (mesh draw, depth buffer, dithered post-process) |
| `TileMapRenderer` | n/a (replaced by mesh terrain / level chunks) |
| `FlowFieldSystem` | navmesh build (offline or load-time; runtime A* on agents) |
| `CameraSystem` (snap to player) | `OrbitCameraSystem` (third-person yaw/pitch around target) |
| `SteeringSystem` (wall repulsion, crowd avoidance — only valid in 2D grid space) | physics-driven separation (Jolt soft constraints) or a 3D port if needed |
| `AnimationSystem` (sprite rows) | `SkeletalAnimationSystem` (clip blending → bone palette) |
| `WeaponSpriteSystem` (anchor lerp, frame matrix) | `WeaponMeshSystem` (parent to hand bone, apply weapon-local offset) |
| `SpriteCompositor` (paper-doll layer composite) | n/a (3D paper-doll = swap meshes/materials per slot) |

---

## 5. Vendor vs build line

Strong defaults below. Re-evaluate each at decision time.

### Vendor (solved problems with hard math)

- **Physics: Jolt.** Modern, well-documented, battle-tested in Horizon: Forbidden
  West, used in Godot 4. Better fit than Bullet for new code; Bullet's API is
  showing its age. PhysX is ruled out (proprietary, Nvidia-tied, heavier
  integration).
- **Skeletal animation: ozz-animation.** Decoupled from any engine, plain C++,
  used widely (including in commercial engines). Loads its own runtime format;
  we'd write a glTF → ozz converter at asset-pipeline time.
- **glTF parsing: cgltf.** Header-only, zero dependencies, reads both .gltf and
  .glb. The standard choice.
- **3D math: glm.** The default. Header-only, well-known API, GLSL-shaped. Don't
  even consider rolling our own; that's a multi-month rabbit hole with no
  payoff.
- **Image decoding: stb_image** (already vendored).
- **Audio: FMOD** (already vendored).
- **Text rendering: stb_truetype** (already vendored via FontManager). Stays.

### Build (where game-design opinions live)

- **ECS shape and EntityManager.** Already ours. Stays ours.
- **Render-system architecture.** How draw calls are dispatched, batched, and
  ordered. How render passes compose (geometry → outline → dither → UI).
  Building this myself is the whole point of an "engine person" path. The
  3D pipeline is where the 1-bit-soulslike aesthetic gets made; vendoring a
  full renderer (bgfx, sokol, the_forge) trades that creative surface for
  generic capability. **Vendor decision flag:** if build-out time on the
  renderer becomes the critical path on Selva Oscura's first vertical slice,
  reconsider sokol-gfx as a thin abstraction layer, but only then.
- **Game loop.** Fixed-timestep + render interpolation alpha is feel-defining.
  Ours stays.
- **Asset pipeline.** JSON entity templates, sidecar animation configs,
  FetchContent for source dependencies. Ours stays. We'll add a model-import
  step (glTF → internal mesh/material/skeleton bundle).
- **Combat-feel systems.** Hitstop, attack tokens, slot circling, parry/riposte
  windows, lock-on smoothing. All ours. None of it gets vendored.
- **Navigation.** 2D flow field stays ours. 3D navmesh: vendor **Recast/Detour**
  for the offline navmesh build and runtime A*; the *behavior* layer
  (AIController, ChaseSystem) is still ours.

### Re-evaluation triggers

- Renderer build-out blocks Selva Oscura's first vertical slice for >2 milestones
  → consider sokol-gfx for the dispatch layer (still keep the pass composition
  ours).
- Recast/Detour proves too heavy for the "dark wood" hub world's geometry →
  consider a simpler grid-based navmesh.
- Jolt's API friction in the ECS → consider Bullet (more mature wrapper
  ecosystem) only if Jolt integration is genuinely painful, not just
  unfamiliar.

---

## 5b. Animation strategy — hybrid: procedural now, skeletal later

Locked-in 2026-05-05. Selva Oscura ships with **procedural animation as the
implementation today** and **skeletal animation as a future driver behind
the same interface**. Both paths plug into one `AnimationDriver`
abstraction; combat / movement / interaction code targets the abstraction
and never reaches into either implementation directly. When skeletal
lands, the dodge / attack / parry / hit-react state machines stay
unchanged — only the per-state driver implementation swaps.

### Why hybrid (not pure skeletal now)

Three reasons, in order of how much they matter:

1. **We don't yet know what the animation system needs to support.**
   Combat will surface real requirements as it lands: layered animation
   (upper-body swing while lower-body walks), animation-driven hitbox
   placement, cancel windows, blend-out interruptions when a swing eats a
   parry. Building skeletal infrastructure before those requirements are
   visible bakes assumptions that combat will then have to fight. The
   abstraction shape is informed by 5–10 concrete users, not by upfront
   prediction.

2. **Procedural is not strictly worse for Selva Oscura's visual register.**
   Souls is skeletal because Souls is hyperreal. Selva Oscura's
   1-bit/woodcut aesthetic doesn't demand realistic motion-captured human
   movement; a more deliberate, slightly-stylized procedural feel may be
   *more* in keeping with the visual register (Doré woodcuts don't
   animate; their stillness is part of their power). Reference points:
   *Disco Elysium* (zero character animation), *Untitled Goose Game*
   (procedural body + IK), *Genesis Noir* (entirely procedural).

3. **Skeletal animation has a real asset pipeline cost that gates content
   velocity.** Mesh modeling, rigging, weight painting, glTF export,
   ozz conversion, retargeting from Mixamo or hand-keyframed animation —
   every new enemy, weapon, or move expands this pipeline. For a solo
   dev, this is the thing that kills indie projects after combat starts
   working: mechanics are great but content can't keep up. Procedural
   has zero asset pipeline; new enemy = new state machine function.

### When skeletal lands

The migration is not a question of *if*, only *when* — gated on:

- The `AnimationDriver` abstraction having ~5–10 concrete procedural
  users (dodge, light attack, heavy attack, parry, riposte, hit reaction,
  death, ambient idle drift, basic locomotion). At that point the API is
  battle-tested.
- A specific creative need that procedural genuinely cannot deliver
  (e.g. an intricate finishing-move animation that requires per-bone
  keyframing, or a boss whose visual character is inseparable from
  motion-captured movement).
- Bandwidth to pay the asset-pipeline cost without stalling the rest of
  the project.

It's possible Selva Oscura ships entirely procedural. That's a fine
outcome if the creative result is right. The hybrid plan does not
*commit* to skeletal — it keeps the door open and the architecture
clean for it.

### What the abstraction looks like

`AnimationDriver` is a small interface a combat / movement state machine
can target without caring about implementation:

- **Inputs**: a state identifier (e.g. `AnimState::DodgeRoll`,
  `AnimState::LightAttack1`), a normalized phase progress in [0, 1],
  and any per-state parameters (roll direction, attack tier, etc.).
- **Outputs per frame**: a transform offset to apply to the entity
  (translation, rotation, scale offsets relative to the entity's current
  position/yaw), and optional bone overrides (no-op for procedural
  drivers; populated by the skeletal driver).

The procedural driver implementation is a switch over `AnimState` that
computes hop curves, ease-out velocity, tumble rotations, etc. directly
in code. The skeletal driver implementation samples ozz clips, blends
them per the same `AnimState` mapping, and writes a bone palette.

### Doctrine — what to do when adding a new combat mechanic

1. Define the `AnimState` for it (e.g. `LightAttack2`).
2. Wire the gameplay logic (state machine: when does it start, how long
   does it last, when can it be cancelled, what hitbox does it produce
   and when).
3. Implement the procedural driver function for that `AnimState`.
4. Move on. Do not gate combat work on having a skeletal animation for
   the new mechanic.

When skeletal eventually lands, every existing `AnimState` gets a
parallel skeletal implementation; gameplay logic stays put.

### Re-evaluation triggers (animation-specific)

- A specific mechanic genuinely cannot be expressed procedurally without
  a degenerate amount of code → that's the signal skeletal is ready to
  start, not before.
- Procedural drivers grow past ~500 lines of state-by-state curves and
  start feeling like spaghetti → time to consider tooling (clip
  authoring in code, a simple keyframe editor, or skeletal proper).
- Selva Oscura's visual identity converges on something where character
  motion personality is the headline feature → skeletal earns its cost.

---

## 6. Order of operations

Each milestone leaves the engine **fully working for prison-break-game**. No
milestone is a "now we're broken until step N" valley.

### Milestone 0: Engine modularization (no 3D yet)

Restructure `engine/` into modules without changing capabilities. Goal: prove
the kit shape works before introducing 3D.

- Split `engine/` into `engine/core/`, `engine/render2d/`, `engine/nav2d/`,
  `engine/combat/`, `engine/rpg/`, `engine/audio/`, `engine/input/`. Each is a
  CMake target.
- Move components to the right module: `Sprite` → `render2d`, `FlowField` →
  `nav2d`, etc. `EntityManager` and `Transform`/`Velocity`/`Health` stay in
  `core`.
- Update `game/CMakeLists.txt` to link explicit module list. Build still passes.
- Tests still pass.
- **Deliverable:** prison-break-game runs identically; engine layout has been
  modularized.

This is the pure refactor step. Don't combine it with anything else. Keep the
diff focused so a regression is easy to isolate.

### Milestone 1: Component foundation for 3D coexistence

Widen the shared components without breaking 2D.

- `Transform` gains `z`, `pitch`, `roll`. Default `z=0`, pitch/roll = 0.
  `rotation` renamed `yaw` (with a temporary `rotation` accessor that returns
  yaw, removed in milestone 1.5).
- `Velocity` gains `dz`.
- `CollisionShape` discriminator enum extended with `AABB3D`/`Sphere`/`Capsule3D`
  variants. Existing 2D shapes unchanged.
- `Camera` becomes a base struct; existing 2D usage migrated to `Camera2D`
  (which composes `Camera`).
- 2D systems verified against the wider components: read 2D fields only.
- Tests still pass.
- **Deliverable:** prison-break-game runs identically; data model is 3D-ready.

### Milestone 2: Selva Oscura skeleton — empty 3D scene

Stand up the new game directory and prove the 3D render path can draw one
thing.

- Create `game-selva-oscura/` (or `game/selva-oscura/` if not promoting the
  repo yet). Minimal `main.cpp` linking only the engine modules it needs:
  `core`, `render3d` (new), `audio`, `input`. Crucially: no `render2d`, no
  `nav2d`.
- Implement `engine/render3d/` minimum: `Mesh` (vertex buffer, index buffer,
  material handle), `Material` (shader handle, uniform set), `Camera3D` (view
  + projection matrices), `Renderer3D` (single forward pass, depth test on,
  draws all entities with `Mesh` + `Transform`).
- Vendor glm, cgltf. Ship a hard-coded triangle or single glTF cube.
- **Deliverable:** Selva Oscura's exe boots, opens a window, renders a cube
  spinning on a ground plane. prison-break-game still runs identically.

This is the smallest valuable unit that proves the architecture. **This is
where to start.**

### Milestone 3: 3D character controller — capsule on terrain

- Vendor Jolt. Add `engine/physics/` module.
- Add `RigidBody3D`, `CapsuleCollider3D` components. `MovementSystem3D` reads
  `MovementIntent` and applies forces/velocities to a Jolt body.
- Selva Oscura: load a flat heightmap or simple level mesh (glTF), spawn the
  player capsule, walk around with WASD. Camera follows from a third-person
  orbit.
- **Deliverable:** A capsule walks around a 3D environment. No combat, no
  enemies, no animation yet.

### Milestone 4: Skeletal animation — character mesh, idle/walk

- Vendor ozz-animation. Add `engine/animation3d/` module (or fold into
  `render3d` initially; split later if it gets big).
- Asset pipeline: glTF → ozz skeleton + animation clips. Script lives in
  `engine/scripts/` (it's an engine tool, not a game tool).
- `SkeletonInstance`, `AnimationClip`, `SkeletalAnimationSystem`. Game writes
  "play idle" or "play walk" into a state component; engine plays it.
- Selva Oscura: capsule replaced by a humanoid mesh playing idle when stationary
  and walk when moving.
- **Deliverable:** Animated character mesh.

### Milestone 5: Combat plumbing wired to 3D — first attack lands

This is the validation milestone. The whole architectural bet pays off here:
the existing combat systems work in 3D **with no changes** because their
inputs are render-agnostic.

- Equip a weapon on the character (mesh parented to right-hand bone via
  `WeaponMesh` analogue of `WeaponSprite`).
- Spawn a static dummy enemy entity with a `Hurtbox` (3D capsule).
- Trigger attack via input. `CombatSystem` (existing!) emplaces `AttackLocked`
  and spawns a `Hitbox`. `HitboxResolverSystem` (extended for 3D primitives)
  resolves the weapon's `WeaponHitboxShape` against the dummy's `Hurtbox`.
  `DamageSystem` (existing!) applies damage. `Hitstop` (existing!) freezes
  the world for 50ms. `DamageFeedback` (existing!) flashes the dummy.
- **Deliverable:** Player swings sword, dummy takes damage, combat *feels*
  right because every feel-system from prison-break-game is intact.

This milestone is the moment of truth. If `HitboxResolverSystem` ports cleanly
with just a primitive-test fork, the bet was right. If it requires deeper
surgery, that's a finding worth surfacing.

### Milestone 6: AI enemy — chase, swing, take damage

- Vendor Recast/Detour. Add `engine/nav3d/`. Build navmesh from level mesh
  offline.
- `NavMeshAgent` component. New `ChaseSystem3D` (or existing `ChaseSystem`
  extended) reads the navmesh and writes `MovementIntent.dx/dz` toward target.
- Existing `AggroSystem`, `AIController`, attack tokens, slot circling all
  carry over. (Slot circling becomes 3D but is still 2D math projected onto
  the ground plane — this is fine; that's how every soulslike does it.)
- **Deliverable:** A second character chases the player and swings. Player can
  damage and kill it. The combat foundation is fully proven in 3D.

### Beyond milestone 6

- Lock-on, parry, riposte, dodge i-frames — all should "just work" because
  the systems are render-agnostic. Verify each one explicitly during
  implementation; don't assume.
- 1-bit / dithered render pass. The aesthetic-defining piece. Implement as a
  post-process: render forward to an HDR target, then apply dither LUT in a
  full-screen pass to a 2-color palette.
- Stamina, weapon scaling (STR/DEX), per-hand weapon XP, equipment, crafting
  — port forward as-is.

No timelines on any of this. Each milestone is sized "an obvious unit of
work that leaves the engine in a known-good state."

---

## 7. The first concrete thing to build

**Milestone 2: Selva Oscura's exe boots and renders a single glTF cube on a
ground plane, using a new `engine/render3d/` module that prison-break-game does
not link.**

Why this and not something earlier:

- **Why not a doc-only first step?** This *is* the doc-only step. After this
  doc, the next thing is code.
- **Why not start with milestone 0 (modularization)?** Modularization without
  a second consumer is theoretical — easy to get the boundaries subtly wrong.
  Doing milestone 2 first means the second consumer exists (even if it's
  just rendering a cube), so the engine boundaries get exercised by two games
  from day one. **Caveat:** if milestone 2 reveals that pulling render2d out
  of `engine/` is needed *first* to keep the build clean, do milestone 0 then
  milestone 2. Order is a guideline, not a contract.
- **Why not start with the character controller?** A character controller
  without a renderer to see it through is a black box. Render first, then
  control what you can see.
- **Why not the dither shader / aesthetic first?** Tempting, because the
  aesthetic is the most exciting unknown. But the aesthetic is a post-process
  on top of working geometry — having geometry first means iterating on the
  shader is fast.

The cube on a ground plane is the smallest unit that *proves the architecture
works*: a second game exists, links the engine, uses a new render module, and
boots. Everything after this milestone is incremental.

---

## 8. Open questions to surface during implementation

Things this doc commits to a direction on but should be reviewed when
implementation pressure hits them:

- **Does `Transform` widening to 3D actually keep 2D code clean, or does the
  ignored-field pattern leak?** If 2D systems start having to write `z=0`
  defensively or clear pitch/roll anywhere, fork into `Transform2D`/`Transform3D`
  and accept the duplication.
- **Does `HitboxResolverSystem` cleanly extend, or does it fork?** Extension is
  the bet. If the 3D primitive tests force a different control flow (sweep vs
  overlap, broadphase), fork is honest.
- **Does the camera abstraction split cleanly?** The 2D camera has lock-on
  offset, screen shake, pixel rounding, and ortho zoom. The 3D camera has FOV,
  pitch/yaw, follow distance, collision against geometry. The base might end
  up too thin to share.
- **Selva Oscura's tonal-register doctrine.** Mono had voice rules (Dantean,
  Commedia register, no modern English). Port the doctrine doc forward as
  `game-selva-oscura/docs/VOICE.md` when starting on dialogue/UI text.
- **Save data path and identity.** Selva Oscura needs its own
  `%APPDATA%/SelvaOscura/` (or whatever the final title is). Don't let it
  collide with prison-break-game's saves. Trivial but easy to forget.

---

## 9. What this doc does NOT decide

- The dither shader's exact technique (ordered Bayer matrix, blue noise,
  per-material dither pattern, etc.). Defer until the render path exists.
- Whether Selva Oscura is third-person only or also first-person. Defer until
  the character controller is in.
- Class evolution data structures, vestigia save format, nine-circles structure
  — these are mono content design, not engine architecture. Port the design
  docs forward when starting on game content.
- Whether the engine eventually open-sources. Decide later, after at least
  two shipped games prove it's worth releasing.

---

## 10. Tracking

This doc lives until either:
- it's superseded by milestone-specific docs (milestone 2 implementation might
  spawn `engine/render3d/README.md`), or
- the architecture turns out wrong and we write a postmortem here explaining
  what we learned and what replaces it.

Keep it honest. If a milestone reveals that the bet was wrong, update this
doc. Don't paper over divergence.
