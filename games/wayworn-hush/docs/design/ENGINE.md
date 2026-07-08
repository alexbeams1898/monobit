# Wayworn Hush — Engineering / Architecture

> **Owns:** per-game engineering notes for Wayworn Hush. Engine-wide doctrine
> lives in [`engines/engine/docs/ENGINE.md`](../../../../engines/engine/docs/ENGINE.md).
>
> **Status:** initial doc — scaffold intent, not shipped architecture.

## Engine reuse

Wayworn Hush runs on `engines/engine/` — the same custom C++ engine that
powers prison-escape-game and selva-oscura. The engine is game-agnostic
(zero includes of game headers) and provides the 2D pipeline the new
game needs.

### From engine core (100% reusable)

- **ECS core** (entt registry, generic components: Transform, Velocity,
  Sprite, Animation, Collider)
- **Fixed-step update loop + render-callback model** — engine owns the
  frame, game registers `setRenderWorld` / `setRenderUI` / `setRenderDebug`
  callbacks
- **`RenderSystem`** — sprite quads + Y-sort
- **`TileMapRenderer`** — 2D tilemap rendering
- **`AnimationSystem`** — sprite-sheet animation, direction-based rows
- **`MovementSystem`** — axis-split velocity projection with static
  depenetration
- **`CollisionSystem`** — dynamic-vs-dynamic MTV correction, event
  emission
- **`CameraSystem`** — camera-follow with interpolation
- **`TextureManager` / `FontManager`** — asset loading
- **`AudioSystem`** — FMOD-backed audio playback
- **`UIRenderer`** — screen-space overlays
- **`SpriteCompositor`** — layered sprite composition

### From prison-escape-game (fork-and-adapt)

Prison-escape has a near-complete 2D game skeleton. The following
patterns are directly reusable with adaptation:

- **`main.cpp`** — game entry point, engine init, render-callback
  registration, main loop
- **`GameLoop.cpp`** — per-frame game-side update sequence
- **`ConfigLoader.cpp`** — JSON config loading for entities, items,
  animations
- **`TileMapLoader.cpp`** — tilemap loading from config
- **`SaveManager.cpp`** — save/load framework (save schema will differ)
- **`WorldInit.cpp`** — game-side world / entity initialization
- **Screen system** — title / main / pause / menu screens
- **Ops helpers**:
  - `InventoryOps` — pickup, drop, use (directly applicable)
  - `ItemOps` — item registry, item-def loading (directly applicable)
  - `CraftingOps` — recipe system (usable for simple crafting if
    included, otherwise skippable)
- **Dialog templates** — `ConfirmDialog`, `MenuDialog` (may need extension
  for inner-monologue system)
- **Config-driven-everything convention** — JSON files author all
  content; the code has no game-content constants

### From selva-oscura (limited reuse)

Selva is 3D. Most systems (3D rendering, MPFB2 humanoid pipeline, 3D
animation retarget, region streaming) do not apply.

Selva DID ship shared systems that MIGHT apply to Wayworn Hush:

- **`selva::lang`** module — tiered string map with insight-gated
  promotion. Could power the inner-monologue-shifts-across-game
  mechanic — as the protagonist's self-understanding grows, string
  entries tier-promote to deeper voice.
- **`selva::insight`** module — the insight-graph backend that drives
  string tier promotion. Could power "the protagonist learned
  something" flags.

Both are reusable if the language tier / insight-node model fits the
game's authoring needs. Not a hard dependency — could also author
inner monologues directly via flags without the full tier system.

## What to build fresh

- **Turn-based combat system** — prison-escape is action-combat. New
  system needed. Menu-driven, action-select, target-select, resolve.
  Undertale-style dodge phase (real-time between turns) is optional
  extension.
- **Overworld / region traversal system** — traveling between regions,
  managing large map spaces. Prison-escape is single-dungeon-per-run;
  wayworn-hush is multi-region open exploration.
