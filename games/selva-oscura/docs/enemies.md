# Enemy authoring workflow

> **Owns:** the mechanical pipeline for adding a new enemy archetype —
> what files to create, what fields to author, what assets to bake.
>
> **Scope:** engineering / authoring reference. Design canon
> (cosmology, bestiary, class fit) lives in `docs/design/`. Runtime
> behavior lives in [`docs/systems.md`](systems.md) *Gameplay* section.
>
> **Audience:** whoever is adding an archetype tomorrow. If a field
> below looks wrong or a step is missing, fix this doc — the source of
> truth is the schema (`include/gameplay/EnemyArchetype.h`); this doc
> is the walk-through.

## The one-page picture

Every humanoid enemy archetype is:

- **One JSON file** at `config/enemies/<id>.json` — actions, clips,
  pools, cosmology flags
- **One character file** at `config/characters/<id>.json` — colour,
  body scale, morph weights baseline (AuthoredCharacter schema; named
  characters like the Guide additionally author display_name_key /
  stats / class / hand items)
- **One or more baked `.glb` files** at
  `assets/characters/enemies/<id>/humanoid.glb` — per-archetype mesh
  with the archetype's own skin diffuse baked as a texture, sharing a
  well-known skeleton with every other humanoid so the clip registry
  cross-plays
- **Zero new clips** if the archetype re-uses existing animations
  (idle / walk / attacks all live in the shared per-skeleton clip
  library keyed by `skeleton_id`). New clips get baked once, then
  named by any archetype that wants them.

The archetype JSON is the ONE authored source of truth for a spawned
actor's behavior. Everything else (mesh, skeleton, clips, hurtboxes,
lockon points, spawn locations) is looked up by id from that JSON.

## Directory conventions

```
config/
  enemies/<archetype_id>.json               # this file authors the archetype
  appearances/<archetype_id>.json           # this file authors the visual baseline
  spawn_flows/<flow_id>.json                # references the archetype by id

assets/characters/enemies/<archetype_id>/
  humanoid.glb                              # baked mesh; per-archetype
  skin_diffuse.png                          # baked into the .glb, kept for source-control diff
  source/                                   # authoring inputs (base texture, asset refs, etc.)
  blender_authoring/                        # staged intermediates from the bake script
```

The `<archetype_id>` string is the join key. It is:

- The filename stem of the JSON at `config/enemies/<id>.json`
- The `id` field inside that JSON (kept in sync — file loader logs a
  warning on mismatch)
- The directory name under `assets/characters/enemies/<id>/`
- The value used in `mesh_path_variants` entries when a parent
  archetype ships gender-variant meshes (see [Multi-variant meshes]
  below)

## The archetype JSON schema

The full schema is [`include/gameplay/EnemyArchetype.h`](../include/gameplay/EnemyArchetype.h);
every field carries its own comment. This section walks through the
fields grouped by concern.

### Identity + cosmology

- `"id"` — matches the filename stem
- `"form"` — `"UnjudgedSoul"` / `"DamnedSoul"` / `"Animal"` / etc.
  Drives per-form stat defaults applied BEFORE archetype overrides.
- `"faction"` — `"Hostile"` (default) / `"Neutral"` (peaceful NPCs,
  dormant mobs) / `"Allied"` (party) / `"Player"` (never in JSON)

### Skeleton + mesh

- `"skeleton_id"` — key into the per-skeleton registry (see
  `clipsByKey("humanoid_male")` etc.). Two shared bundles cover every
  humanoid archetype today (male / female). Adding a new
  archetype does NOT normally need a new skeleton — you re-use one of
  the existing keys.
- `"mesh_path"` — single mesh case. Points at
  `assets/characters/enemies/<id>/humanoid.glb`. Every actor of this
  archetype renders using this mesh.
- `"mesh_path_variants"` — multi-variant case (see below). When
  non-empty, OVERRIDES `mesh_path`.
