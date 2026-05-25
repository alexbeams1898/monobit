# Stale code audit — 2026-05-24

After today's Scenes architecture build (engine Scene/SceneManager,
JsonScene, AsyncSceneLoader, terrain modifiers registry, Jolt physics
migration, footstep-via-body-tag, save-schema scene-awareness), several
pre-Jolt / pre-Scenes systems are partially or fully stale. This audit
catalogs them with severity + recommended action.

## Severity legend

- **DEAD** — zero callers, safe to delete after a final grep
- **STALE-LIVE** — still called BUT only by legacy/dead-end systems;
  delete once the caller migrates
- **MIGRATION-PENDING** — actively used by code that hasn't migrated to
  Jolt yet (camera raycast, NPC ground sampling). Migration is a known
  pending step.
- **DOCUMENTATION-STALE** — comments referring to deleted systems

## DEAD (zero callers, delete)

### `world::resolveBodyCollision` (Collision.cpp:334, Collision.h:127)
Last caller deleted when PerFrameTick migrated to Jolt CharacterVirtual.
Function still defined. **Action:** delete function + declaration.

### `world::isOnAuthoredSurface` (Terrain.cpp:375, Terrain.h:71)
Last caller was the locomotion picker's stair-clip branch, deleted when
the velocity-driven locomotion plan went to uniform stairs. Function
walks `currentScene().boxes` looking for `walkable_top` boxes — none
exist anymore (populateCryptDescent was deleted). Always returns false.
**Action:** delete function + declaration.

### `walkable_top` flag on `BoxCollider` (Collision.h:68)
No `BoxCollider` is ever authored with `walkable_top=true` after
populateCryptDescent's deletion. The flag is read by Terrain.cpp:379
and Terrain.cpp:420 in `isOnAuthoredSurface` and `groundHeight` — both
dead branches (groundHeight is migration-pending, see below).
**Action:** delete the flag from BoxCollider + the branches that read it.

### `BoxCollider::camera_only` flag
Same fate as walkable_top: no authored box uses it now. Read by
resolveBodyCollision (dead) and the F1 collider overlay (still draws
labels). **Action:** delete the flag once resolveBodyCollision is gone;
overlay just drops the magenta color tier.

## STALE-LIVE (called by stale paths)

### `world::groundHeight` (Terrain.cpp:389)
Called by `Enemies.cpp:259` (`a.pos.y = groundHeight(...)`) to snap
NPCs to terrain Y each frame. NPCs haven't migrated to Jolt capsules
yet (Phase 6 in todo list). Internally reads mesh_y AND walks
`currentScene().boxes` for walkable_top boxes (always empty). Once
NPCs are on Jolt, NPC Y comes from physics, and groundHeight is dead.
**Action:** delete after Phase 6 NPC migration.

### `world::sampleHeight` (Terrain.cpp:323)
Called by `PerFrameTick.cpp:3714` (spawn ground), `main.cpp:248` (foot
IK lambda — disabled but lambda exists), `WorldRenderer.cpp:434/687/768`
(camera ground clamp + cylinder collider overlay heights). Reads
`mesh_y` which now correctly includes TerrainModifiers. NOT stale at
the API level (the values it returns are correct), but the camera and
tree-overlay callers will likely migrate to Jolt raycast as part of
Phase 7. **Action:** keep for now; revisit after Phase 7.

### `world::raycastScene` / `world::sphereOverlapsScene` (Collision.cpp:644, 631)
Pre-Jolt scene-only raycast / overlap, walks `currentScene().cylinders`
+ `.boxes`. Used by `WorldRenderer.cpp:328, 344, 447` — the camera
collision pipeline. The chapel is no longer in `currentScene()` (only
trees are), so these queries effectively only see trees. Camera does
not collide against chapel walls anymore via this path.
**Action:** migrate camera collision to `engine::physics::raycast`
(Phase 7); then delete raycastScene/sphereOverlapsScene + the entire
intersectCylinder/intersectAabb support code in Collision.cpp.

