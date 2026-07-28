# Wayworn Hush -- Systems Index

> **Purpose:** one-line-per-system map of what exists and where to read for intent. Read this BEFORE designing a new system (per `.claude/CLAUDE.md` audit-discipline rule). If a relevant entry exists, read the header it points to before proposing anything.
>
> **Maintenance:** append a line every time a real system ships. Cheap; pays for itself the next time anyone audits for adjacent infrastructure. The cost of staleness is "the same system gets designed twice."
>
> **Sort order:** roughly bottom-up (config/util → world/map → gameplay → UI). Each line: `**name**` — what it is — `path/to/header.h`.

## Config + utility

- **config::load** — the one permissive JSON-config loader (open / parse-exceptions-off / discard-check); every config loader routes through it — [include/JsonConfig.h](../include/JsonConfig.h)
- **formulas** — stat-driven value formulas (coefficients in `config/formulas.json`, math in code). First customer: the observation glow's brightness as a function of a faculty. The shared home for "a stat changes what you experience." — [include/Formulas.h](../include/Formulas.h)
- **reading_color** — the shared 1..5 rarity/faculty color vocabulary (readings and items use the same ramp) — [include/ReadingColor.h](../include/ReadingColor.h)
- **worldclock** — in-world time source (notebook datelines; day/night later) — [include/WorldClock.h](../include/WorldClock.h)
- **unlock::Condition** — the shared gate primitive (observed / flag / stat). Reused by observation tiers, deeds, thoughts, and item/visibility gates (crafting does NOT gate — see crafting) — [include/UnlockCondition.h](../include/UnlockCondition.h)

## World + map

- **ldtk import** — LDtk region → tiles / props / encounter placements / world-item placements; authoring at native scale, doubled to the world grid — [include/LdtkImport.h](../include/LdtkImport.h)
- **world_config** — per-region asset paths (map, atlas, ambient) — [include/WorldConfig.h](../include/WorldConfig.h)
- **surfaces** — per-surface walkability (terrain collision derived from tile tags, no hand-painted layer) — [include/Surfaces.h](../include/Surfaces.h)
- **structures** — resizable walk-on structures (9-slice deck + per-slice walkability + per-cell surface) — [include/Structures.h](../include/Structures.h)
- **world_init** — player spawn + region setup helpers — [include/WorldInit.h](../include/WorldInit.h)

## Gameplay

- **player_movement / player_config** — movement integration + authored player tuning — [include/PlayerMovement.h](../include/PlayerMovement.h)
- **footsteps** — per-surface footstep audio, speed-scaled cadence — [include/Footsteps.h](../include/Footsteps.h)
- **growth** — the self: faculties + secondary stats + Spirit EXP; stat-level queries; starting levels — [include/Growth.h](../include/Growth.h)
- **psyche** — the one content/cognition engine. An **Encounter** is a glowing spot you can OBSERVE (reading tiers) or ACT ON (deeds) -- it must offer BOTH to load, though the player may lean entirely on one. Plus thoughts (written) and remarks (spoken), four line kinds by initiator/voice (observation = chosen, impression = pressed, remark = speech, thought = concluded), the ambient re-check engine, per-spot taken-state, `{player}` name binding; deeds may grant items / consume their spot (drops) — [include/Psyche.h](../include/Psyche.h)
- **npc** — a person on the map: config (WHO: art, name, walk feel) + LDtk placement (WHERE) + speaker-flagged psyche content (WHAT); spawn/steer helpers scenes reuse — [include/Npc.h](../include/Npc.h)
- **scene** — scripted choreography that PLAYS the graph: body verbs (enter/move/face/leave), box verbs (observe/remark/menu with blocking/must_choose pacing), world verbs (wait/fade_in/set_flag/sound); gated by start_when, once-guarded + sequenced by its required set_flag — [include/Scene.h](../include/Scene.h)
- **ambience** — named world-sound channels with a flag bus: stop_on_flag / start_on_flag (once per walk, armed against restored flags so a resume never replays the past), per-channel fades — [include/Ambience.h](../include/Ambience.h)
- **interaction** — the generic verb layer: resolve the active target + fire; routes to observe / act / take without owning their logic — [include/Interaction.h](../include/Interaction.h)
- **inventory** — the satchel: two-layer item model (blueprint / instance), categories, ops (add/remove/count/has), key-item gating, new-find flag — [include/Inventory.h](../include/Inventory.h)
- **loot** — gather loot tables + a pure weighted roll (the lottery) — [include/Loot.h](../include/Loot.h)
- **crafting** — Little-Alchemy making: ingredients are the only requirement (no unlock gate); match by type-set + a closeness near-miss signal; stat+roll outcome quality; inverse-mastery XP; first-make RECORDS a recipe (learning is book-keeping, not a prerequisite); deeds can teach recipes early via `grant_recipe` — [include/Crafting.h](../include/Crafting.h) / [design/CRAFTING.md](design/CRAFTING.md)
- **world_items** — items lying in the world: floor sprites + actionable-only interactables (pickups / gather nodes), with a rim-outline cue — [include/WorldItems.h](../include/WorldItems.h)
- **notebook** — the thought collection: the thoughts he has reached, as dated pages; stores only the world-clock moment each landed, always (writing gated on carrying the notebook; the watch buys reading the HOUR off a note, not the moment being recorded) — [include/Notebook.h](../include/Notebook.h)

## UI + feedback

- **thought_box** — the HUD reading/menu surface: typed readings + the deed menu — [include/ThoughtBox.h](../include/ThoughtBox.h)
- **glimmer** — the warm observation glow (brightness from the faculty formula; active-target only) — [include/Glimmer.h](../include/Glimmer.h)
- **interaction_mode** — the movement-stance cue: an Observe/Act badge + a transition sound (see [design/INTERACTION-MODEL.md](design/INTERACTION-MODEL.md)) — [include/InteractionMode.h](../include/InteractionMode.h)
- **watch_hud** — the carried pocket watch's corner readout; drawn only while the watch is in the satchel, and holds with the world's clock while paused — [include/WatchHud.h](../include/WatchHud.h)
- **head_marker** — the over-head thought bubble that tracks the player — [include/HeadMarker.h](../include/HeadMarker.h)
- **notify** — ambient self-fading toasts (finds, unlocks, EXP) — [include/Notify.h](../include/Notify.h)
- **tutorial** — first-time teaching cards: the world stops on the fully-shown moment, dims except the lit region, one card once per pilgrim (seen-set saved); events fire off what is ON SCREEN, one per line kind + the surfaces — [include/Tutorial.h](../include/Tutorial.h)
- **settings** — how the player likes the game: HUD mode + per-piece toggles. The installation's, not any pilgrim's -- it sits beside the roster in the save — [include/Settings.h](../include/Settings.h)
- **settings_screen** — the one settings surface, opened from the title AND the pause System tab — [include/SettingsScreen.h](../include/SettingsScreen.h)
- **pause_page** — the on-demand screen (Self / Satchel / Craft / Notebook / System tabs); Satchel, Craft + Notebook share a list+detail layout with mouse & keyboard nav — [include/PausePage.h](../include/PausePage.h)
- **hud::canvas / regions** — fixed HUD region rects + scale — [include/HudCanvas.h](../include/HudCanvas.h)
- **screen_to_world** — cursor → world transform (undoes the pixel-target blit + camera) — [include/ScreenToWorld.h](../include/ScreenToWorld.h)
- **tune_panel** — the dev tuning/inspection panel — [include/TunePanel.h](../include/TunePanel.h)