- `"character_path"` — points at `config/characters/<id>.json`.
  Loaded once at spawn; every actor of this archetype starts from
  this appearance.

### Multi-variant meshes

Some archetypes ship N pre-baked meshes and the spawn code rolls a
uniform random index per-spawned-actor. Example: an archetype that
ships male + female bakes with the same skeleton so every actor in a
wave picks one at random.

```jsonc
"mesh_path_variants": [
  "assets/characters/enemies/foundling/humanoid.glb",
  "assets/characters/enemies/foundling_female/humanoid.glb"
]
```

The picked index is stashed on `Actor.mesh_variant_index` at spawn so
subsequent archetype swaps (fresh → aged) preserve the actor's
gender: the new archetype's `mesh_path_variants` list is read at the
same index. **Author gender-variant lists in the SAME ORDER across
parent + swap-target archetypes** or the swap will change gender
visually.

Adding a new variant later (ethnicity, skin tone) is one entry in the
list — no schema changes required.

### Per-actor face-morph randomization

```jsonc
"random_face_morphs": true
```

When true, `spawnEnemyFromDecl` calls `rollRandomFaceMorphs` (see
[`include/gameplay/AppearanceVariance.h`](../include/gameplay/AppearanceVariance.h))
and writes the rolled values into `Actor.appearance.morph_weights`.
Uses a thread-local RNG so each actor is independent. Skips all scale
morphs (head_size, eye_size, etc.) by design — those change silhouette
and would break the shared-skeleton clip cross-play. Only rolls face
axes (nose, cheek bones, chin, eye position, lip volume).

Turn on for archetypes that spawn in crowds. Leave off for
individually-named actors whose face is authored (bosses, NPCs, the
player).

### Physics + damage

- `"disable_hurtboxes"` — actor takes no damage; the player's swings
  pass through with no hit registration. Distinct from `intangible`
  below. Used for actors that should not be killable in this state
  (immature-state actors, intact NPCs).
- `"intangible"` — actor's Jolt body sits in the `INCORPOREAL`
  collision layer. Still stands on terrain (gravity + ground snap +
  hazard avoidance intact), but the player and other actors walk
  through it and it walks through others of its kind. See
  [Collision layers] below.

The two are independent axes:

| tangible | takes damage | example                              |
|----------|--------------|--------------------------------------|
| yes      | yes          | ordinary enemy                       |
| yes      | no           | intact NPC                           |
| no       | yes          | soul-form the player can still hit   |
| no       | no           | immature-state actor (not yet killable, walkable-through) |

### Hurtbox layout

- `"hurtbox_decls"` — array of capsule declarations authored per
  archetype. Empty falls back to the shared humanoid layout that the
  player uses. Bosses and non-humanoids author their own.

### Clip family overrides

Every archetype resolves its idle / walk / attack / flinch / death
clip through `lookupArchetypeClip` in `Enemies.cpp` so the
per-skeleton registry is always honored. Empty means "use the shared
humanoid default for this family" (e.g. `walk_clip: ""` resolves to
`"walking"`). Override any clip family with a name from the
`skeleton_id` bundle's clip library.

Common overrides for a crawling archetype:

```jsonc
"spawn_clip": "zombie_crawl",
"walk_clip": "zombie_crawl",
"walk_back_clip": "zombie_crawl",
"strafe_left_clip": "zombie_crawl",
"strafe_right_clip": "zombie_crawl",
"run_clip": "zombie_crawl",
"idle_clip": "zombie_idle",
"death_clip": "zombie_death"
```

`spawn_clip` matters — without it the pipeline binds `idle_clip` on
frame 0 and the gait picker swaps to the crawl on frame 1+; the actor
renders as "standing for a split second" before going prone.
`spawn_clip` overrides the frame-0 bind.

### Actions

