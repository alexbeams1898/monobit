# Boss backend — design proposal

> **Status:** design pre-execution; awaiting review. WIP. Drafted
> 2026-05-31 as the data + lifecycle layer that supports Lupa (the
> Beat 2 opening boss per [story.md](../story.md)) and every future
> boss. Scope: BACKEND only — presentation layer (boss-GUI, music
> triggers, "ENEMY FELLED" overlays) is deferred to its own session.
>
> **Why this exists:** Lupa is the first boss in the game. Per
> [[selva-wood-lore-locked-2026-05-31]] she is `permanent_on_death`,
> tragic-register, single-encounter. Shipping her without boss
> infrastructure means retrofitting bosses later when the next one
> (a keeper, a guardian) arrives. Better to build the data scaffolding
> once, configurable, and ship Lupa AS the first complete boss.

## Goals + constraints

**Must:**
- Boss-ness is a data flag on the archetype (one bool). Doesn't
  pollute the Actor struct unless required.
- Bosses are TRIGGER-spawned, not boot-spawned. The encounter starts
  when the player crosses a defined threshold, not when the world
  loads.
- Boss death is save-persistent: a felled boss stays felled in
  every subsequent cycle of the same save. The empty slope IS the
  monument.
- The system is data-configurable from JSON. Adding a new boss is
  authoring `config/enemies/<id>.json` + a trigger declaration +
  optional encounter-data, not C++ changes.
- The GUI layer (HP bar, name display, music triggers, lockout) is
  not built here, but the data it WILL read is — so GUI work is
  pure-presentation when it lands, no data model changes.

**Won't (deferred):**
- **In-encounter behavior shifts** (Souls-style HP phases or anything
  similar). Per [ai.md](../ai.md): Selva's in-boss-change system is
  TBD, will be designed lore-first per-boss. Lupa is single-encounter
  / single-behavior for v1. No phase data shape, no transition
  triggers, no behavior-tree swap-on-trigger infra. When designed,
  builds on top of what's here.

**Will (basic forms, expanded scope 2026-05-31):**
- **Boss-GUI (minimal):** on-screen name display + HP bar visible
  during the encounter. Fade in on encounter start, fade out on death.
  No "ENEMY FELLED" overlay yet beyond the basic post-death freeze
  below.
- **Music:** boss bed-swap on encounter start, return to ambient on
  death. Engine's `selva::audio` already supports bed switching
  (per the audio.json structure); just wires the trigger hooks.
- **Arena lockout:** the player cannot leave the boss arena during
  the encounter. Implemented as a per-encounter zone (AABB) inside
  which the player is contained — leaving-attempts get a soft
  push-back / invisible-wall. Behavior cleared on boss death.
- **Lock-on for non-humanoid bosses:** extend `SkeletonJointMap` with
  a `lockon_chest` field. Replace hardcoded `mixamorig:Spine2` in
  [`ui/ActorHud.cpp`](../../src/ui/ActorHud.cpp) with a per-skeleton
  lookup. Wolf gets `"Torso2"` (or whichever joint in the rig is the
  visual chest center). Player rig keeps `mixamorig:Spine2`.
- **Boss reward shape:** data-flow for "boss death emits an event
  the reward system consumes." Item drop / sangue grant / etc. wired
  through the existing inventory + sangue paths. Specific reward
  CONTENT for Lupa is designer-authored separately (canon says she
  drops no sangue since she's not a demon; what she DOES drop is
  TBD by Alex).
- **"Boss felled" overlay:** brief screen freeze on death (~1-2s) +
  centered text fade ("LUPA FELLED" or game-equivalent in
  tragic register) + return to gameplay. Lightweight. No black-out,
  no music sting.

## Data model

### 1. `EnemyArchetype` additions

Two new fields on `EnemyArchetype` (in
[`include/gameplay/EnemyArchetype.h`](../../include/gameplay/EnemyArchetype.h)),
both with empty / false defaults so existing archetypes
(`limbo_shade`) inherit "not a boss":

