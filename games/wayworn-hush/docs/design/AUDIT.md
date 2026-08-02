# Wayworn Hush — Reuse Audit

> **Owns:** the survey of what already exists in the repo that Wayworn Hush can
> reuse, and what must be built fresh. Source-of-truth for the "don't reinvent
> it" question.
>
> **Status:** initial audit — findings from a source-level walk of
> `engines/engine/`, `games/prison-escape-game/`, and `games/selva-oscura/`.
> Where the existing `ENGINE.md` scaffold and the actual source disagreed, the
> source wins and the correction is noted.

## How to read this

Three trees were audited: the shared engine, the 2D action game (prison-escape),
and the 3D soulslike (selva-oscura, only two narrative modules). Every finding
is one of:

- **Reusable now** — exists, generic, works for a 2D free-movement exploration
  game as-is.
- **Reuse with adaptation** — good skeleton, but has action-combat / roguelike
  assumptions to strip or swap.
- **Not applicable** — prison-escape-specific or 3D-specific; leave it behind.
- **Build fresh** — doesn't exist anywhere; net-new for Wayworn Hush.

## Corrections to the scaffold ENGINE.md (docs were stale)

The initial `ENGINE.md` scaffold made several claims that the source does not
support. These matter because they change the build plan:

1. **Audio is miniaudio, not FMOD.** `AudioSystem.cpp` wraps miniaudio's
   `ma_engine` exclusively. FMOD is a dead link target in engine CMake — never
   included or called. (`ENGINE.md` scaffold said "FMOD-backed"; the engine-wide
   `docs/ENGINE.md` also says FMOD and is likewise stale.)
2. **GL is 3.3 core, not 4.3.** `Engine.cpp` requests a 3.3 core context; all
   shaders are `#version 330 core`.
3. **There is no engine-level `MovementSystem`.** Movement (axis-split
   integration, static depenetration) is **game-side** — prison-escape owns its
   `MovementSystem.cpp`. The engine ships only the tilemap-depenetration helper
   inside `CollisionSystem`.
4. **There is no offscreen FBO / render target in the engine.** RenderSystem
   draws straight to the default framebuffer. "Pixel-perfect" today is
   position-snapping (`round()`) + `GL_NEAREST` + half-texel UV insets, not an
   integer-scaled render target. The integer-upscale target Wayworn's aesthetic
   needs **does not exist yet** — see SCALE.md.
