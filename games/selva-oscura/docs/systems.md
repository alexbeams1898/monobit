# Selva Oscura -- Systems Index

> **Purpose:** one-line-per-system map of what exists, who owns what, and where to read for intent. Read this BEFORE designing a new system (per `.claude/CLAUDE.md` audit-discipline rule). If a relevant entry exists, read the header it points to before proposing anything.
>
> **Maintenance:** append a new line every time a real system ships. Cheap. Pays for itself the next time anyone audits for adjacent infrastructure. The cost of staleness is "the same system gets designed twice" -- which is what this file exists to prevent.
>
> **Sort order:** roughly bottom-up (engine layer → world → gameplay → UI). Within a layer, alphabetical. Each line: `**name**` — what it is — `path/to/header.h`.

## Application + persistence

- **AppState / GameState / UIState** — top-level mode (MainMenu / CharCreate / Playing / etc.), pause menu state, character-create form state — [include/AppState.h](../include/AppState.h)
- **AppStateGlobal singletons** — `gameState()`, `saveData()`, `uiState()`, `activePlayerProfile()`, **flag helpers** (`hasFlag` / `setFlag` / `clearFlag`) — [include/AppStateGlobal.h](../include/AppStateGlobal.h)
- **PlayerProfile** — per-character persistent state: pos+yaw, current_region_id, **felled_bosses**, **flags** (generic quest-state), **door_states** — defined in AppState.h
- **SaveManager** — JSON save/load, schema-versioned, addCharacter / deleteCharacter — [include/SaveManager.h](../include/SaveManager.h)
- **Tunables** — hot-reloadable balance numbers (walk_speed, accel, lockon yaw rates, AI tick hz, …) — [include/Tunables.h](../include/Tunables.h)
- **debug::Flags** — SESSION-ONLY diagnostic toggles (ai_perception overlay, footstep_log, physics_log, enemy_lifecycle, …). Kept distinct from Tunables: ships separate, never serialized, F1 panel renders under a distinct "Debug" section — [include/debug/Flags.h](../include/debug/Flags.h)
- **Formulas** — HP / poise / stamina formula registry — [include/Formulas.h](../include/Formulas.h)
- **WallClock** — single `wallClock()` time source used by everything (animations, cooldowns, scripted events) — [include/WallClock.h](../include/WallClock.h)
- **Scene (cinematic)** — time-bounded moment that locks input categories (combat / movement / look) while something plays out — `begin({combat:true, movement:true, look:false})` / `end()` / `active()` — used by wake-scene at game start, designed for the Guide-stepping-out moment too — [include/Scene.h](../include/Scene.h)

## Animation

- **PoseSampler** — per-actor ozz sampling, loco crossfade, one-shot stack (freeze_last, cancel_fraction), hip-XZ extraction, BodyMask, IK hooks — [include/anim/PoseSampler.h](../include/anim/PoseSampler.h)
- **AnimationClip** — wrapper around `ozz::animation::Animation` loaded from `.ozz` — [include/anim/AnimationClip.h](../include/anim/AnimationClip.h)
- **ClipRegistry / SkeletalAssets** — per-skeleton clip registry (`clipsByKey("wolf")`, `clipsByKey("player")`), skeleton registry, joint maps — [include/anim/SkeletalAssets.h](../include/anim/SkeletalAssets.h)
- **SkeletonJointMap** — per-skeleton named joint refs (hips, foot_left, lockon_points list) authored in `config/skeletons/<id>.json` — [include/anim/SkeletonJointMap.h](../include/anim/SkeletonJointMap.h)
- **LocomotionConfig** — per-clip metadata: `translation_source` (velocity / root_motion / in_place), blend_in_seconds — `config/locomotion.json` — [include/anim/LocomotionConfig.h](../include/anim/LocomotionConfig.h)
  - **Hip-classify rule (authoritative):** only `root_motion` clips have their hip-XZ extracted and applied as world translation. `velocity` and `in_place` clips never do. Unregistered clip keys (empty `registry_key`) default to `in_place` -- visible non-motion is the diagnostic. The OLD 1.5m hip-path threshold heuristic that auto-extracted any clip whose hip travelled "far enough" was removed; key+JSON registration is now the single source of truth (was bandaiding the bug class memory `[[feedback_pose_sampler_key_propagation]]` documents).

## Combat

- **HitVolumes / hitboxes** — capsule hitboxes parented to a joint, lifetime + sweep + `attacker_faction` — [include/combat/HitVolumes.h](../include/combat/HitVolumes.h)
- **ActorVolumes / hurtboxes** — per-actor capsule hurtboxes built each frame from `body.hurtbox_decls` — [include/combat/ActorVolumes.h](../include/combat/ActorVolumes.h)
- **HitDetection** — per-frame hitbox×hurtbox sweep, faction filter via `factionsHostile`, memo to prevent double-damage per swing — [include/combat/HitDetection.h](../include/combat/HitDetection.h)
- **WeaponClass / AttackChain / TransitionProfile** — player attack chain definitions, per-attack windup/active/recovery, splice transitions — [include/combat/WeaponClass.h](../include/combat/WeaponClass.h)