```cpp
struct EnemyArchetype {
    // ... existing fields ...

    // Boss flag. When true, this archetype follows the boss lifecycle:
    // trigger-spawned (not boot-spawned), save-persistent death,
    // surfaces in the future boss-GUI. Single source of truth for
    // "is this thing a boss" across systems.
    bool is_boss = false;

    // Display name when the boss-GUI eventually surfaces. Italian
    // for legends per the Italian-as-legend doctrine
    // (see [[selva-epistemic-doctrine-2026-05-31]]). E.g. "LUPA".
    // Empty for non-bosses. The GUI reads this verbatim; no
    // translation layer.
    std::string boss_name;
};
```

Wolf archetype gets `is_boss: true`, `boss_name: "LUPA"`. Limbo shade
leaves both at defaults.

### 2. `EnemySpawnDecl` additions (region.json schema)

Bosses don't spawn at boot. They spawn when their assigned trigger
fires. Add an optional field to the existing spawn-decl:

```cpp
struct EnemySpawnDecl {
    // ... existing fields ...

    // If set, this enemy does NOT spawn at region boot. Instead, the
    // enemy spawns when the trigger with this id fires (per the
    // region.json triggers[] array). Used for bosses (Lupa spawns
    // when the player approaches the colle slope, not when surface
    // region loads at world boot).
    //
    // Empty = boot-spawn (current behavior; shades, ambient enemies).
    std::string spawn_trigger_id;
};
```

JSON shape in `region.json`:
```json
"enemy_spawns": [
  {
    "id": "lupa",
    "archetype": "wolf",
    "pos": [0, "auto_terrain", -150],
    "yaw": 3.14,
    "permanent_on_death": true,
    "spawn_trigger_id": "lupa_arena_enter"
  }
]
```

### 3. `RegionTrigger` extension

The existing `RegionTrigger` in
[`engines/engine/include/world/Region.h`](../../../../../engines/engine/include/world/Region.h)
is hardcoded to fire region transitions. Extend it with an action
type so a single trigger can either transition regions OR fire a
non-transition action (spawn a boss, etc.):

```cpp
enum class TriggerAction : std::uint8_t {
    RegionTransition,  // current behavior; uses target/target_spawn_pos
    SpawnEntity,       // new; uses spawn_entity_id
};

struct RegionTrigger {
    // ... existing fields (id, center, half_extents, etc.) ...

    TriggerAction action = TriggerAction::RegionTransition;

    // SpawnEntity-only: which enemy_spawns[] entry this trigger
    // activates. Matches EnemySpawnDecl.id within the SAME region.
    // Ignored for RegionTransition.
    std::string spawn_entity_id;
};
```

JSON shape:
```json
"triggers": [
  {
    "id": "lupa_arena_enter",
    "center": [0, 28, -135],
    "half_extents": [10, 5, 15],
    "action": "SpawnEntity",
    "spawn_entity_id": "lupa",
    "debug_name": "lupa_slope_approach"
  }
]
```

Edge-triggered (already the existing trigger semantics): fires once
when player enters the AABB. After firing, the trigger is consumed
for the rest of the cycle (no re-fire if player leaves and re-enters
during the same cycle).

### 4. `Actor` — minimal additions

One bool. Driven from archetype:

```cpp
struct Actor {
    // ... existing fields ...

    // Mirrors archetype->is_boss. Cached on Actor for hot-path lookups
    // (the GUI / lockout systems poll the actor pool every frame; doing
    // an archetype indirection per-actor per-frame is wasteful).
    bool is_boss = false;
};
```

Set in `spawnEnemyFromDecl` from the archetype. No new behavior on
Actor; just a hot-path data flag.

### 5. `PlayerProfile` — felled bosses persistence

Per [AppState.h](../../include/AppState.h) line 120-124, PlayerProfile
already mentions `keepers_felled` as a planned field. Bosses are the
same shape (the keeper-felled and boss-felled lists could even be the
same list if we accept that "keeper" is a subset of "boss"). Adding:

```cpp
struct PlayerProfile {
    // ... existing fields ...

    // Bosses defeated in this save. Persists across cycles. When a
    // boss-spawn trigger fires, the spawn check FIRST consults this
    // set — if the boss's id is already here, no spawn. The empty
    // slope IS the monument per [[selva-wood-lore-locked-2026-05-31]].
    std::vector<std::string> felled_bosses;
};
```

When a boss actor dies (`actor.is_boss && actor.is_dead`):
1. Add `spawn_decl.id` to `active_profile.felled_bosses`
2. Mark save as dirty (so it persists on next save flush)