5. **There is no engine save framework.** Save/load is entirely game-side
   (prison-escape's `SaveManager`).

---

## 1. engines/engine/ — shared engine

The engine is a game-agnostic static library. It already backs a 2D game and a
3D game, so the tree contains a mature 2D sprite path **and** a large 3D/physics
subsystem that Wayworn ignores entirely.

### Reusable now (the 2D foundation)

| System | File | Notes |
|---|---|---|
| Engine main loop + callback model | `include/Engine.h`, `src/Engine.cpp` | 60 Hz fixed step, render-alpha interpolation, framebuffer clear, camera interp, window/GL/ImGui setup. Callback setters: `setGameUpdate` / `setPerFrameUpdate` / `setPreRender` / `setRenderWorld` / `setRenderDebug` / `setRenderUI` / `setOnResize`. This is the whole engine contract. |
| ECS core | `include/ecs/EntityManager.h` | Thin `entt::registry` wrapper; owns `TileMap`, `TileConfig`, input buffers, `render_alpha`, collision events. |
| Generic components | `include/ecs/Components.h` | `Transform` (2D+3D), `PreviousTransform` (interp), `Velocity`, `Sprite` (layer/sub_layer/flip/rotation/sort_anchor), `Collider`, `Camera`, `FacingDirection`, `MovementIntent`, `Animation` (full sheet layout, `CardinalDir` S/W/E/N), `Particle`, `Dead`, `Glow`, `TintOverride`, `SolidColor`. |
| Sprite rendering + Y-sort | `include/systems/RenderSystem.h`, `.cpp` | Layer → foot-Y → sub_layer depth sort; tint, flip, rotation, interpolation, pixel-snap. Draws to default framebuffer (no FBO). |
| Tilemap data + renderer | `include/TileMap.h`, `include/systems/TileMapRenderer.h`, `.cpp` | Flat row-major grid, atlas-UV tile config, one-VBO bake + one draw call, flat-color fallback. **`TILE_SIZE=32` is a shared `constexpr` here — being promoted to per-game (see SCALE.md).** |
| Sprite animation | `include/systems/AnimationSystem.h`, `.cpp` | Row = state, column = `dir*max_frames + frame`. Loop / one-shot-freeze / reverse / speed / per-frame mask. 4-dir sheets — exactly the Pokémon/Mother register. |
| Direction utils | `include/utils/DirectionUtils.h`, `.cpp` | `engine::direction` — 4-dir snap with anti-jitter hysteresis, sprite-column mapping. |
| Camera | `include/systems/CameraSystem.h`, `.cpp`; `CameraPanSystem` | Follow-snap + interpolation (interp lives in `Engine::render`); cutscene pans via `CameraPan`. |
| Collision | `include/systems/CollisionSystem.h`, `.cpp` | AABB overlap → `CollisionEvent` per pair; dyn-vs-dyn MTV; static-vs-tilemap depenetration. Free-movement collision — matches our Chrono-Trigger movement choice. |
| Audio (miniaudio) | `include/systems/AudioSystem.h`, `.cpp` | `playMusic` (stream, loop, fade), `playSfx` (32-voice pool), tracked/looping SFX with fade, master/music volume, **music low-pass** (menu muffle). OGG/Vorbis. |
| UI primitives | `include/UIRenderer.h`, `.cpp` | Batched screen-space quads + text: `drawRect`, `drawTexturedRect`, `drawText`, `measureText`, mid-frame scissor. Raw layer — no textbox/menu widget (those are game-side). |
| Fonts | `include/FontManager.h`, `.cpp` | `.ttf` via stb_truetype → glyph atlas; `loadFontGroup` packs sizes into one texture. ASCII 32–126. |
| Sprite compositor | `include/SpriteCompositor.h`, `.cpp` | CPU layer-blend → one GL texture, with per-layer **palette-swap** (base-RGB → target-RGB). Good for region/character recoloring. |
| Textures | `include/TextureManager.h`, `.cpp` | PNG → cached GL texture; magenta-checkerboard fallback. |
| Debug draw | `include/utils/DebugDraw.h` | World-space dot/rect/line/circle through UIRenderer; zero-cost when unused. |
| Logging | `include/log/Log.h`, `.cpp` | `engine::log` — named channels, levels, file/stderr sinks. |

### Reuse with adaptation

| System | File | Adaptation |
|---|---|---|
| Item/inventory data model | `include/ecs/Items.h` | `ItemDef`/`ItemRegistry`/`Inventory` are generic; the weapon/armor/evolution fields are action-RPG. Keep base fields, drop combat. |
| Ops helpers | `include/ops/InventoryOps.h`, `ops/CraftingOps.h` | Engine-level generic versions exist (repo is mid-generalization). Inventory add/remove/count/consume + crafting are clean; equip/evolve are action-RPG. |
| RPG components | `include/ecs/RpgComponents.h` | `Stats`/`Experience` reusable if we want light stats; the rest (Poise/Stamina/Parry/Weapon) is combat. |
| Config loaders | `include/ecs/ConfigLoaders.h`, `.cpp` | Generic JSON loaders, but for the RPG/item schemas specifically. Pattern reusable; content-specific. |

### Not applicable (3D subsystem — ignore entirely)

`physics/PhysicsWorld` (Jolt), `world/Region`, `world/Territory`,
`world/TerrainModifiers`, `world/StructureFootprints`, `world/AsyncRegionLoader`,
`world/Lights` (3D point-light registry — note: it has flicker fields, but there
is **no 2D light renderer**). `FlowFieldSystem` + `SteeringSystem` exist and are
generic, but are combat-AI navigation — not needed for a game with rare,
turn-based encounters and no chasing enemies.

### Build fresh (doesn't exist at engine level)

Integer-scaling render target (FBO + upscale blit), keybinding/remap, save
framework, dialog/textbox/menu widgets, day/night, weather, particle **emitter**
(only the `Particle` component + its render fade exist; spawning/ticking is
game-side), any 2D lighting.

---

## 2. games/prison-escape-game/ — 2D game shell

Prison-escape is a twin-stick action roguelike. The **application shell** forks
cleanly; the **combat/wave/AI layer** does not transfer at all.

### Copy verbatim (or nearly)

| File | Why |
|---|---|
| `main.cpp` crash-handler block (lines ~1–89) | Signal handlers → `crash.log`; game-agnostic. |
| `main.cpp` callback-registration block (lines ~192–200) | The 7 setters + `run()` — the template for wiring any game to the engine. |
| `screens/ConfirmDialog.{h,cpp}` | Self-contained auto-sizing yes/no dialog. Zero coupling. |
| `screens/MenuDialog.{h,cpp}` | Auto-sizing option-list dialog (label+desc+hint, kbd+mouse). **The single strongest lift** — base for all Wayworn menus. |
| `screens/ScreenColors.h`, `ScreenInput.h` | UI palette constants + input-glue helpers. Retheme colors. |
| `systems/NotificationSystem.{h,cpp}` | Floating text toasts. Generic. |
| `systems/AmbientSoundSystem.{h,cpp}` | Randomized interval sounds from an entity's pool. Ideal for overworld ambience. |
| `systems/TintSystem.{h,cpp}` | Timed sprite tint (flash). Generic. |
| `ops/UpdateChecker.{h,cpp}` | GitHub-release version check. App plumbing. |
| `Version.h.in` | CMake version template. |

### Fork the skeleton, gut the body

| File | Keep / Replace |
|---|---|
| `main.cpp` (whole) | Keep crash + callback blocks; replace the ~18 `ctx().emplace<>` singletons, config-load list, and screen-init list with Wayworn's. |
| `GameLoop.cpp` + `GameLoop.h` | Keep the 7-callback dispatch + `GameState` phase machine; delete all combat/lock-on/wave/attack-token logic. |
| `ConfigLoader.cpp` + `.h` | Keep the **dispatch-table mechanism** (`kComponentLoaders`: string key → loader fn, JSON cache). Register a different component set (drop weapon/poise/stamina/loot/ai_controller). |
| `SaveManager.cpp` + `.h` | Keep the **entire I/O + migration framework** (`SDL_GetPrefPath` dir, `schema_version` + `migrate()`, JSON read/write). Replace the DTO structs (RunStats/Run/leaderboard → Wayworn's map-position/inventory/skills/monologue-flags/clock). Change save-dir string to `WayWornHush`. |
| `TileMapLoader.cpp` + `.h` | Keep `parseRoom`/`loadConfig`/`stampRoom`; **replace `generate()`'s procedural room-placement with authored-map loading.** Wayworn's overworld is hand-authored, not carved. |
| `WorldInit.cpp` + `.h` | Keep the `createWorld`/`destroyWorld` seam; rewrite the body (no procgen, no wave start, no god-mode item-dump). |
| `systems/InputMappingSystem.{h,cpp}` | Keep "one system: raw input → intent struct"; remap actions (attack/dodge/block → move + interact + menu). |
| `systems/MovementSystem.{h,cpp}` | Keep the tile-collision query; **our movement is free pixel (Chrono Trigger), so the continuous integrator mostly transfers as-is** — this is the cheaper path. |
| `systems/AnimStateSystem.{h,cpp}` | Keep the AnimState → Animation-row resolver; keep Idle/Walk, drop Attack/Hit/Death combat states. |
| `screens/MainMenuScreen`, `PauseMenu`, `SettingsScreen`, `ControlsScreen`, `LoadGameScreen` | Reusable skeletons; drop HighScores, retheme, rewrite controls text. |
| `renderers/HudRenderer.{h,cpp}` | Keep the overlay framework; the aesthetic says **no HUD during exploration**, so this is combat/menu-only. |
| `renderers/InteractionPromptRenderer.{h,cpp}` | The "[F] pick up X" world-space prompt is exactly the overworld "talk/examine" prompt — retarget. |
| `systems/PickupSystem.{h,cpp}` | Keep the interact→pickup seam; drop combat coupling. |
| `ecs/AppState.h`, `ecs/ItemConfig.h` | Data-model shells: keep the UIState/GameState phase enums + registry-keyed-by-path pattern; define Wayworn's save schema + item fields here. |

### Not applicable (leave behind entirely)

All combat/wave/AI: `CombatSystem`, `DamageSystem`, `DeathSystem`,
`ProjectileSystem`, `AggroSystem`, `ChaseSystem`, `SpawnerSystem`, `WaveSystem`,
`WeaponSpriteSystem`, `WeaponXPSystem`, `EquipmentSystem`, `LevelingSystem`,
`RestSpotSystem`, `LadderSystem`. Renderers: `Crosshair`, `FacingDot`,
`AIDebugOverlay`, `AIRecorder`, `ItemStat`. Screens: `LevelUp`, `Sanctuary`,
`GameOver`, `Victory`, `RunSummary`, `HighScores`. Data: `GameComponents.h`
(twin-stick actions), `BalanceConfig.h` (combat tuning). All combat/wave/
evolution config content.

### The config-driven entity schema (reuse the SHAPE)

Strong reuse. The shapes are game-agnostic; only the content and the registered
component set change.

- **Entity**: `{ "tag": <string>, "components": { <key>: <fields> } }`. Loader
  creates entity, emplaces `Tag`, dispatches each component key through a
  string→function map. Unknown keys warn, don't fail. All fields use
  `j.value("field", default)` so partial configs work.
- **Item**: flat JSON → `ItemDef`. Base fields `name/description/icon/category/
  rarity/stackable/max_stack/value`. Keyed in the registry by config path.
- **Animation manifest**: `{ texture, frame_width, frame_height, states: {
  <name>: {row, frames, duration} } }`. The `states` map is a clean reusable
  spritesheet schema. (Prison-escape's `hand_anchors` block is weapon-rigging —
  drop it.)
- **Appearance manifest** (LPC paperdoll): `{ frame_size, categories: [...] }`.
  Reusable if Wayworn has a customizable player. **We're authoring all sprites
  from scratch**, so this is optional, not a dependency.

### CMake template

Prison-escape's `CMakeLists.txt` is the fork template: per-game `project()` +
version (release pipeline reads it), a STATIC `game-systems` lib linking
`PUBLIC engine`, `Version.h` generation, a per-game `build/bin/<game>/` output
dir (so two games' configs don't clobber), a `sync-assets-and-config ALL` custom
target (copies `assets/`+`config/` every build so JSON/PNG edits sync without a
relink), and a `game-tests` Catch2 target. Rename targets, swap the source list,
keep the rest.

---

## 3. games/selva-oscura/ — lang + insight (inner-monologue backend)

Two narrative modules from the 3D game are candidate backends for Wayworn's
inner-monologue system. Neither touches 3D/physics/humanoid/terrain/combat
headers — the only coupling is to selva's game-state structs.

### `selva::lang` — tiered string map — **near-free lift**

- **Files:** `include/lang/Language.h`, `src/lang/Language.cpp`,
  `config/lang/*.json`.
- **Model:** one `unordered_map<string, Entry>`; each `Entry` has `tier_0`
  (required floor), `tier_1`, `tier_2`, plus `unlock_node_tier_1/2` (names of
  insight nodes). `resolve(key)` walks tiers high→low, returns the deepest tier
  whose unlock node has fired; falls back to `tier_0`. Missing key returns a
  loud `[lang:KEY]`.
- **Authoring:** `{ "some.key": { "tier_0": "...", "tier_1": "...",
  "unlock_node_tier_1": "knows_x" } }`.
- **Why it fits:** "text whose register deepens as named understanding-nodes
  fire" is *already* the inner-monologue-shifts-across-the-game mechanic from
  DESIGN.md. The tier ladder is the protagonist's arc of self-knowledge.
- **Lift cost — low.** Rename the namespace; replace the one predicate
  `isUnlocked()` (currently `selva::hasInsight(id)`) with Wayworn's "has this
  flag fired?" query. Drop or repoint the one hard-coded `mind.*.summary`
  convention. No singletons of its own beyond a function-local static map.

### `selva::insight` — event-driven flag graph — **moderate lift**

- **Files:** `include/insight/Insight.h`, `InsightLayout.h`; `src/insight/
  Insight.cpp`, `InsightLayout.cpp`; `config/insight/*.json`.
- **Model:** a flat `vector<Node>`, polled once per frame by `tick()`.
  **Observations** auto-fire when a trigger condition is met; **inferences**
  fire only when the player deduces them (selects the right observations).
  Five trigger kinds: `flag_set`, `dialog_began`, `examined`, `kill_count`,
  `sangue_accumulated`. Firing = appending the node id to the profile's
  unlocked set. **No inter-node propagation** — it's a set of independent gates
  polled against game state, plus a player-driven deduction layer.
- **Authoring:** `{ "knows_x": { "kind": "observation", "category": "world",
  "trigger": { "kind": "examined", "subject": "..." } } }`; inferences add
  `requires: [...]` + `readings: [...]` (readings are literally authored
  interior-monologue interpretations).
- **Why it fits:** it's the flag-graph that drives `lang` tier promotion. Its
  `growPerception`/`growCognition` side-effect counter is *exactly* the
  "protagonist gained self-knowledge" signal that should deepen the monologue
  register.
- **Lift cost — moderate.** Needs a **profile-state adapter**: the module reaches
  into selva's `PlayerProfile` via ~8 accessors (`activePlayerProfile`,
  `hasFlag/setFlag`, `hasInsight/setInsight`, `kill_counts`, `examine_counts`,
  `sangue_lifetime`). Define a small Wayworn profile struct with those fields and
  reimplement the accessors as a thin shim. **Drop `InsightLayout` entirely**
  (it's purely selva's Mind-page node-graph UI). Rename/drop the
  `sangue_accumulated` trigger. Assumes one active profile — fine for a
  single-protagonist game.

### Verdict for the monologue system

Together these already implement "contextual narrative text whose register
deepens as world events fire understanding-nodes." Lift both: `lang` is trivial,
`insight` needs a state-adapter and de-selva-fication. **Decision deferred** —
the alternative is a simpler bespoke flag+text map. The tier/node model is worth
it only if the register-deepening arc is authored densely; if monologues are
sparse (as DESIGN.md suggests), a lighter bespoke system may suffice. Flagged
for a design call before the monologue slice.

---

## 4. Repo-wide tooling / assets

### Fonts

- **`engines/arduboy-legacy/assets/fonts/Px437_IBM_VGA_8x14.ttf`** — the repo's
  only true **pixel font** (with license), in the archived tree. Strong
  candidate for Wayworn's UI. (Prison-escape ships only `cinzel.ttf`, a serif
  display face — wrong register.)