## World / region

- **Region (engine)** — abstract region with onActivate / onDeactivate / triggers — [engines/engine/include/world/Region.h](../../../engines/engine/include/world/Region.h)
- **JsonRegion** — data-driven region loaded from `assets/regions/<id>/region.json`: static_meshes, terrain_modifiers, triggers, enemy_spawns, doors, ai_block_volumes — [include/world/JsonRegion.h](../include/world/JsonRegion.h)
- **PhysicsRegion** — Jolt integration, kinematic player capsule, static colliders (terrain + architecture + doors) — [include/world/PhysicsRegion.h](../include/world/PhysicsRegion.h)
- **Door** — ER-style door world-object: state machine (Locked / Closed / Opening / Open), per-door `openDoor` / `unlockDoor`, profile-persistent state — [include/world/Door.h](../include/world/Door.h)
- **Terrain** — global heightmap + modifiers (FlushAt / FlushSlope / Hole), per-region `terrain_region` carves — [include/world/Terrain.h](../include/world/Terrain.h)
- **StaticMeshAssets** — `.glb` loader, per-primitive trimesh collision, surface tags (Architecture / Foliage / etc.) — [include/world/StaticMeshAssets.h](../include/world/StaticMeshAssets.h)
- **RegionBootstrap** — registers all regions at boot, owns region instances — [include/world/RegionBootstrap.h](../include/world/RegionBootstrap.h)

## Gameplay

