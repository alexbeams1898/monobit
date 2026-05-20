# prison-escape-game — Game Architecture

Per-game architectural notes: helper trees, dialog templates, registry
singletons, content-specific conventions.

For engine-wide doctrine (engine/game boundary, collision architecture, flow
field, animation, render interp, depth sort, coordinate convention,
attack-token/slot system, helper-placement decision tree), see
[engines/engine/docs/ENGINE.md](../../../engines/engine/docs/ENGINE.md).
That file is the canonical engine reference; this one extends it with
prison-escape-game-specific structure.

---

## Game-side helpers (in `games/prison-escape-game/include/ops/` + `games/prison-escape-game/src/ops/`)

- `InventoryOps` (`ops/InventoryOps.h`) — inventory add/remove,
  equip/unequip, item counting, material consumption, evolution execution
  (canEvolve, evolveWeapon).
- `SpawnUtils` (`ops/SpawnUtils.h`) — enemy spawn logic.
- `CraftingOps` (`ops/CraftingOps.h`) — crafting recipe execution.

---

## Reusable dialog utilities (in `games/prison-escape-game/include/screens/`)

Two auto-sizing dialog templates — measure content first, compute panel
dimensions from measurements. Never hardcode panel sizes.

- **ConfirmDialog** (`screens/ConfirmDialog.h`) — centered Yes/No
  confirmation popup. `Options`: title, body_lines (vector), title_font,
  body_font, selection pointer, min_width. Returns `Result::Yes`,
  `Result::No`, or `Result::None`. Handles keyboard (Y/N, Left/Right,
  Enter, Escape) and mouse.
- **MenuDialog** (`screens/MenuDialog.h`) — centered option-list dialog
  with labels + descriptions. `Options`: title, items (vector of
  `{label, description, enabled}`), hint text, title_font, body_font,
  selection pointer, close_on_escape, close_on_rmb, darken_background.
  Returns `Result{selected, dismissed}`. Handles keyboard (Up/Down,
  Enter, Escape) and mouse (hover to select, click to activate, RMB to
  dismiss).

**Pattern:** caller builds an `Options` struct, calls `render()`, reads the
result. The dialog measures all text content and sizes the panel to fit.
Used by SanctuaryScreen (MenuDialog) and LoadGameScreen (ConfirmDialog).

**When building new dialogs:** Always use these templates or follow the
same measure-first pattern. Never hardcode pixel dimensions for dialog
panels.

**Footer button standard:** All footer buttons use bottom-up layout
anchored to the panel edge:

- 20px clearance from panel bottom to button bottom edge
- Button internal padding: 30px horizontal (each side), 10px vertical (each side)
- `btn_h = text.height + 20.0f`, `btn_w = text.width + 60.0f`
- `btn_y = panel_bottom - 20.0f - btn_h`
- Text drawn at `(btn_x + 30, btn_y + 10)`
- Never position footer buttons top-down from hardcoded offsets (e.g.
  `panel_h - 60`).

---

## GameConfig.h split (in `games/prison-escape-game/include/ecs/`)

`GameConfig.h` is an umbrella header. New code should include only what it
needs:

- `BalanceConfig.h` — FormulaConfig, SoundConfig, MusicConfig, WaveConfig,
  WaveState
- `ItemConfig.h` — ItemDef, ItemRegistry, RecipeRegistry, WeaponTierRegistry,
  EvolutionRegistry, Compendium, Rarity, qualityName
- `AppState.h` — UIState, GameState, RunStats, ScoringConfig, SaveData

---

## Registry / config singletons (in `entt::registry::ctx()`)

- `FormulaConfig` — all balance constants (formulas.json)
- `SoundConfig`, `MusicConfig` — audio mappings
- `WaveConfig`, `WaveState` — wave rules and runtime state
- `ItemRegistry` — all item definitions keyed by config_path
- `RecipeRegistry` — crafting recipes
- `WeaponTierRegistry` — per-class growth defaults (dagger, sword, club)
- `EvolutionRegistry` — weapon evolution trees + reverse lookup (weapon →
  family + node)
- `Compendium` — discovered items (persistent across runs)
- `UIState` — active screen/tab tracking
- `GameState` — top-level app state (MainMenu, Playing, GameOver, etc.)
- `RunStats` — accumulated run statistics
- `ScoringConfig` — score formula weights
- `SaveData` — persistent save data (characters, runs)

---

## Evolution tree file conventions

- One file per weapon class: `config/evolution/blades.json`,
  `bludgeons.json`, `ranged.json`
- Named by weapon class, not by root weapon
- Cross-tree evolution via optional `"target_tree"` field (not yet
  implemented)
- ConfigLoader scans the directory — adding a tree = adding a JSON file

---

## Per-game tunings referenced in engine doctrine

The engine doctrine in `engines/engine/docs/ENGINE.md` mentions some patterns
where the *pattern* is generic but the *tuning* is per-game. For
prison-escape-game, those tunings are:

- **Movement inset:** `MOVEMENT_INSET = 2.0f` calibrated for 32×32 entities
  on a 32px tile grid.
- **Flow field:** 128×128 cells at 16px/cell. Corridors 5 tiles wide (160px
  for 32px entities).
- **Steering wall repulsion:** `REPULSION_STRENGTH = 0.5`.
- **Attack tokens:** default 2 (configured via `combat_ai.max_attack_tokens`
  in `config/balance/formulas.json`, loaded into `FormulaConfig.combat_ai`).
- **AI orbit speed:** `AIController.orbit_speed` default 0.5, skeleton 1.0.
- **Y-sort layers:** wall=0, campfire/rest_spot=1, all characters (player
  parts + enemies)=2.
- **Animation state priority:** `Dead > DamageFeedback(Hit) >
  AttackLocked(Attack) > velocity!=0(Walk) > Idle`. Owned by
  `AnimStateSystem`.
- **WASD-locked facing:** the player sprite faces WASD direction or its
  exact opposite (backpedal); aim direction tracks mouse / lock-on.
  `BACKPEDAL_DOT = -0.15` for backpedal hysteresis. Resolved per-frame in
  `gamePerFrame()` rather than in the fixed-step loop.