On region load:
1. Iterate `enemy_spawns[]` with non-empty `spawn_trigger_id`
2. For each, check if `id` is in `active_profile.felled_bosses`
3. If yes, REMOVE that spawn-decl from active set — the trigger fires
   but the entity is never spawned. (Or skip-and-log; same effect.)

### 6. Boss-encounter active state

The "is a boss encounter happening right now" flag is the central
hub the GUI, music, and arena-lockout all read from. Single source
of truth; everything else derives.

Lives in `selva::GameState` (per existing pattern in
[`AppStateGlobal.h`](../../include/AppStateGlobal.h)):

```cpp
struct GameState {
    // ... existing fields ...

    // Active boss encounter — nullptr if none. Set when a boss-
    // spawn trigger fires (and the boss actor is created); cleared
    // when the boss actor is dead (post-felled-overlay sequence).
    // Caches the actor pointer (pool index would dangle if the pool
    // resizes; we resolve to ptr on the same frame we set).
    selva::gameplay::Actor* active_boss = nullptr;

    // The boss's spawn-decl id (e.g. "lupa"). Stable across frames
    // even if the actor pointer becomes stale. Used by GUI for
    // name lookup, by save system for felled-list append.
    std::string active_boss_id;
};
```

Accessor `selva::gameplay::activeBoss()` returns
`gameState().active_boss`. All consumers read through it.

### 7. Arena lockout

A boss encounter constrains the player to a defined zone (no escape
during the fight). Built as a per-boss "encounter zone" — an AABB
the player is contained inside while the boss is alive.

**Data:** new optional field on `EnemySpawnDecl`:

```cpp
struct EnemySpawnDecl {
    // ... existing fields ...

    // World-space AABB the player is constrained inside while this
    // boss is alive. Only consulted if archetype.is_boss && this
    // spawn is active (i.e. trigger fired, boss alive). Empty
    // half_extents (zero vec) = no lockout, fight is free-roaming.
    // Per-encounter, not per-region: different bosses in same
    // region get different lockout zones.
    glm::vec3 arena_center{0.0f};
    glm::vec3 arena_half_extents{0.0f};  // (0,0,0) = no lockout
};
```

JSON shape:
```json
{
  "id": "lupa",
  "spawn_trigger_id": "lupa_arena_enter",
  "arena_center": [0, 30, -150],
  "arena_half_extents": [25, 10, 30]
}
```

