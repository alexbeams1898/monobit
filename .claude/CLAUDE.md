# Prison Break Game — CLAUDE.md

## What this is
Comedic top-down action roguelike set in Hell — marketed as a real-world escape dungeon
roguelike; the hell setting is the narrative twist revealed through play. Inspired by
Dante's Inferno/Divine Comedy (layers of Hell structure), Doom (escalating demonic hostility),
Vampire Survivors (core loop, enemy escalation), and Dark Souls (stats, build variety,
crafting). Custom C++ engine. Full design doc in `docs/DESIGN.md`.

## Portability goal — "runs on a calculator"
This game should be portable to everything. Doom ran on a pregnancy test. Vampire Survivors
runs on a potato. That's the vibe. Every technical decision should keep this in mind:
- No platform-specific APIs outside the abstraction layer (SDL2, OpenGL)
- No unnecessary dependencies
- Minimal memory footprint, minimal CPU budget
- No features that assume a high-end GPU or multi-core CPU
- If a system can run a 2D sprite game at 60 Hz, this game should run on it

## How we work
- Alex drives all design and creative decisions
- Claude assists, asks questions, and helps think things through
- Don't get ahead or push ideas unsolicited, but speak up if something seems worth flagging
- Claude should update CLAUDE.md, docs/DESIGN.md, docs/PERFORMANCE.md, and README.md as new decisions are made
- docs/PERFORMANCE.md tracks all performance and rendering architecture decisions ad hoc — update it whenever a system's perf characteristics change, a bottleneck is found/fixed, or a major rendering decision is made
- When in doubt, ask

## Engineering philosophy
- **No bandaids. Fix at the root.** Structural flaws get fixed, not papered over.
- **No deferring foundational work.** Alex's default is to fix it now properly. No timeline
  pressure — getting it right matters more than getting it done fast.
- **Branch scope is flexible.** Fine to pile related fixes onto the current branch. Document
  everything in the PR description. Thorough PR description > strict 1-branch-1-issue mapping.
- **Bandaids are explicitly flagged.** Truly temporary things get a comment + a GitHub issue
  immediately. Nothing temporary is silent.
- **Every system is built for the worst case from day one.** See Performance Philosophy.

## Git workflow
- Branch per issue: `issue/N-short-description`
- Commit messages: **one line, no heredoc, no multi-line body, no co-author signature** — e.g. `git commit -m "Fix enemy spawn path"`
- Before pushing: run lint pipeline below and fix all issues first
- When done: push branch, open PR with `Closes #N` in body. **No "Generated with Claude Code" footer in PRs.**
- Alex reviews and merges manually with squash
- **Never auto-push. Never commit/push until Alex confirms local build passes (F7).**

## Testing policy
- Always add Catch2 tests in the same PR — never defer
- Test anything that runs without window/GPU (arithmetic, ECS queries, config loading)
- Systems needing SDL keyboard or GL context are integration-tested by running the game;
  document the exclusion in the test file
- New test files go in `tests/` and must be added to `tests/CMakeLists.txt`
- Use `Catch2::Approx` for float comparisons

## Pre-push lint pipeline
Run all three steps from repo root before every push. All must pass.

```bash
# 1. clang-format (fix in-place)
find engine game -name '*.cpp' -o -name '*.h' | \
  xargs /c/msys64/mingw64/bin/clang-format.exe -i

# 2. clang-tidy (requires build/compile_commands.json — run F7 first)
find engine game -name '*.cpp' | \
  xargs /c/msys64/mingw64/bin/clang-tidy.exe -p build --quiet

# 3. tests
./scripts/test.sh
```

Common clang-tidy CI failure: `clang-analyzer-security.FloatLoopCounter` — never use a float as a
for-loop counter. Use an int and compute the float position from it.

## Code comment policy
- Comments must be practical and concise — explain *why* non-obvious code works, not *what* it does
- No meta/design language in `.cpp`/`.h` files: no game names (Dark Souls, Vampire Survivors, Elden Ring), no genre labels (soulslike, roguelike, VS-style), no marketing copy
- That language belongs only in `docs/` markdown files, never in source code
- No verbose multi-sentence blocks re-explaining high-level system intent — that goes in docs/

## Learning context
- Alex is learning C++, CMake, game engine architecture, ECS, OpenGL from scratch
- Background is React/Node.js — use JS/TS analogies: CMake ≈ package.json + vite,
  FetchContent ≈ npm install, header files ≈ TS exports, entt ≈ Redux/Zustand, SDL2 ≈ Electron
- After every meaningful action, explain what was just done and why — plain language
- Explanations follow the action, not precede it

---

## Performance Philosophy

Target: 1000+ simultaneous enemies at 60 Hz. Every architectural decision should assume
this is already here.

- **Systems do the work, entities don't think.** Fetch one shared value (e.g. player pos)
  once per frame, sweep all entities in a tight loop. No per-entity lookups into other entities.