- Font loading is the shared engine `FontManager` (any `.ttf` → baked atlas).

### Palette tooling (reusable, but we're authoring from scratch)

- `games/prison-escape-game/scripts/bake_palettes.py` + `config/palettes/*.json`
  — a complete **color-ramp palette-swap** system for indexed pixel art
  (6-stop hex ramps), with a runtime twin in `SpriteCompositor::PaletteSwap`.
  Useful later for region/character recoloring; not needed to start.

### Tilemap authoring — **no Tiled pipeline exists**

There is **no `.tmx`/`.tsx`/Tiled integration anywhere.** Prison-escape authors
tiles as ASCII `.room` templates fed to a *procedural* dungeon carver. Wayworn's
hand-authored overworld needs an **authored-map format + loader** — the engine
`TileMap`/`TileMapRenderer`/`TileConfig` layer is reusable, but the loader is
net-new. This is a real decision (format choice) called out in SLICE.md.

### Testing

Catch2, fetched via CMake. Tests are per-game (`<game>/tests/`); no shared mock
library. Prison-escape's `tests/test_helpers.h` (`emplaceGameConfigs`) is the
only reusable fixture pattern (adapt it). New test files register in the game's
`CMakeLists.txt` `game-tests` target.

### Build / lint / CI

- **Repo-wide CI linters** (in `games/selva-oscura/scripts/`) will auto-cover
  Wayworn once it's added to their scope lists: `check_comment_density.py`
  (≤30% comments/file), `check_bandaid_keywords.py` (TODO/FIXME/HACK scan).
  Also clang-format, clang-tidy, cppcheck, lizard (CCN 15 / len 250 / 8 params).
  **Action item:** add `games/wayworn-hush` to the lizard + cppcheck scope lists
  when the CMake target lands.
- `changelog.py` is per-game (copies have diverged) — copy prison-escape's and
  its `test_changelog.py`.

### Config validation

No JSON-schema files anywhere; validation is code-side (nlohmann_json with
`.value(field, default)` fallthrough). Engine ships `ecs/ConfigLoaders.h` as the
shared loader base. Wayworn follows the same "config-driven, no content
constants in code" convention.

### Placeholder art (convenience only — sprites are being authored fresh)

`dungeon_tiles.png` (32px atlas), LPC 4-dir character sheets, `campfire.png` +
`ember.png` (relevant to camping). Useful to prove the pipeline before real art
exists; none of it ships.

---

## Cross-references

- [SCALE.md](SCALE.md) — the tile/sprite/render-target/camera number proposals.
- [SLICE.md](SLICE.md) — the vertical-slice build plan.
- [ENGINE.md](ENGINE.md) — per-game engineering notes (to be updated with these
  corrections).
