# scene.json schema

Each scene lives in `games/selva-oscura/assets/scenes/<scene_id>/` and is
declared by `scene.json` in that folder. The engine's `JsonScene` class
consumes any scene.json — no per-scene C++ code.

## Top-level structure

```json
{
  "schema_version": 1,
  "scene_id": "surface",
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

Present iff the scene has heightmap terrain. Null for interior scenes.

```json
{
  "config": "assets/world/terrain/config.json",  // existing terrain config
  "region": "selva_inner"                          // which region in config
}
```

### `static_meshes` (array)

Each entry: a .glb file registered into the scene, with optional
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

Surface-only. Applied during terrain mesh build. Empty array for
interior scenes.

```json
{
  "center_xz": [0.0, -210.0],
  "half_extents_xz": [3.0, 4.0],
  "mode": "FlushAt",                    // FlushAt | FlushSlope | DepressTo | AddDelta
  "value": 32.25,
  "value_far": 0.0,                     // FlushSlope only
  "slope_axis": "Z",                    // FlushSlope only: X or Z
  "blend_pad": 2.0,
  "debug_name": "chapel_plateau"
}
```

### `triggers` (array)

Trigger volumes that initiate scene transitions when the player enters.
Edge-triggered (fires on first frame of overlap), one-way (paired by
declaration on the target scene).

```json
{
  "id": "chapel_door_enter",            // unique per scene, used for save/load
  "center": [0.0, 33.0, -206.0],
  "half_extents": [0.6, 1.2, 0.4],
  "target_scene": "chapel_interior",
  "target_spawn_pos": [0.0, 0.0, -0.5], // in target scene's local coords
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

### `default_spawn`

Used for NEW characters when no save data dictates a position. One scene
in the game is marked `is_default_spawn_scene: true` at the engine
level (set via a top-level scene registry config); that scene's
`default_spawn` is the new-character spawn point.

## Example: SurfaceScene

```json
{
  "schema_version": 1,
  "scene_id": "surface",
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
      "target_scene": "chapel_interior",
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

## Example: ChapelInteriorScene

```json
{
  "schema_version": 1,
  "scene_id": "chapel_interior",
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
      "target_scene": "surface",
      "target_spawn_pos": [0.0, 33.0, -205.0],
      "target_yaw": 0.0,
      "transition_mode": "Fade",
      "fade_duration_seconds": 0.4,
      "debug_name": "chapel_door_out"
    },
    {
      "id": "acheron_descent",
      "center": [0.0, -30.0, 140.0],
      "half_extents": [2.0, 1.0, 1.0],
      "target_scene": "acheron",
      "target_spawn_pos": [0.0, 0.0, 5.0],
      "target_yaw": 3.14159,
      "transition_mode": "Fade",
      "fade_duration_seconds": 0.6,
      "debug_name": "acheron_arrival"
    }
  ],
  "ambient": { "color": [0.06, 0.05, 0.04] },
  "sky": { "kind": "void" },
  "footstep_default": "footstep_concrete"
}
```

## Engine top-level config: `assets/scenes/scenes.json`

```json
{
  "schema_version": 1,
  "scenes": ["surface", "chapel_interior", "acheron"],
  "default_spawn_scene": "surface"
}
```
