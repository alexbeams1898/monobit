# region.json schema

Each region lives in `games/selva-oscura/assets/regions/<region_id>/` and is
declared by `region.json` in that folder. The engine's `JsonRegion` class
consumes any region.json — no per-region C++ code.

## Top-level structure

```json
{
  "schema_version": 1,
  "region_id": "surface",
  "debug_name": "Selva surface (outdoor)",
  "terrain": { ... } | null,
  "static_meshes": [ ... ],
  "terrain_modifiers": [ ... ],
  "triggers": [ ... ],
  "ambient": { ... },
  "sky": { ... },
  "footstep_default": "footstep_grass",
  "audio_bed": "selva_ambient" | null,
  "default_spawn": { "pos": [x, y, z], "yaw": 0.0 }
}
```

### `terrain` (object | null)

Present iff the region has heightmap terrain. Null for interior regions.

```json
{
  "config": "assets/world/terrain/config.json",  // existing terrain config
  "region": "selva_inner"                          // which region in config
}
```

### `static_meshes` (array)

Each entry: a .glb file registered into the region, with optional
world-space offset and surface tag.

```json
{
  "path": "assets/world/static_meshes/crypt_exterior.glb",
  "world_origin": [0.0, 32.25, -210.0],
  "surface_tag": "Architecture",        // Terrain | Architecture | Foliage
  "debug_name": "chapel_exterior"
}
```

### `terrain_modifiers` (array)

Per-region terrain modifiers, registered into the global modifier
registry at boot BEFORE `initTerrain()` so the per-vertex mesh build
sees them. Authoritative source of truth: this array (the C++
`registerChapelTerrainModifiers` was retired in favor of this).

```json
{
  "debug_name": "chapel_exterior_plateau", // optional; F1 modifier overlay label
  "terrain_region": "selva_inner",         // optional; scopes modifier to one terrain region
                                            // (matches a name in terrain/config.json).
                                            // OMITTED = applies to any terrain region whose
                                            // XZ AABB contains the query (legacy behavior).
  "mode": "FlushAt",                       // FlushAt | FlushSlope | DepressTo | AddDelta | Hole
  "center_xz": [0.0, -210.0],
  "half_extents_xz": [3.0, 4.0],
  "value": 22.0,                            // mode-dependent meaning
  "value_far": 25.08,                       // FlushSlope only: Y at +axis edge
  "slope_axis": "Z",                        // FlushSlope only: "X" or "Z"
  "blend_pad": 2.0,                         // default blend distance into surrounding terrain
  "blend_pad_neg_x": 5.0,                   // optional per-side overrides (default -1 = use blend_pad)
  "blend_pad_pos_x": -1.0,
  "blend_pad_neg_z": 5.0,
  "blend_pad_pos_z": 0.0                    // 0 = sharp edge (no blend on this side)
}
```

**Why the `terrain_region` field exists:** the JsonRegion that OWNS
a modifier (e.g. `surface/region.json`) is not always the same as the
terrain region the modifier affects (e.g. the Acheron trench is
authored in `surface/region.json` today but targets the `"limbo"`
terrain region). The field is explicit so the modifier author can
target any terrain region without coupling JsonRegion identity to
terrain-region identity.

### `triggers` (array)

Trigger volumes that initiate region transitions when the player enters.
Edge-triggered (fires on first frame of overlap), one-way (paired by
declaration on the target region).

```json
{
  "id": "chapel_door_enter",            // unique per region, used for save/load
  "center": [0.0, 33.0, -206.0],
  "half_extents": [0.6, 1.2, 0.4],
  "target_region": "chapel_interior",
  "target_spawn_pos": [0.0, 0.0, -0.5], // in target region's local coords
  "override_yaw": false,                 // optional; default false = preserve player yaw across transition (seamless-traversal default)
  "target_yaw": 3.14159,                 // only applied if override_yaw=true
  "transition_mode": "Instant",          // Instant | Fade | Continuous
  "fade_duration_seconds": 0.0,          // ignored for Instant; use 0.4-0.6 for cinematic Fade transitions only
  "debug_name": "chapel_door_in"
}
```