### `currentScene()` legacy collision scene + `sScene` global (Collision.cpp:317)
Holds the legacy `CollisionScene { cylinders, boxes, interior_footprints }`.
Today contains only trees (cylinders) + chapel interior footprint
(boxes is empty after populateCrypt* deletion). After:
- Phase 6 (NPCs on Jolt): nothing else needs trees as collision data
- Phase 7 (camera on Jolt): nothing needs trees as raycast targets
…the entire CollisionScene struct becomes vestigial. Trees should be
Jolt cylinder bodies registered at scene activation; interior
footprints become a Scene-level "indoor zones" concept.
**Action:** delete CollisionScene struct + currentScene() global after
both phase migrations complete.

### `world::isIndoors` (Collision.cpp:322)
Called by WorldRenderer.cpp camera code (4 callsites). Reads
`sScene.interior_footprints` which is populated by
`populateCryptInteriorFootprint`. With Scenes architecture this becomes
a per-Scene concept (each interior scene IS by definition "indoor";
each exterior scene is "outdoor"). Once camera migrates, isIndoors
becomes `currentScenePtr()->kind == Interior` or similar.
**Action:** preserve until camera migrates; replace with scene-kind
check at that time.

### `populateCryptInteriorFootprint` (Collision.cpp:223)
Only caller is `initHubScene()` which builds the legacy CollisionScene.
Footprint is read by `isIndoors`. Both dead-end together.
**Action:** delete with isIndoors migration.

## MIGRATION-PENDING (known Phase 6/7 work)

### NPCs use `Enemies.cpp:259` ground sampling
NPCs read terrain Y via `groundHeight`, write to `a.pos.y` each frame.
Not Jolt-driven. Phase 6 (NPCs on Jolt capsules) replaces this.

### Camera uses pre-Jolt collision (`raycastScene`, `sphereOverlapsScene`, `isIndoors`)
WorldRenderer.cpp camera-collision pipeline. Phase 7 (camera via Jolt
raycast) replaces this with engine::physics::raycast.

### F1 collider debug overlay reads legacy `sScene.boxes/cylinders` (ActorHud.cpp:577..)
Still iterates the legacy CollisionScene for trees + (empty) boxes.
The Jolt-body overlay is wired alongside it (ActorHud.cpp:778..). Once
the legacy collision is deleted, the legacy half of the overlay
collapses to just trees-as-Jolt-bodies via enumerateBodies.

## DOCUMENTATION-STALE (comments only)

- Multiple comments throughout Collision.cpp / CryptLayout.h reference
  "the legacy populateCryptDescent" / "before Jolt physics" — these are
  fine as historical context but should be reviewed for outdated
  doctrine (e.g. "walkable_top is the pattern" — no longer the case).
- `feedback_real_physics_doctrine` memory note already supersedes most
  of these.
- `CryptLayout.h` comments about "kept in sync with gen_crypt_foundation.py"
  are accurate but flag a real dual-source — future move to JSON
  manifest is recommended (queued, not done).

## Recommended cleanup order

1. **After Phase 6 (NPCs on Jolt):** delete `groundHeight`,
   `isOnAuthoredSurface`, `walkable_top` flag, all related branches.
2. **After Phase 7 (camera on Jolt):** delete `raycastScene`,
   `sphereOverlapsScene`, `intersectCylinder`, `intersectAabb`,
   `isIndoors` (replaced by scene-kind check), `CollisionScene`,
   `populateCryptInteriorFootprint`, `currentScene` global.
3. **At that point Collision.cpp becomes ~50 lines** (trees-as-data
   helper for the renderer; trees-as-Jolt-bodies live in PhysicsScene
   instead).

## Newly identified dual-source items (track but don't fix yet)

### CryptLayout.h constants duplicated in `gen_crypt_foundation.py`
Constants like `kSingleFlightHalfWidth`, `kStairTread`, etc. are in
both the C++ header AND hardcoded in the Blender script. Comment in
the Python script literally says "Keep in sync if the C++ values
change." Classic dual-source-of-truth. Diamond fix: move shared
geometry config to JSON manifest read by both sides. Queued under
"larger refactor" — not blocking today's work.

### Chapel `crypt.glb` includes BOTH exterior and descent geometry
After the scene split (Step 8 of today's planning), this asset should
be `crypt_exterior.glb` (SurfaceScene) + `crypt_interior.glb`
(ChapelInteriorScene). Currently the chapel mesh has 443 primitives
spanning both — single .glb feeding the legacy boot. Until the split,
the legacy boot still works, but the future ChapelInteriorScene
cannot load just the interior.