`"actions"` is a list of one thing the actor can do — see the
`EnemyAction` struct in the schema header. Each action carries a
clip, a range window, a cooldown, a weight, hitbox geometry, and
windup / active / recovery timing. The behavior tree's
`LeafPickAction` filters by range + cooldown + awareness and weighted-
randoms within what remains.

Non-combat archetypes leave `actions` as `[]`.

### Interaction

Interactables register automatically at spawn based on archetype
fields. Nothing to wire manually.

- `"is_npc": true` + `"display_name"` (or `"display_name_key"` for
  the language-map path) → Talk prompt registers, dialog tree loaded
  from `config/npcs/<id>.json`
- `"examine_text"` non-empty OR `"examine_text_key"` set → Examine
  prompt registers; press-E surfaces the text through the language
  map
- `"interact_range_meters": 0.0` → uses the default (2.5m Talk, 2.0m
  Examine)

### Boss fields

Non-boss archetypes leave every boss field defaulted (`is_boss:
false`). Boss authoring is documented separately — see the header's
"Boss fields" section for the Pattern A / Pattern B state-machine
details.

### Transformation-over-time

`"transform_target_archetype"` — an archetype id whose appearance
this actor lerps TOWARD over its arrival-wait period.
`resolveAppearance` interpolates every appearance axis (colour, body
scale, all future fields) simultaneously from the wait fraction. On
arrival, the spawn-flow's `on_arrival_action: "convert_to:<target>"`
fires and the actor's archetype swap completes the transition. The
swap uses `applyArchetypeSwap` which rebuilds the Jolt capsule if the
collider dimensions OR the intangibility flag changed.

## Baking a new humanoid mesh

The bake pipeline is a standalone script that runs an offline
authoring app in headless mode. Inputs → outputs mapping:

```
inputs                              →  outputs at assets/characters/enemies/<id>/
  --gender {male,female}                 humanoid.glb          (skinned mesh + skeleton + morphs)
  --skin-diffuse path/to/skin.png        (baked into the .glb)
  --eyes-mhclo path/to/eyes.mhclo        (eyes joined into body mesh)
  --eyes-diffuse path/to/iris.png        (iris texture)
  --out-blend / --out-fbx / --out-glb    (three output formats; .glb is the shipped one)
```

The script is at
[`scripts/blender/gen_humanoid.py`](../scripts/blender/gen_humanoid.py).
It:

1. Loads the humanoid base mesh in the requested gender variant.
2. Assigns the skin diffuse as the material's albedo texture.
3. Attaches the eye asset — imports the eyes as a submesh, strips the
   cornea vertices (identified by their UV region), and joins the
   eye mesh into the body mesh so the glTF exports as one mesh with
   two primitives (one per material slot). Attaches BEFORE the
   modifier-bake stage so the eye's vertex indices don't shift out of
   range.
4. Bakes shape-key modifiers and exports.

The output `.glb` is single-mesh + multi-primitive. The runtime
mesh loader ([`src/anim/SkeletalMesh.cpp`](../src/anim/SkeletalMesh.cpp))
iterates every primitive and uploads a material per primitive; the
render path handles multi-primitive skeletal meshes transparently.

### When to bake a new .glb vs re-use an existing one

- Different silhouette (body scale, gender, skeleton family) →
  new bake
- Different skin tone / colour / diffuse detail → same skeleton,
  new bake with a different `--skin-diffuse`
- Same skeleton + same diffuse + different appearance-tuning (per-actor
  colour tint, body-scale multiplier) → NO new bake. Author a new
  `config/characters/<id>.json` that points at the same mesh and
  applies a different colour multiplier or body-scale value.

The appearance file is fast to iterate; the bake is slow. Prefer
appearance-only variants whenever the silhouette is unchanged.

## Collision layers

The Jolt layer setup lives in
[`engines/engine/src/physics/PhysicsWorld.cpp`](../../../engines/engine/src/physics/PhysicsWorld.cpp)
under `namespace Layers`. Three layers:

| layer         | collides with          | used for                                        |
|---------------|------------------------|-------------------------------------------------|
| `NON_MOVING`  | `MOVING` only          | static terrain, architecture, doors             |
| `MOVING`      | `NON_MOVING`, `MOVING` | ordinary characters (player, standard enemies)  |
| `INCORPOREAL` | `NON_MOVING` only      | actors sharing space with the player (walkable-through) |

An archetype with `"intangible": true` puts its spawned actors in the
`INCORPOREAL` layer. Gravity + ground snap + hazard avoidance all work
through the normal single physics path — the layer is the only thing
that changes. The player walks straight through them, they walk
through each other, and they still fall down slopes and clamp to
terrain.

On an archetype swap that flips the intangibility flag,
`applyArchetypeSwap` treats it as a body-rebuild trigger (same shape
as a collider-dim change). No special-case guards elsewhere in the
tick.

## Registering a new archetype: the full checklist

1. Pick an id `<archetype_id>` — snake_case, e.g. `foundling`.
2. Create `config/enemies/<archetype_id>.json` — mirror an existing
   archetype whose behaviour is closest to what you want (an ambient
   mob for a walker, a boss archetype for a boss).
3. Create `config/characters/<archetype_id>.json` with the desired
   colour + body scale + morph baseline.
4. If the archetype needs its own mesh: run the bake script with the
   archetype-specific skin diffuse to produce
   `assets/characters/enemies/<archetype_id>/humanoid.glb`. Otherwise
   point `mesh_path` at an existing archetype's mesh.
5. If the archetype should spawn: add it to a spawn flow at
   `config/spawn_flows/<flow_id>.json` (or reference it from another
   archetype's `transform_target_archetype` if it enters via a swap).
6. If the archetype has dialogue: create
   `config/npcs/<archetype_id>.json` with the dialog tree.
7. If the archetype has an examine text: add the language-map entries
   at `config/lang/<domain>.json`.
8. If the archetype is examinable AND should promote insight on first
   examine: register the insight node at `config/insight/<domain>.json`.

Everything else — hurtboxes, hitboxes, lockon points, HP pool, poise
pool, currency drop, loot drops, interactable range, boss lifecycle,
face-morph variance — lives in the archetype JSON. No C++ changes
required for a new archetype that fits the shared humanoid mould.

## What NOT to add to an archetype

- **Hardcoded lore text.** Player-facing strings (display name,
  examine prompt, boss HP-bar name, felled overlay) go through the
  language map. The archetype references a `_key` string; the
  language map holds the tiered text with insight-gated reveals.
- **Special-case code in `Enemies.cpp`.** If an archetype needs a
  behavior the schema can't express, add a schema field first (data-
  driven), then let every archetype opt in via JSON. `applyArchetypeToActor`
  is a diamond funnel — every archetype-derived field flows through
  one site. Bypassing it creates the dual-source-of-truth bug class
  the codebase spends a lot of energy avoiding.
- **A new skeleton for a humanoid variant.** The two shared humanoid
  bundles (male / female) cover every humanoid enemy today. Different
  colour / silhouette / skin tone are per-archetype mesh bakes on
  top of one of those skeletons, not new skeletons.

## Related reading

- [`docs/systems.md`](systems.md) *Gameplay* — runtime tick behaviour
  (`spawnEnemyFromDecl`, `applyArchetypeSwap`, `applyArchetypeToActor`,
  `FlowSpawner`, `BehaviorTree`)
- [`docs/design/animals_and_multi_skeleton.md`](design/animals_and_multi_skeleton.md)
  — non-humanoid architecture (per-skeleton hurtbox layouts, per-
  skeleton joint maps, adding a new skeleton bundle)
- [`docs/design/bestiary.md`](design/bestiary.md) — design canon for
  the roster
- `include/gameplay/EnemyArchetype.h` — the authoritative schema; when
  in doubt, read the field's comment there