### `ambient` (object)

```json
{
  "color": [0.16, 0.18, 0.22],          // engine clear color
  "fog_color": [...],
  "fog_density": 0.0
}
```

### `sky` (object)

```json
{
  "kind": "atmospheric" | "skybox" | "void",
  "skybox_path": "..."                   // skybox only
}
```

### `territory` (array)

World-space AABBs declaring this region's cosmological law-domain.
The set of all territories across all regions partitions the world:
any point belongs to exactly one region (resolved by priority then
smallest volume) or to none (the void). Used by the AI chase gate
(an actor drops chase when the player is in foreign territory) and
will be the lookup target for future region-derived systems (audio
ambiance, lighting, etc.). A single source of truth for "which
region claims this position."

```json
{
  "debug_name": "chapel_descent_corridor",
  "center": [0.0, -11.305, -283.15],
  "half_extents": [2.4, 2.5, 76.97],
  "rotation_euler_deg": [-24.55, 0.0, 0.0],
  "priority": 0
}
```

`rotation_euler_deg` is optional Euler XYZ in degrees (glm
composition: rotate X, then Y, then Z). Identity (omit field) =
axis-aligned box. Non-identity = oriented bounding box (OBB) — use
for ramps and any rotated structure where an AABB would leave
unowned wedges of empty airspace.

`priority` defaults to 0. Higher priority wins overlap; ties broken
by smallest volume (innermost). The corridor sitting inside Limbo's
disc resolves correctly at priority 0 because the corridor volume is
much smaller than the disc.

Each volume's owner is implicitly the region it's declared in.
Authoring lives in `world/Territory.h` (engine-side); integration
with AI is in `BehaviorTree.cpp::LeafMoveToTarget` (queries
`regionIdAtPosition` for the player-in-foreign-territory chase
drop) and `PerFrameTick.cpp::clampActorsToOwnTerritory` (per-frame
hard-clamp that pushes any actor outside its own region back to
the nearest boundary face).

### `enemy_spawns` (array)

Per-instance enemy placement for this region. Each entry references an
archetype declared in `config/enemies/<archetype>.json` and places one
actor in the world at boot.

```json
{
  "id": "limbo_shade_riverbank_01",      // unique within this region; persists across cycles for save state
  "archetype": "limbo_shade",             // archetype lookup id from config/enemies/
  "pos": [12.5, -43.13, -18.0],           // world XYZ; OR pos[1] can be the string "auto_terrain"
  "yaw": 1.57,                            // facing radians (optional, default 0)
  "permanent_on_death": false,            // optional, default false. true = "felled keeper does not respawn" per setting.md
  "patrol_path": [[12.5, -43.13, -18.0], [8.0, -43.13, -22.0]]  // optional roaming waypoints; ignored until AI_Roaming lands
}
```

**Required:** `id`, `archetype`, `pos`. Missing any of these throws at
boot rather than silently spawning nothing -- the same fail-loudly
contract as the rest of the region schema.

**`pos[1]` sentinel:** numeric Y is the literal world Y (default
authoring). The string `"auto_terrain"` is a sentinel meaning
"sample `groundHeight(x, z)` at spawn-time and use that Y." Useful
for actors authored on a slope where the exact Y is tedious to
hand-pick. The 16 Limbo shades all use literal Y=-43.13 (flat disc
floor); the wolf placeholder on the colle's south slope uses
`"auto_terrain"` because the slope's heightmap isn't trivially
sample-able from the JSON. Example:

```json
"pos": [0.0, "auto_terrain", -90.0]
```

**Respawn semantics** are the canonical cycle-flow model per
`docs/design/setting.md` *Per-circle reactivity* + *Cycle structure*:

- `permanent_on_death: false` (shades, default) — re-spawn on every
  new cycle. The cycle boundary is *player second-death* OR *new
  game / load game*. Per cosmology: Hell re-streams souls into their
  punishment positions every cycle.
- `permanent_on_death: true` (keepers) — once felled, stay felled
  across cycles. Per *fallback.md* "Class persists across cycles /
  Keepers felled" — a felled keeper does not respawn.

There is NO per-enemy respawn timer. The doctrine is intentional:
Hell is timeless (no day/night, no aging); enemies don't "tick back
to life" while the player watches. They re-flow on the next descent.

### `default_spawn`

Used for NEW characters when no save data dictates a position. One region
in the game is marked `is_default_spawn_region: true` at the engine
level (set via a top-level region registry config); that region's
`default_spawn` is the new-character spawn point.

## Example: SurfaceRegion

```json
{
  "schema_version": 1,
  "region_id": "surface",
  "debug_name": "Selva surface",
  "terrain": {
    "config": "assets/world/terrain/config.json",
    "region": "selva_inner"
  },
  "static_meshes": [
    {
      "path": "assets/world/static_meshes/crypt_exterior.glb",
      "world_origin": [0.0, 32.25, -210.0],
      "surface_tag": "Architecture",
      "debug_name": "chapel_exterior"
    }
  ],
  "terrain_modifiers": [
    {
      "center_xz": [0.0, -210.0],
      "half_extents_xz": [3.0, 4.0],
      "mode": "FlushAt",
      "value": 32.25,
      "blend_pad": 2.0,
      "debug_name": "chapel_plateau"
    }
  ],
  "triggers": [
    {
      "id": "chapel_door_enter",
      "center": [0.0, 33.0, -206.0],
      "half_extents": [0.6, 1.2, 0.4],
      "target_region": "chapel_interior",
      "target_spawn_pos": [0.0, 0.5, -0.5],
      "target_yaw": 3.14159,
      "transition_mode": "Fade",
      "fade_duration_seconds": 0.4,
      "debug_name": "chapel_door_in"
    }
  ],
  "ambient": { "color": [0.16, 0.18, 0.22] },
  "sky": { "kind": "atmospheric" },
  "footstep_default": "footstep_grass",
  "default_spawn": { "pos": [0.0, 0.0, 0.0], "yaw": 0.0 }
}
```

## Example: ChapelInteriorRegion

```json
{
  "schema_version": 1,
  "region_id": "chapel_interior",
  "debug_name": "Chapel + descent",
  "terrain": null,
  "static_meshes": [
    {
      "path": "assets/world/static_meshes/crypt_interior.glb",
      "world_origin": [0.0, 0.0, 0.0],
      "surface_tag": "Architecture",
      "debug_name": "chapel_interior_mesh"
    }
  ],
  "terrain_modifiers": [],
  "triggers": [
    {
      "id": "chapel_door_exit",
      "center": [0.0, 1.0, 4.0],
      "half_extents": [0.6, 1.2, 0.4],
      "target_region": "surface",
      "target_spawn_pos": [0.0, 33.0, -205.0],
      "target_yaw": 0.0,
      "transition_mode": "Fade",
      "fade_duration_seconds": 0.4,
      "debug_name": "chapel_door_out"
    },
    {
      "id": "limbo_descent",
      "center": [0.0, -30.0, 140.0],
      "half_extents": [2.0, 1.0, 1.0],
      "target_region": "limbo",
      "target_spawn_pos": [0.0, 0.0, 5.0],
      "target_yaw": 3.14159,
      "transition_mode": "Fade",
      "fade_duration_seconds": 0.6,
      "debug_name": "limbo_arrival"
    }
  ],
  "ambient": { "color": [0.06, 0.05, 0.04] },
  "sky": { "kind": "void" },
  "footstep_default": "footstep_concrete"
}
```

## Engine top-level config: `assets/regions/regions.json`

```json
{
  "schema_version": 1,
  "regions": ["surface", "chapel_interior", "limbo"],
  "default_spawn_region": "surface"
}
```