- **Weather system** — rain, snow, mist, clear, fog. Visual-only initial
  slice; expand to affect encounters later. Not in prison-escape or
  engine.
- **Day/night cycle** — 24hr compressed real-time clock; affects
  lighting, ambient encounters, camping availability.
- **Camping mechanic** — designated safe spots; UI for tent, fire,
  pass-time; save-at-camp; possible narrative-vignette triggers.
- **Skills-as-verbs system** — acquired skills that apply in specific
  contexts (cold-crossing, plant-gathering, wildlife-calming). Not the
  same as ability trees or spell slots. Config-driven skill definitions;
  environmental-context triggers.
- **Inner-monologue trigger system** — contextual triggers that pop up
  the protagonist's thoughts. Similar to insight-firing but for
  narrative text, not gameplay flags. Might fold into selva::insight.
- **Ambient audio layering system** — region-specific ambient beds
  with layer transitions on region-switch, weather-change,
  time-of-day-change. FMOD is present; the compositional pipeline is new.

## Directory layout (mirrors prison-escape)

```
games/wayworn-hush/
├── src/
│   ├── main.cpp
│   ├── GameLoop.cpp
│   ├── ConfigLoader.cpp
│   ├── TileMapLoader.cpp
│   ├── SaveManager.cpp
│   ├── WorldInit.cpp
│   ├── ops/                    # helper trees (InventoryOps, etc.)
│   ├── renderers/              # game-side render passes
│   ├── screens/                # title / pause / menu / dialogue
│   └── systems/                # game systems (CombatSystem, WeatherSystem, ...)
├── include/
│   └── ...                     # public headers for the above
├── config/
│   ├── entities/               # per-actor JSON
│   ├── items/                  # per-item JSON
│   ├── animations/             # sprite-sheet manifests
│   ├── regions/                # region definitions (tilemap + ambient bed + weather profile)
│   ├── skills/                 # skill definitions
│   ├── monologue/              # inner-monologue triggers + text
│   └── audio/                  # ambient bed definitions
├── assets/
│   ├── sprites/                # pixel art (character, enemies, tiles)
│   ├── tilemaps/               # authored region tilemaps
│   ├── audio/                  # music + SFX
│   └── ui/                     # UI atlases
├── docs/
│   └── design/                 # DESIGN.md, ENGINE.md, AESTHETIC.md
├── scripts/                    # build helpers, asset bakers
└── tests/                      # Catch2 tests (per engine's testing policy)
```

## CMake integration

- Add `add_subdirectory(games/wayworn-hush)` to top-level CMakeLists.txt
- `games/wayworn-hush/CMakeLists.txt` links against the shared engine
  library (`engines/engine`), pulls in nlohmann_json / SDL2 / FMOD /
  entt from shared dependencies
- Configure with `-DBUILD_WAYWORN_HUSH=ON` (or default-on for dev)
- Separate exe target: `wayworn-hush.exe`
- Follow prison-escape-game's CMakeLists.txt as the reuse template

## Save data

- **Path:** `%APPDATA%/WayWornHush/` (Windows) — identifies the game,
  not the repo, per monobit convention
- **Save schema:** location, inventory, learned skills, monologue
  flags fired, region-visited flags, weather-state (optional), clock
- **Multi-save-slot** support (3 slots recommended; matches the
  small-scale intimate register)

## Testing policy

Same as engine-wide policy:
- Always add Catch2 tests in the same PR — no deferring
- Test anything that runs without window/GPU
- Systems needing SDL keyboard or GL context are integration-tested
  by running the game; document the exclusion in the test file

## Cross-references

- [`engines/engine/docs/ENGINE.md`](../../../../engines/engine/docs/ENGINE.md) —
  engine-wide doctrine
- [`games/prison-escape-game/docs/ENGINE.md`](../../../prison-escape-game/docs/ENGINE.md) —
  per-game architectural notes; reuse template
- [DESIGN.md](DESIGN.md) — high-level game concept
- [AESTHETIC.md](AESTHETIC.md) — art / music direction
