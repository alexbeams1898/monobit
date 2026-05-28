# Chapel source-of-truth audit — 2026-05-24 (v2, corrected)

**Status:** v1 of this audit was wrong because the audit script didn't read glTF `node.translation` while the actual C++ loader does. v2 fixes that and produces real data. The conclusions below are based on v2.

Source: `games/selva-oscura/scripts/audit_chapel_sources.py`

## Headline

The chapel has **84 mesh primitives** + **23 C++ box colliders**, both claiming to describe the same architecture. They do not match. Both have been independently authored and have drifted.

- **4 exact-match pairs** (`corridor_landing_floor_0..2` ↔ `corridor_landing_0..2`, `acheron_platform` ↔ `acheron_platform`) — pure duplicates, pick one source.
- **65 shifted-overlap pairs** — overlapping XZ footprint but different Y, different extents, or different decomposition (e.g. mesh has `crypt_wall_back_left/right/upper` as 3 prims, code has `wall_back_left/right` as 2 colliders, `_upper` has no counterpart on the C++ side).
- **15 mesh-only primitives** — visible but with no collider counterpart: roof, pediment, cornice, ceilings, alcoves, cross, plinth_l/r (intermediate stair steps 01,02,03 too — see "step-collider mismatch" below).
- **0 code-only colliders** — every C++ collider has at least some overlapping mesh primitive.

## What's currently blocking the player at z=-213.63

Player capsule x[-0.59, 0.11], y[31.96, 33.76], z[-213.98, -213.28]. Nothing within 1m clearly blocks forward motion — but the player got stuck. Best explanation from the spatial query:

- The C++ `chapel_floor_apse` strip ends at z=-213.40 (no floor past that point inside the chapel back-wall tunnel)
- The C++ `landing_top` starts at z=-212.97 and extends to z=-215.15, at y_top=30.82
- The C++ `wall_back_left/right` (and matching mesh `crypt_wall_back_left/right`) at z[-214.00, -213.40] form the chapel back wall — but they have a gap at x[-1.5, 1.5] for the tunnel
- So the player walked into the tunnel gap, exited the chapel back, but the landing is **1.44m BELOW** the chapel floor. Should be a controlled step-down or fall.
- Instead the player got stuck at y=31.96 (= `chapelWorldOrigin().y` = terrain ground level outside the chapel). They're standing ON terrain that exists between the chapel back wall and the apse, NOT on the landing (1.14m below).
- The mesh `crypt_apse` at z[-215.80, -214.00] is 0.02m forward of the capsule front edge — close to blocking but not the actual problem.

**The block isn't a single bad collider. It's the geometric topology being broken:** chapel floor and landing are at different heights with no ramp between them in the back-wall tunnel area, plus the apse-side terrain is acting as an unwanted floor in the gap region.

## Drift catalog (key examples — not exhaustive)

