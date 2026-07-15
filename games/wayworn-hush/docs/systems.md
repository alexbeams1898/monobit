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
- **unlock::Condition** — the shared gate primitive (observed / flag / stat). Reused by observation tiers, deeds, thoughts, item/visibility gates, and (planned) crafting recipes — [include/UnlockCondition.h](../include/UnlockCondition.h)

## World + map

- **ldtk import** — LDtk region → tiles / props / observable placements / world-item placements; authoring at native scale, doubled to the world grid — [include/LdtkImport.h](../include/LdtkImport.h)
- **world_config** — per-region asset paths (map, atlas, ambient) — [include/WorldConfig.h](../include/WorldConfig.h)
- **surfaces** — per-surface walkability (terrain collision derived from tile tags, no hand-painted layer) — [include/Surfaces.h](../include/Surfaces.h)
- **structures** — resizable walk-on structures (9-slice deck + per-slice walkability + per-cell surface) — [include/Structures.h](../include/Structures.h)
- **world_init** — player spawn + region setup helpers — [include/WorldInit.h](../include/WorldInit.h)

## Gameplay

- **player_movement / player_config** — movement integration + authored player tuning — [include/PlayerMovement.h](../include/PlayerMovement.h)
- **footsteps** — per-surface footstep audio, speed-scaled cadence — [include/Footsteps.h](../include/Footsteps.h)
- **growth** — the self: faculties + secondary stats + Spirit EXP; stat-level queries; starting levels — [include/Growth.h](../include/Growth.h)
- **observations** — the cognition engine: objective tiers + subjective thoughts + deeds; the observe verb; the ambient re-check engine; per-spot taken-state; deeds may grant items / consume their spot (drops) — [include/Observations.h](../include/Observations.h)
- **interaction** — the generic verb layer: resolve the active target + fire; routes to observe / act / take without owning their logic — [include/Interaction.h](../include/Interaction.h)
- **inventory** — the satchel: two-layer item model (blueprint / instance), categories, ops (add/remove/count/has), key-item gating, new-find flag — [include/Inventory.h](../include/Inventory.h)
- **loot** — gather loot tables + a pure weighted roll (the lottery) — [include/Loot.h](../include/Loot.h)
- **world_items** — items lying in the world: floor sprites + actionable-only interactables (pickups / gather nodes), with a rim-outline cue — [include/WorldItems.h](../include/WorldItems.h)
- **notebook** — the dated record of readings (gated on carrying the notebook; time gated on the watch) — [include/Notebook.h](../include/Notebook.h)

## UI + feedback

- **thought_box** — the HUD reading/menu surface: typed readings + the deed menu — [include/ThoughtBox.h](../include/ThoughtBox.h)
- **glimmer** — the warm observation glow (brightness from the faculty formula; active-target only) — [include/Glimmer.h](../include/Glimmer.h)
- **interaction_mode** — the movement-stance cue: an Observe/Act badge + a transition sound (see [design/INTERACTION-MODEL.md](design/INTERACTION-MODEL.md)) — [include/InteractionMode.h](../include/InteractionMode.h)
- **head_marker** — the over-head thought bubble that tracks the player — [include/HeadMarker.h](../include/HeadMarker.h)
- **notify** — ambient self-fading toasts (finds, unlocks, EXP) — [include/Notify.h](../include/Notify.h)
- **pause_page** — the on-demand screen (Self / Noticed / Satchel / Notebook / System tabs) — [include/PausePage.h](../include/PausePage.h)
- **hud::canvas / regions** — fixed HUD region rects + scale — [include/HudCanvas.h](../include/HudCanvas.h)
- **screen_to_world** — cursor → world transform (undoes the pixel-target blit + camera) — [include/ScreenToWorld.h](../include/ScreenToWorld.h)
- **tune_panel** — the dev tuning/inspection panel — [include/TunePanel.h](../include/TunePanel.h)