**Mechanism:** per-frame in `tickPlayerMovement` (post-AI, post-
input, pre-physics-step), if `activeBoss() != nullptr` and the
boss's spawn-decl has nonzero `arena_half_extents`:
1. Compute player's intended next position
2. Clamp to the arena AABB (soft push-back: player can press
   against the boundary but doesn't cross it)
3. Restore intended velocity if clamping happened (don't kill momentum,
   just prevent crossing)

Cleared automatically when `active_boss` becomes nullptr (on death).

### 8. Boss-GUI hook (data only; visuals later)

The GUI is its own implementation work but it reads from already-
defined state. Required data surfaces:

- `activeBoss()` returns the actor — GUI reads `actor.hp.current`
  and `actor.hp.max` for the bar
- `gameState().active_boss_id` resolves to archetype via
  `archetypes().get(active_boss_id)` — GUI reads `archetype->boss_name`
  for the display
- Encounter-start "fade in" + encounter-end "fade out" are GUI-side
  state; the GUI tracks its own fade timer driven by the
  active_boss-becoming-nonnull / becoming-null transitions

No new C++ fields beyond what's already in sections 1, 4, 6 above.
GUI is consumer-only.

**For this scope, the GUI implementation IS in scope** — a minimal
ImGui-based overlay:
- Boss name (large, centered horizontal, near bottom of screen)
- HP bar (centered horizontal beneath the name)
- Fade in over ~0.5s when `active_boss` transitions nullptr → non-null
- Fade out over ~1.0s when `active_boss` transitions non-null → nullptr

Authored in [`ui/BossHud.cpp`](../../src/ui/BossHud.cpp) (new file),
wired into `gatedRenderImGui` in [`main.cpp`](../../src/main.cpp)
alongside the existing ImGui hooks.

### 9. Music hook

Bosses optionally swap the audio bed on encounter start. New field
on `EnemyArchetype`:

```cpp
struct EnemyArchetype {
    // ... existing fields ...

    // Audio bed name (key in audio.json music section) to swap to
    // when this boss's encounter starts. On encounter end (boss
    // dies, post-felled-overlay), reverts to the previous bed
    // (typically the region's `audio_bed` from region.json).
    // Empty = no swap; boss uses ambient.
    std::string encounter_audio_bed;
};
```

Lupa example: `encounter_audio_bed: "lupa_encounter"` (placeholder
key; Alex authors the music file + audio.json entry separately).

**Mechanism:** in `selva::audio` (existing module), a push/pop bed
stack. When boss encounter starts, push the current bed and swap to
encounter_audio_bed. When encounter ends, pop. Implementation: ~20
lines in audio.cpp.

### 10. Lock-on for non-humanoid bosses

Extends `SkeletonJointMap` (per
[animals_and_multi_skeleton.md](../animals_and_multi_skeleton.md))
with a chest-joint role for lock-on reticle anchoring:

```cpp
struct SkeletonJointMap {
    // ... existing fields ...

    // Visual chest center — the joint the lock-on reticle anchors
    // to. Humanoid: "mixamorig:Spine2". Wolf: "Torso2" (or whichever
    // joint sits at the visual chest of the rig). Empty = no lock-on
    // support for this skeleton (the reticle simply doesn't draw,
    // doesn't crash).
    std::string lockon_chest;
};
```

JSON addition to `config/skeletons/<id>.json`:
```json
{
  "skeleton_id": "wolf",
  "joint_names": {
    "hips": "Body",
    "lockon_chest": "Torso2",
    ...
  }
}
```

Replace hardcoded `mixamorig:Spine2` in
[`ui/ActorHud.cpp:312`](../../src/ui/ActorHud.cpp) with:
```cpp
const auto& jmap = selva::anim::jointMapByKey(target.skeleton_id);
if (jmap.lockon_chest.empty()) return;  // skeleton doesn't support lock-on
const int chest_idx = target.sampler.findJoint(jmap.lockon_chest.c_str());
```

### 11. Boss-felled overlay

Brief screen-freeze + text on boss death. Authored in same
`ui/BossHud.cpp` as the boss-name display. Mechanism:

- When `active_boss` transitions non-null → nullptr (boss death
  confirmed), enter a short "felled-display" state in BossHud
- ~0.5s freeze + center-screen text "LUPA FELLED" (or whatever the
  per-boss "felled message" template is)
- Tragic register: no music sting, no black-out, no celebration
  fanfare. Brief, mournful, ephemeral.
- Text fades over ~2s, gameplay resumes

**Optional per-boss "felled message"** — field on archetype:
```cpp
struct EnemyArchetype {
    // ... existing fields ...

    // Text shown briefly on death in the boss-felled overlay.
    // Default (empty): just "{boss_name} FELLED" generic. Per-boss
    // override lets each boss have a tonally-appropriate line.
    // Tragic register for Lupa.
    std::string felled_message;
};
```

### 12. Boss reward event (data shape only; content TBD)

When a boss dies, emit an event the reward systems consume.
Simplest implementation: a hook function the boss-death-cleanup
calls, dispatching by boss id. No new struct.

```cpp
// In Enemies.cpp per-frame death cleanup, when a boss actor dies:
selva::gameplay::onBossFelled(active_boss_id, killing_actor);

// onBossFelled in BossRewards.cpp (new):
void onBossFelled(const std::string& boss_id, Actor& killer) {
    // For Lupa: no sangue (she's not a demon), but possibly an
    // item / offering / Grimoire-unlock. Authored per-boss in
    // this dispatch function; TBD content for Lupa.
    if (boss_id == "lupa") {
        // TBD: what does Lupa drop? Alex to decide.
    }
}
```

Once enough bosses ship, this likely becomes data-driven (rewards
declared in archetype JSON). For v1, code-dispatch is fine — the
"how rewards work mechanically" question (item drop vs Grimoire
unlock vs world state flip) hasn't been answered yet, so coding it
generically would be premature.

## Lifecycle (end-to-end for Lupa)

**Encounter-shape note:** Lupa is NOT trigger-spawned in the
"appears-out-of-nowhere when player crosses a line" sense. She is
**already there at game-start, sitting outside the chapel**, visible
to the player as soon as the chapel comes into view. The "trigger"
that activates her combat is a proximity-engage, not a spawn.

This means the lifecycle below is slightly different from the
generic boss case described in the data model. For future bosses
that DO spawn-out-of-nowhere (e.g. a keeper appearing in a circle
arena), the standard trigger-spawn path applies. Lupa specifically
uses a "spawn at boot, but in sitting/idle state, then engage on
proximity" variant.

1. **World boot.** Surface region loads. `spawnAllRegionEnemies`
   runs. Lupa's `EnemySpawnDecl` does NOT have an
   `spawn_trigger_id` — she boot-spawns. BUT her archetype's
   `initial_state` is set to `"sitting"` (new field; see below) —
   she spawns in the sitting idle clip, NOT in combat-ready state.
   AI does not tick attacks; she just sits.

2. **Save-check on world boot.** Same as generic case — if Lupa is
   in `profile.felled_bosses`, her spawn-decl is skipped. The
   chapel-front is empty.

3. **Player approaches the chapel.** A boss-engage trigger
   (`lupa_engage`) sits at a proximity AABB around the chapel
   exterior. When the player crosses into it, the trigger fires:
   - Sets `gameState().active_boss = &lupa_actor`
   - Sets `gameState().active_boss_id = "lupa"`
   - Switches Lupa's `idle_clip` from `idle_2_head_low` (sitting)
     to `idle` (combat-standing). Via the rise animation
     (`jump_to_idle`) — her ONE animated transition from
     not-fighting to fighting.
   - Music bed swaps to `encounter_audio_bed`
   - Boss-GUI begins fade-in
   - Arena lockout activates

4. **Lupa rises.** The animation plays once (~1s). During this beat
   the player has a small window to attack — designer choice
   whether the rise is invulnerable or punishable.

5. **Player fights Lupa.** Standard combat. Hitboxes, hurtboxes,
   stamina, etc. No phase changes. No mid-fight scripted moments.
   Single-phase, single-behavior, but mechanically dangerous
   (two-tap deadly per the locked combat-tuning paradox).

6. **Lupa dies.** `actor.is_dead = true`. Per-frame death-cleanup
   sees `actor.is_boss = true && is_dead = true`:
   - Appends `active_boss_id` to `profile.felled_bosses`
   - Marks save dirty
   - Triggers boss-felled overlay (~0.5s freeze + tragic-register
     text fade)
   - Clears `gameState().active_boss = nullptr`
   - Music bed pops back to ambient
   - Arena lockout clears
   - Boss-GUI fades out
   - Boss-reward dispatch fires (`onBossFelled("lupa", killer)`)

7. **Player triggers save flush** (autosave or quit-to-menu).
   `felled_bosses` persists to disk.

8. **Next cycle / new run.** Surface loads. Boot-time save-check
   sees Lupa in `felled_bosses`. Her spawn-decl is skipped
   permanently. The chapel-front is empty. Monument framing intact.

### New data field for the "sitting → standing → fighting" beat

`EnemyArchetype.initial_state: std::string` — default empty
(= normal combat-ready spawn). Lupa: `"sitting"`. When the engage
trigger fires, the actor transitions to combat-ready state via the
archetype's `engage_clip` (also new):

```cpp
struct EnemyArchetype {
    // ... existing fields ...

    // Spawn-time state (empty = combat-ready immediately). Lupa
    // uses "sitting" — she boot-spawns into the sitting idle clip
    // and doesn't tick combat AI until engage-trigger fires.
    std::string initial_state;

    // Clip to play once when engage-trigger transitions actor from
    // initial_state to combat-ready. Lupa: "jump_to_idle" (the
    // "rise to attention" animation). After this clip ends, normal
    // combat AI begins.
    std::string engage_clip;
};
```

The boss-engage trigger (`lupa_engage`) uses the same trigger
infrastructure as section 3 above, but with a NEW action type:
`EngageBoss` — finds the boss actor by `spawn_entity_id`, switches
its state from `initial_state` to combat-ready, plays `engage_clip`
once, then unlocks combat AI ticks.

```cpp
enum class TriggerAction : std::uint8_t {
    RegionTransition,  // existing
    SpawnEntity,       // section 3 — for trigger-spawned bosses
    EngageBoss,        // NEW — for already-spawned bosses (Lupa)
};
```

## Configuration surface (designer authoring a new boss)

Two patterns supported. Most bosses use Pattern A (trigger-spawned);
Lupa specifically uses Pattern B (already-there boss with engage
proximity trigger).

### Pattern A — generic trigger-spawned boss

For bosses that appear when the player crosses a threshold (most
keepers, arena bosses):

1. **Archetype JSON** at `config/enemies/<boss_id>.json`:
   - `id`, `skeleton_id`, `actions`, `hurtbox_decls` (existing)
   - `is_boss: true`
   - `boss_name: "<DISPLAY NAME>"`
   - `encounter_audio_bed: "<bed_id>"` (optional; music swap)
   - `felled_message: "<text>"` (optional; per-boss death overlay text)

2. **Spawn entry in region.json `enemy_spawns[]`**:
   - `id`, `archetype`, `pos`, `yaw`, `permanent_on_death: true`
   - `spawn_trigger_id: "<trigger_id>"`
   - `arena_center`, `arena_half_extents` (optional; lockout AABB)

3. **Trigger entry in region.json `triggers[]`**:
   - `id` matches the spawn_trigger_id
   - `center`, `half_extents`
   - `action: "SpawnEntity"`
   - `spawn_entity_id: "<spawn_id>"`

### Pattern B — already-there boss with proximity engage (Lupa)

For bosses that are visible before combat — sitting, sleeping,
waiting at a fixed pose:

1. **Archetype JSON**:
   - All Pattern A fields PLUS:
   - `initial_state: "sitting"` (matches a defined idle clip)
   - `engage_clip: "jump_to_idle"` (transition animation)

2. **Spawn entry in region.json `enemy_spawns[]`**:
   - All Pattern A fields EXCEPT `spawn_trigger_id` (left empty —
     boss boot-spawns)
   - `engage_trigger_id: "<trigger_id>"` (NEW field)
   - `arena_center`, `arena_half_extents` (lockout AABB)

3. **Trigger entry in region.json `triggers[]`**:
   - `id` matches the `engage_trigger_id`
   - `center`, `half_extents` (proximity zone — close enough to
     engage)
   - `action: "EngageBoss"` (NEW action type)
   - `spawn_entity_id: "<spawn_id>"` (finds the already-spawned
     boss actor)

### Skeleton config (any boss with non-humanoid skeleton)

Add `lockon_chest` to `config/skeletons/<skeleton_id>.json`:
```json
"joint_names": {
  "hips": "Body",
  "lockon_chest": "Torso2",
  ...
}
```

### Reward (per-boss; for now code-dispatch, future data-driven)

In `BossRewards.cpp::onBossFelled`, add a case for the new boss_id.
Trivial; will become JSON-driven once enough bosses ship.

### Audio (if encounter_audio_bed is set)

Add the bed entry to `config/audio.json` `music` section. Same
pattern as existing ambient beds.

Zero C++ changes for adding a generic boss using either pattern.
Boss ships from JSON.

## Tests required

**Unit (Catch2, pure-compute / no GL):**
- `EnemyArchetype` JSON round-trip — all new fields (`is_boss`,
  `boss_name`, `encounter_audio_bed`, `felled_message`,
  `initial_state`, `engage_clip`)
- `EnemySpawnDecl` JSON round-trip — all new fields
  (`spawn_trigger_id`, `engage_trigger_id`, `arena_center`,
  `arena_half_extents`)
- `RegionTrigger` JSON round-trip — `action`, `spawn_entity_id`,
  the three action types (`RegionTransition`, `SpawnEntity`,
  `EngageBoss`)
- `SkeletonJointMap` JSON round-trip — new `lockon_chest` field
- `PlayerProfile` save round-trip with `felled_bosses` populated
- Arena AABB containment math — player pushed back at boundary,
  velocity preserved
- Felled-bosses check at boot — boss in list = decl skipped

**Integration / smoke (manual in-game):**
- Walk to chapel exterior — Lupa visible, sitting
- Close approach — engage trigger fires, music swap, boss-GUI
  fades in, Lupa stands via engage_clip, combat AI begins
- Try to leave arena during fight — soft push-back at boundary
- Lock-on Lupa — reticle anchors to chest joint (Torso2)
- Kill Lupa — boss-felled overlay plays, music returns to ambient,
  GUI fades out, reward dispatch fires
- Reload save — chapel-front empty, no Lupa, no music swap
- New cycle from same save — chapel-front empty
  manual smoke test if Catch2 fixture is too heavy)