- **Avoid O(n²) unless n is provably tiny.** Flag with a comment and a plan. CollisionSystem
  is currently O(n²) — needs spatial partitioning before enemy counts scale.
- **No unnecessary nested loops.** Before writing a nested loop, ask: can the inner loop be
  eliminated with a lookup, a lambda, or by enumerating only the relevant subset directly?
  Triple-nested loops are almost always a sign the algorithm needs rethinking. If a nested loop
  is genuinely the right structure, add a comment explaining why.
- **Prefer data-oriented layout.** entt sparse sets are contiguous. Keep components small and
  flat. No pointers-to-pointers.
- **Batch everything renderable.** One draw call per enemy is fatal at scale. RenderSystem
  must use instanced rendering before enemy counts grow.
- **Two tiers of AI complexity.** Enemy design is souls-style — small purposeful groups with
  distinct behaviors, not blobs. The architecture scales to 1000+ if the design calls for it
  (wave escalation endgame), but individual enemy behavior should feel intentional.
  Fodder: simple ECS state machine + flow field, O(1) per frame, no allocations.
  Elites/bosses: richer state machines or scripted attacks — fine because they're rare.
  New enemy behaviors = new component + new system; cost is proportional to how many enemies
  carry that component. Never grow a monolithic AI function.
- **Measure before optimizing, but design for scale from the start.** Tracy is wired in.

## Tracy profiling conventions
- Every system's `update()` function gets `ZoneScopedN("SystemName")` as its first line.
- Every renderer's `render()` function gets `ZoneScopedN("RendererName")`.
- **When adding a new system or renderer, add the zone immediately** — don't defer.
- Key gameplay events use `TracyMessageL("EventName")` for timeline correlation:
  `PlayerAttack`, `PlayerSkill`, `PlayerDodge`, `EntityDamaged`, `EntityDied`,
  `EnemySpawned`, `EnemyAggro`, `EnemyAttack`.
- **When adding a new significant gameplay event, add a `TracyMessageL` immediately.**
- Trace files live in `traces/` (gitignored). Analyze with `./scripts/analyze-trace.sh`.

---

## Engine & Stack

**Stack:** C++17/20 · SDL2 (windowing/input) · OpenGL (rendering) · entt (ECS) · FMOD (audio) · CMake

**ECS:** Everything is an entity. Components are pure data. Systems are logic. Adding content
= writing a config file. Non-negotiable.

Goals: data-driven, config-driven, moddable from day one.

---

## Engine patterns — hard-won lessons

Reference before designing any system touching movement, collision, or AI.

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
**Key rule:** Any corridor/door must be ≥3 cells wide (48px) so after removing 1 clearance cell
each side, ≥1 routable center cell remains.

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

### Split-body rendering (player)
Player rendered as two child entities: lower body (legs facing velocity) and upper body (torso
facing mouse aim). Each child has its own Sprite + Animation, linked via `BodyPart` component.
Parent keeps all gameplay components but has no Sprite/Animation.
**BodyPart component:** `parent` (entt::entity), `faces_aim` (bool). `faces_aim=true` = direction
from parent's FacingDirection + state priority Dead > Hit > Attack > Idle. `faces_aim=false` =
direction from parent's Velocity + state priority Dead > Hit > Walk > Idle.
**Position sync:** Children's transforms copied from parent in `Engine::update()` (after
CameraSystem) and `Engine::render()` (before AnimationSystem).
**Config:** `player.json` uses `"body_parts"` array instead of `"sprite"`/`"animation"`. Each
entry: `{ "sheet": "config/animations/player_lower.json", "draw_order": 2, "faces_aim": false }`.
**DeathSystem cascade:** When parent is destroyed, all children with matching `BodyPart.parent`
are destroyed too.
**Sprite sheets:** `assemble_spritesheet.py --split` generates `player_lower.png` and
`player_upper.png` with vertical alpha mask at y=35 (lower keeps legs, upper keeps head/shoulders).
**LPC limitation:** No torso-twist frames in LPC assets. Only head + slight shoulders rotate.
Custom art planned for future.

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
Camera interpolated the same way. Facing dot position integer-rounded.
**Critical:** Player facing is updated per-frame in `processEvents()`, NOT in the fixed-step
loop. Mouse position is a per-frame input — tying it to the fixed tick rate causes 0-update
frames where facing goes stale (visible as wobble when mouse+WASD simultaneously).

---

## Tech debt

_No outstanding tech debt at this time._

---

## Research Notes (non-urgent)

- **VS number tuning** — how VS makes damage numbers, XP, and loot feel satisfying at scale
- **Chest frequency tuning** — too many breaks economy, too few feels dry. Revisit once built.
- **Death loop hybrid** — souls-style death/respawn as alternative to VS-style game over
- **Pikuma C++ 2D Game Engine course** — 30 hours, C++, SDL2, ECS, Lua scripting. Useful ref.