| Mesh prim | C++ collider | Drift |
|---|---|---|
| `crypt_wall_back_left/right` | `wall_back_left/right` | Same XZ, same z range. Mesh y[32.26, 34.41]. Code y[32.25, 37.25]. **Code wall is 2.84m taller** (extends to roof; mesh only to door-header height). |
| `crypt_wall_back_upper` | (none) | Mesh has the upper band above the tunnel; code has no equivalent — the player could walk straight through the upper-tunnel area if they could jump (currently can't). |
| `crypt_apse` | (none, only `wall_back_*` partially overlaps) | Mesh has the closed half-dome behind the chapel. Code has no apse collider (only the C++ `populateCryptApseCylinders` builds curve, which my new code wires to Jolt — see below). |
| `stair_upper_step_00..08` | `upper_flight_ramp` (single sloped box) | Mesh has 9 discrete step boxes covering z[-213.15, -210.00]. Code has 1 sloped ramp at z[-213.15, -210.00] with continuous slope. Steps 01,02,03,05,06,07,08 don't even spatially-overlap the ramp in y. |
| `corridor_ramp_0..3` (mesh) | `corridor_ramp_0..3` (C++) | Same XZ footprint, mesh has big tall box (y range 15.7m to cover the full ramp diagonal as an AABB), code has thin sloped box (y range 0.5m). **Massively different solids.** |
| `crypt_plinth_front/back_l/r/l/r` | (none — plinth is exterior chrome) | Plinth is the exterior zoccolo at the chapel base. Currently registered to Jolt via the chapel trimesh; player walks into the chapel by stepping over the plinth via... nothing collider-side. Works only because the plinth is 0.3m tall and the door provides a clear gap above it. Fragile. |

## Why "is the loader broken?" was a wrong diagnosis (v1 of this audit)

I jumped at "the loader doesn't apply `node.translation`" because my audit script forgot to apply transforms when computing AABBs, and the result showed every stair step at the same world position. The loader at `games/selva-oscura/src/world/StaticMeshAssets.cpp:185` correctly calls `cgltf_node_transform_world` and applies the world matrix to every vertex. **The mesh side is being loaded correctly.** Lesson logged: see [[feedback_audit_script_can_lie]] — when audit output is suspicious, cross-check against runtime data BEFORE proposing a fix based on the audit.

## Off-task weak-foundation findings

Surfaced while doing the audit; flagged for Alex to triage. Not blocking the current chapel work.

### F-A: Renderer + physics each independently apply `chapelWorldOrigin()`

The mesh vertices are local-space; renderer applies `cryptModelMatrix()` as a uniform; PhysicsScene bakes the same translation into the trimesh verts (via `world_positions = p + world_origin`). The two paths agree TODAY, but only because both call `crypt_layout::chapelWorldOrigin()` directly. If a future asset uses a non-identity ROTATION (e.g. tilted chapel for a non-default orientation), the renderer would naturally apply a rotation matrix while the physics path's `vec3 + vec3` would silently break. Solution: PhysicsScene should consume a `glm::mat4` (not just translation) when registering chapel mesh primitives, applied as a per-vertex transform, mirroring the renderer's model matrix exactly.

### F-B: 84 mesh primitives = 84 separate Jolt static trimesh bodies for one chapel

Each mesh primitive (every stair step, every alcove side, every facade cladding rectangle) becomes its own Jolt static body via the registerChapel loop. Jolt scales fine to thousands of static bodies, but the per-body overhead (broadphase cell, BodyID, mShape, mWorldTransform, MotionProperties) is per-primitive. A 5-prop scene won't notice; a 100-prop scene with finely-subdivided assets will. Worth profiling Jolt's body count after a few more assets are in.

### F-C: Tunnel-gap stair geometry is logically a SINGLE descent shape, encoded TWICE incompatibly

The mesh has 9 step boxes. The code has 1 sloped ramp. They cover the same XZ and approximately the same Y range but with very different per-Z heights. Both registered to Jolt = the player capsule will collide with WHICHEVER is higher per query — unpredictable per position. This is exactly the dual-source bug class.

### F-D: `crypt_wall_back_upper` exists in mesh but not in C++ (and vice versa for door header)

- Mesh `crypt_wall_back_upper` at y[34.41, 38.46] — high band above the tunnel; player can never reach it, so the lack of a C++ collider doesn't matter today. But it's data drift waiting to bite.
- C++ `door_header` (`camera_only=true`) at y[34.45, 37.25] — was for legacy camera-collision; Jolt has no camera-collision concept yet, so this was already irrelevant going forward.

### F-E: `populateCryptApseCylinders` produces ~32 small cylinders

These wrap the rear of the apse from outside. With Jolt I converted them to AABBs (cheaper than cylinder primitive), but **32 individual bodies for one curved wall** is silly when the chapel mesh already includes `crypt_apse` (a real curved half-dome trimesh). Once we pick mesh as source of truth, all 32 cylinders go.

### F-F: F1 collider debug overlay reads only the legacy `CollisionScene`

If we delete `populateCrypt*`, the F1 overlay goes blank for the chapel. The overlay needs a Jolt-bodies enumeration path. Already noted, restated here for completeness.

### F-G: Two parallel "chapel world origin" sources

`crypt_layout::chapelWorldOrigin()` returns `(kCryptX, kChapelGroundY - kPlinthHeight + 0.01, kCryptZ)`. `populateCryptColliders` computes `y_base = kChapelGroundY` directly. The TWO different reference Y values (`kChapelGroundY` vs `kChapelGroundY - kPlinthHeight + 0.01`) explain why every "shifted" pair has a Y offset of ~0.3m. Should be one constant referenced everywhere.

## Harmony recommendation

The dual-source-of-truth is the bug class. Three ways to resolve, in order of recommended approach:

### Option H1 — Mesh is the source of truth (recommended)

- **What:** The chapel `.glb` becomes authoritative for ALL chapel geometry. Physics registers the chapel mesh primitives as Jolt static trimeshes. Delete `populateCryptColliders`, `populateCryptDescent`, `populateCryptApseCylinders` from `Collision.cpp`.
- **Pros:**
  - Single source of truth — geometry drift becomes impossible
  - Adding a new prop is "author it in Blender → re-export → done"
  - The mesh has finer detail than colliders (real stair steps vs single ramp; real apse curve vs cylinder ring)
  - Removes ~200 lines of C++ collider-authoring code
- **Cons:**
  - Need a Blender round-trip to fix the back-wall-tunnel geometric topology (the height mismatch between chapel floor and landing); right now the .blend has it broken
  - The `walkable_top` / `top_slope` ramp abstraction is lost; CharacterVirtual handles slopes via real geometry. This is actually good (more correct), but means tuning happens in Blender not C++
  - F1 overlay needs a Jolt-bodies enumerator (F-F)

### Option H2 — Code is the source of truth

- **What:** C++ `populateCrypt*` is canonical. The `.glb` becomes a "render-only" chrome layer with no collision impact (Jolt registers ONLY C++ collider data; mesh registration skipped entirely).
- **Pros:**
  - No Blender round-trip needed; design constants in C++ drive everything
  - Code-driven, scriptable, programmer-controllable
- **Cons:**
  - Need to add a procedural-mesh generator that builds visible geometry from CryptLayout.h constants OR keep the .glb completely synchronized via the gen_crypt_foundation.py script
  - Lose mesh-level detail (collisions become coarse-AABB or coarse-ramp)
  - Doesn't generalize: every new prop now needs C++ collider code, AND a Blender mesh, AND they must agree
  - Same dual-source bug class, just with a different "winner" — fundamental problem unfixed

### Option H3 — Hybrid (specific contract)

- **What:** Mesh is authoritative for static architecture geometry (walls, floors, ceilings, decorative chrome). Code authors **gameplay zones** only: spawn points, indoor footprints, scripted-event triggers, NPC patrol regions. Two systems with NON-OVERLAPPING responsibilities, not two systems both claiming the same fact.
- **Pros:**
  - Most realistic going forward (every game engine ends up here)
  - Drift impossible within each domain
- **Cons:**
  - Requires clear boundary discipline; "I'll just add this collider in code for now" is the start of drift

## Recommended action sequence

1. **Pick H1 (mesh as source of truth).** Aligns with the "physics owns world geometry" direction we're already going with Jolt.
2. **Open `crypt_foundation.blend` in Blender, fix the back-wall-tunnel topology:** add a sloped transition between chapel floor (y=32.26) and landing top (y=30.82). Either ramp down through the tunnel, or extend the chapel floor THROUGH the wall to where the descent stairs begin. The current geometric topology has a 1.44m drop with no floor in between — that's a design problem, not a physics problem.
3. **Re-export `crypt.glb`.**
4. **Delete `populateCryptColliders`, `populateCryptDescent`, `populateCryptApseCylinders` from `Collision.cpp`.** Delete corresponding `registerSceneBoxes` from `PhysicsScene.cpp`.
5. **Remove the `crypt_floor`, `crypt_wall_back`, `crypt_apse` skip-list** from `PhysicsScene::registerChapel` (will be no longer needed; mesh is canonical).
6. **Extend F1 collider overlay to enumerate Jolt bodies** so we keep visualization. Separate small task.
7. **Re-run this audit** — should show 0 dual-source pairs.