- **Smoke test:** Lupa renders on the slope when player crosses
  trigger, dies when killed, doesn't respawn in next cycle (manual
  in-game test)

## Open questions / WIP

1. **Trigger consumption on save reload.** If a boss spawn trigger
   fired in cycle 1, was the boss killed mid-fight, and the player
   reloads the save before death — does the trigger re-fire on next
   slope-approach? Or did the kill-mid-fight count?
   - Probably: trigger does re-fire (it wasn't consumed AS a kill).
   - Felled state only updates on confirmed `actor.is_dead`.

2. **Multi-region boss spawn.** What if a boss should spawn in a
   different region than where its trigger lives? Out of scope for
   v1 (Lupa is single-region). When this lands: extend trigger to
   carry `target_region_id` + `spawn_entity_id` (already half-exists
   via the existing region-transition trigger).

3. **Should `keepers_felled` and `felled_bosses` be the same list?**
   Per setting.md, keepers ARE bosses. Probably yes — one list,
   one save slot. The difference is bestiary classification, not
   data shape. Deferred until the first keeper ships.

4. **Trigger-as-quest-marker / fire-once-globally.** Bosses are
   inherently fire-once-per-save. The current `RegionTrigger` is
   fire-once-per-cycle. Save-checking before firing handles the
   per-save semantics WITHOUT changing the trigger system. Going
   with this for v1.

5. **Player loadout / class state when entering boss encounter.**
   The boss should respect existing combat state. No special "boss
   strips your buffs" behavior. (Future bosses can author their own
   encounter-entry effects if needed.)