- **Actor (unified PC/NPC model)** — pos, yaw, sampler, faction, is_boss, is_npc, boss_state, perception, hp/poise/stamina, action_state, spawn_decl_id — [include/gameplay/Actor.h](../include/gameplay/Actor.h)
- **Faction** — `Faction` enum (Player / Hostile / Neutral / Allied) + `factionsHostile()` damage rule + **`Form` enum** (UnjudgedSoul / DamnedSoul / Animal / HellMachinery / Divine — cosmological category, drives stat-spread defaults and the Wood combat-permission doctrine) — [include/gameplay/Faction.h](../include/gameplay/Faction.h)
- **applyFormDefaults** — per-form Body/Stats baselines applied at spawn BEFORE archetype overrides. Sit at [src/gameplay/Actor.cpp](../src/gameplay/Actor.cpp). Tuned numbers in code; archetype `max_hp_override` etc. layer on top.
- **Enemies / spawn pipeline** — `spawnEnemyFromDecl`, `tickEnemies` (per-frame tick), `applyEnemyHitReact`, `fireEnemyDeath`, `tickPendingAttackSpawns` (windup deferred-spawn). Helper `actorByDeclId(id)` is the canonical actor-by-id lookup; centralizes the dozen interactable/scripted-event/HUD callsites that need it — [include/gameplay/Enemies.h](../include/gameplay/Enemies.h)
- **FlowSpawner** — declarative spawn flows from `config/spawn_flows/*.json`: initial populations (per-slot positions/yaws/archetypes/on_arrival_actions), trickle spawns up to an `active_cap`, scripted paths via `scripted_target_pos` + `waypoints`, slot-mode (`first_vacant_slot`) for queue/feeder layouts, on_arrival actions (`halt`, `convert_to:<archetype>`, `despawn`). Each spawn stamps `actor.spawning_flow_id` so `tickArrivals` matches by flow rather than archetype id (two flows sharing an archetype don't collide) — [include/spawn/FlowSpawner.h](../include/spawn/FlowSpawner.h)
- **Interaction** — Press-E system: each registered `Decl` has a `Kind`, `position`-closure, `range_meters`, `on_interact` callback, optional `available` gate. Per-frame closest-target pick. Archetype-driven Talk (NPC) and Examine (mob with `examine_text`) interactables auto-register on spawn; range from `archetype.interact_range_meters` — [include/interact/Interaction.h](../include/interact/Interaction.h)
- **ItemRegistry / Inventory** — items declared in `config/items/*.json`, categories in `config/items/categories/`, runtime inventory per-character — [include/items/ItemRegistry.h](../include/items/ItemRegistry.h)
- **EnemyArchetype** — JSON-loaded archetype: actions, faction, is_npc, skeleton_id, clip overrides per family (idle / walk / death / flinch / hit_react), hurtbox_decls, lockon_points, boss fields (is_boss, boss_name, initial_state, engage_clip, initial_freeze_at_seconds, max_hp_override), spawn_clip + spawn_clip_freeze_at_seconds, tint_color + tint_burn_target_archetype, examine_text + interact_range_meters, avoids_hazards, disable_hurtboxes — [include/gameplay/EnemyArchetype.h](../include/gameplay/EnemyArchetype.h)
  - **Inheritance:** archetypes may set `inherits: "<parent_id>"` to reuse a parent's fields. Merge semantics: child object keys override parent, child arrays REPLACE parent arrays wholesale (`merge_patch`). Used by `larva_aged_feeder` to inherit from `larva_aged`. Depth-limited to 8 to catch cycles.
- **applyArchetypeToActor** — THE diamond-foundation funnel: same function called by `spawnEnemyFromDecl` AND `applyArchetypeSwap`. Sets every archetype-derived field (faction, form, pools, hurtboxes, interactable, is_boss, is_npc) in ONE site so the spawn and mid-life-swap paths can't drift. `applyArchetypeSwap` additionally rebuilds the Jolt character body when collider dims change (cross-form swap). See [`Enemies.cpp`](../src/gameplay/Enemies.cpp) `applyArchetypeToActor` for the contract.
- **BehaviorTree (humanoid_basic)** — Selector(combat / alerted / idle), LeafPickAction (weighted-random action), LeafMoveToTarget, LeafCircleTarget (duel-strafe mirror), LeafIdle — [include/gameplay/BehaviorTree.h](../include/gameplay/BehaviorTree.h)
- **Perception** — vision cone, awareness ladder (Unaware / Suspicious / Alerted / Combat), leash-based decay. **Faction-gated**: only Hostile actors progress the ladder — [include/gameplay/Perception.h](../include/gameplay/Perception.h)
- **AiBarriers** — per-region AABB volumes that block AI movement, owner-region-based — [include/gameplay/AiBarriers.h](../include/gameplay/AiBarriers.h)
- **HazardZones** — generic per-kind AABB hazards (Acheron sangue-flow, etc.). Archetypes opt in via `avoids_hazards: ["acheron", ...]`; locomotion velocity is clamped at the boundary and BT chase target-position is gated by `positionIsInAvoidedZone`. Registered by region JSON parsers. Data-driven, no per-zone-name branches — [include/hazard/HazardZones.h](../include/hazard/HazardZones.h)
- **AiTick** — phase-staggered AI decision scheduler so a wave of co-spawned actors don't sync — [include/gameplay/AiTick.h](../include/gameplay/AiTick.h)
- **BossState** — SINGLE source of truth for boss lifecycle: enum (Dormant / Engaged / Disengaged / Felled), `setBossState()` funnel writes mirrors (current_boss_state, gameState.active_boss_*), `tearDownActiveBosses()` for phase-exit symmetry — [include/gameplay/BossState.h](../include/gameplay/BossState.h)
- **BossDispatcher** — Custom-trigger routing for boss-engage payloads (`engage:lupa` etc.), handles Pattern A/B spawn + engage — [include/gameplay/BossDispatcher.h](../include/gameplay/BossDispatcher.h)
- **BossRewards** — on-felled reward dispatch (XP / drops / unlocks) — [include/gameplay/BossRewards.h](../include/gameplay/BossRewards.h)
- **PerFrameTick** — gameplay's per-frame entry: input handling, player loco, lockon, combat input, region tick, scripted-event hooks — [include/gameplay/PerFrameTick.h](../include/gameplay/PerFrameTick.h)
- **PlayerState** — yaw helpers, yawFromGroundDir, wrapAngleSigned — [include/gameplay/PlayerState.h](../include/gameplay/PlayerState.h)
- **LocomotionStateMachine** — legacy SM (mostly replaced by velocity-driven path); see header for current role — [include/gameplay/LocomotionStateMachine.h](../include/gameplay/LocomotionStateMachine.h)
- **Footsteps** — foot-plant detection + per-step SFX dispatch — [include/gameplay/Footsteps.h](../include/gameplay/Footsteps.h)

## UI

- **Screens** — main menu / char create / load / settings / pause menu (System tab covers save+quit / quit to desktop). setPhase() chokepoint — [include/ui/Screens.h](../include/ui/Screens.h)
- **ActorHud** — in-world overlays: lock-on reticle, second-death card, second-death overlay rendering — [include/ui/ActorHud.h](../include/ui/ActorHud.h)
- **BossHud** — boss HP bar + name + felled overlay. State machine: Hidden → FadingIn → Active → Felled → FadingOut. Reads `boss_state == Engaged` from pool, `resetBossHud()` on phase-exit — [include/ui/BossHud.h](../include/ui/BossHud.h)
- **TuningPanel** — F1 tuning panel — live edit of Tunables + Formulas, save to disk — [include/ui/TuningPanel.h](../include/ui/TuningPanel.h)

## Audio

- **Audio (selva::audio)** — miniaudio integration, named SFX banks, `playSfx` / `scheduleSfx`, music bed push/pop stack, peak-aligned death audio composition — [include/audio/Audio.h](../include/audio/Audio.h)

## Render (high-level entry points only)

- **WorldRenderer** — orchestrates all render passes (shadow → sky → terrain → static meshes → actors → trees → light sprites → UI overlays) — [include/render/WorldRenderer.h](../include/render/WorldRenderer.h)
- **Camera** — third-person + FPV modes, lockon-yaw integration, follow-distance/height/pitch from Tunables — [include/render/Camera.h](../include/render/Camera.h)
- **ShadowPass** — cascaded shadow map; the indoor/outdoor signal (no scene_kind flags per [[real-lighting-doctrine]]) — [include/render/ShadowPass.h](../include/render/ShadowPass.h)
- **SkeletalRenderer** — per-actor skinned mesh draw, bone-palette uniform, per-skeleton mesh registry — [include/anim/SkeletalRenderer.h](../include/anim/SkeletalRenderer.h)