6. **Engage trigger re-fire on save reload.** Mirror of question 1 —
   if player engages Lupa, dies mid-fight, reloads: does the engage
   trigger re-fire on next approach? Probably yes (same logic — the
   engage isn't consumed AS a kill). Lupa returns to sitting state
   on reload? Or stays in combat-ready state? Probably returns to
   sitting since `initial_state` is the spawn-time state — natural
   resetting. Confirm during smoke test.

7. **Rise animation invulnerability.** During `engage_clip` playback
   (~1s), is Lupa invulnerable? Punishable? Designer choice — flag
   `rise_invulnerable: bool` on the archetype. Default: punishable
   (Souls-typical pattern, rewards player who engages aggressively).
   Tunable per-boss without code.

8. **What happens if the player walks AWAY from Lupa during the
   engage trigger but before combat begins?** Probably the trigger
   has already fired — Lupa is rising — and disengaging requires
   killing her (arena lockout activates simultaneously). I.e. the
   engage is the point of no return. Confirm.

9. **Audio bed crossfade duration.** Currently `selva::audio`
   bed-swap may be instant. Boss encounter start probably wants a
   short crossfade (~1s) so the music transition isn't jarring.
   Implementation detail; defer to when wiring step 10.

10. **Boss-GUI text register.** For the felled overlay — is it
    "LUPA FELLED" (Souls-pattern caps) or something more
    register-appropriate? Lupa's tragic register might suggest
    lowercase / Italian / something simpler ("lupa è caduta" —
    she has fallen). Designer copy call. Field on archetype
    (`felled_message`) lets each boss have its own.

11. **Boss-felled overlay freeze depth.** Does the overlay freeze
    ALL gameplay (input, AI, physics) or just dim the screen + show
    text while gameplay continues? Souls-pattern is the freeze.
    Tragic register for Lupa suggests soft pause + text fade
    without a hard input-lock. WIP.

## Implementation order (suggested)

Ordered so each step is independently testable; the system reaches
a runnable state at step 5, with subsequent steps adding the
basic-form features one at a time.

1. **Plan review** (this doc) — get sign-off before code.
2. **EnemyArchetype + EnemySpawnDecl fields + JSON parsing + tests.**
   All new archetype fields (`is_boss`, `boss_name`,
   `encounter_audio_bed`, `felled_message`, `initial_state`,
   `engage_clip`) and spawn-decl fields (`spawn_trigger_id`,
   `engage_trigger_id`, `arena_center`, `arena_half_extents`).
   No behavior change yet; data flows through.
3. **RegionTrigger extension + JSON parsing + tests.** Three
   `TriggerAction` types (RegionTransition existing, SpawnEntity,
   EngageBoss); `spawn_entity_id` field. Trigger dispatcher in
   engine routes by action type.
4. **PlayerProfile.felled_bosses + save round-trip test.** Plus
   the boot-time skip-felled-bosses logic.
5. **Spawn-on-trigger + engage-on-trigger lifecycle in Enemies.cpp
   + JsonRegion.cpp.** Pattern A (SpawnEntity) and Pattern B
   (boot-spawn + EngageBoss) both wired. `GameState.active_boss`
   + `active_boss_id` populated on engage. Death cleanup writes
   to felled_bosses. **System is now runnable end-to-end** —
   Lupa would render, engage, fight, die, persist-dead if all
   subsequent JSON were authored.
6. **SkeletonJointMap.lockon_chest + ActorHud.cpp refactor.**
   Replaces hardcoded `mixamorig:Spine2`. Test: humanoid still
   locks on correctly; wolf locks on to `Torso2`.
7. **Arena lockout — player-containment in tickPlayerMovement.**
   Reads from active_boss's spawn-decl's arena AABB. Soft push-
   back at boundary.
8. **Boss-GUI (BossHud.cpp) — minimal HP bar + name display.**
   ImGui overlay wired into `gatedRenderImGui`. Fades in/out on
   active_boss transitions.
9. **Boss-felled overlay** (in BossHud.cpp). Brief freeze + text
   fade on `active_boss` non-null → null transition. Uses
   `felled_message` from archetype (falls back to generic).
10. **Music hook in `selva::audio`** — push/pop bed stack.
    Encounter-start pushes archetype.encounter_audio_bed; death
    pops back to previous bed.
11. **BossRewards.cpp — onBossFelled dispatch.** Empty
    implementation for v1 (TBD what Lupa drops); stub the
    function + call site so the wiring is in place.
12. **Author Lupa as the first boss:**
    - Update `config/enemies/wolf.json`: `is_boss: true`,
      `boss_name: "LUPA"`, `initial_state: "sitting"`,
      `engage_clip: "jump_to_idle"`, `encounter_audio_bed: ""` (TBD
      audio), `felled_message: ""` (TBD copy)
    - Add Lupa spawn entry to `surface/region.json` `enemy_spawns[]`
      — `engage_trigger_id: "lupa_engage"`, arena AABB around chapel
    - Add `lupa_engage` trigger to `surface/region.json` `triggers[]`
      — proximity AABB near chapel exterior, `action: "EngageBoss"`
    - Update `config/skeletons/wolf.json` with `lockon_chest: "Torso2"`
13. **Smoke test** in-game (manual):
    - Lupa visible sitting at chapel from approach
    - Engage triggers (music swap, GUI fade in, rise animation, AI ticks)
    - Combat works, hurtboxes register, two-tap damage scaling
    - Lock-on reticle anchors to wolf chest
    - Arena lockout prevents escape
    - Kill confirms (overlay, music revert, GUI fade out)
    - Reload save → empty chapel-front, no Lupa
    - New cycle → same
14. **Doc updates this commit:**
    - `assets/regions/SCHEMA.md` — all new fields documented
    - `bestiary.md`, `wood.md`, `story.md` — pointer note that the
      boss backend data layer is wired (status: GUI minimal,
      content/copy WIP)
    - Update this design doc's status from "design pre-execution"
      to "implemented; refinements TBD"
    - Memory: append a "boss backend shipped" note to
      [[selva-wood-lore-locked-2026-05-31]]

## Cross-references

- [[selva-wood-lore-locked-2026-05-31]] — Lupa as Beat 2 boss,
  permanent-on-death, tragic register
- [[selva-epistemic-doctrine-2026-05-31]] — Italian-as-legend for
  `boss_name`
- [story.md](../story.md) Beat 2 — encounter framing
- [bestiary.md](../bestiary.md) — boss classification (legends /
  keepers / Lucifer as three boss-types eventually)
- [animals_and_multi_skeleton.md](../animals_and_multi_skeleton.md)
  — engine architecture for non-humanoid bosses
- [setting.md](../setting.md) — keepers as canon bosses, same data
  shape as Lupa
