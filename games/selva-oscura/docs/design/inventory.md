# Inventory

> **Owns:** what the player carries — slots, item types, capacity
> limits, the path-specific carry rules.
> **Status:** structural locks; per-screen UX TBD.

## Items locked at the setting level

Per setting.md *Item system*:

- **The Grimoire** — hub-side, lore-text and unlock viewer. Entries
  appear to the player when triggered. Persistent across cycles.
  Always available.

- **The Signing** (*La Firma*) — **not an item; a Guide ritual
  driven by the class-picker UI at Beat 4.** The Guide frames the
  ritual as a formality of intake — *"None can pass into the depths
  unmeasured."* The class-picker UI then fires (modal full-screen,
  four options: Penitent / Heretic / Wretched / Refuse, two-step
  confirm, no back button). Picking a class = Signing happens = the
  Guide installs the chrism-fire that lets the Vagrant perform the
  Crucible (commit held substance inward into substrate). Refusing =
  no Signing = the Guide's posture opens the channel-fire that lets
  the Vagrant perform the Censer (route held substance outward to
  Beatrice). Same fire, opposite mouths.

  The Vagrant's absorption-capacity is permanent from arrival in
  Selva onward (per setting.md *The Vagrant's cosmological anomaly*).
  Beat 4 doesn't unlock absorption; it unlocks the COMMIT verb. The
  HUD counter accumulates pre-Beat-4 with nowhere to spend.

  The Guide believes the ritual is a formality of intake. He does
  not know (concealed pre-R2) that the Signing is also the act by
  which Beatrice's saturation claims the soul; that knowledge lives
  only in the player's retrospective reading after the R2 reveal.

  The Signing window is **one-shot at Beat 4** for the Guide alone.
  Cosmological cause: the Lupa-fall arrangement supplied the chrism-
  equivalent substance the ritual required; once spent, the Guide
  has nothing to draw on. Later Erasures re-enable the ritual via
  Vagrant-gathered material prerequisites.

- **The Crucible** — **a cosmological commit-verb, not an item.** The
  class-picker's ritual of installing held substance into substrate
  via chrism-fire. Player-invoked, on demand, anywhere. The Vagrant
  performs the Crucible; he doesn't carry it. Per the locked
  cosmology (per [[project_crucible_censer_leveling_system]]): no
  object materializes at Beat 4. What materializes is the capacity to
  perform the ritual. Stats / abilities / capability are the result
  of commits. Held substance pre-commit accumulates in the Vagrant
  himself (HUD counter); auto-magnetizes on kills via subtle pulse
  animation; optional QoL feast-on-body adds horror register and
  tradeoff (slower, more vulnerable, more visceral).

- **The Censer** — **a cosmological commit-verb, not an item.** The
  unburdened's ritual of routing held substance to Beatrice via
  channel-fire. Player-invoked, on demand, anywhere. Same
  performed-not-carried framing as the Crucible. The held substance
  burns into smoke that transits via the cosmological link Beatrice
  opened (at the moment of refusal) to her reservoir. Riversamento
  volume accumulates with each commitment. The Vagrant grows
  subtractively (Svuotato → Diaphanous) because his body is used as
  transit-substrate without nourishment.

- **Markers** — player-placed bidirectional teleport destinations
  replacing the old Cord. The Vagrant places a marker at a chosen
  location in any circle (one active marker per circle); marker
  becomes a Wood ↔ marker teleport node. Per-use sangue cost (curve
  TBD). Cosmological justification: the Vagrant cannot be marked,
  so he can mark — leaves a piece of his unjudged-exemption in the
  geography, creating a zone Hell cannot see. Available to both
  paths. See [[project_markers_replace_cord]] and brainstorming
  notes below.

- **The Erasure** — **not a portable item but a materials-gated
  ritual the Guide performs.** The Vagrant gathers cosmological
  prerequisites (items / sangue from specific sources / insight
  unlocks) through play; when conditions are met, the Guide senses
  the cosmological possibility return and offers the ritual.
  Erasure re-runs the class-picker UI (same modal as Beat 4); the
  Guide swaps the commit-fire (chrism-fire ↔ channel-fire) and prior
  commitments refund to the Vagrant's held substance (Beatrice-
  mediated), enabling path-switching in either direction. The Guide doesn't
  understand the mechanism — he just feels he can do it now.
  Specific Erasure prerequisites TBD.

- **The chest** — a storage object in or immediately adjacent to
  the chapel. **Cosmologically mundane** (no Hell-grip, no
  installation behavior, accessible to any soul including the
  unburdened) but **aesthetically loaded** (13th–15th century
  Italianate / gothic register; cathedral-reliquary or pilgrim's-
  alms-chest silhouette; same register as the chapel exterior —
  Beatrice's gothic infrastructure for the Vagrant). Both paths
  can put items into the chest and pull them out at any visit.
  Items in the chest persist across cycles — the chest is in the
  Wood, and the Wood remembers (per wood.md *The Wood remembers*).

  Pure storage. Both paths autosave through the same triggers
  (Crucible/Censer commitments, item commits, voluntary retreat,
  second death) — no special save-site role for the chest.

  Specifics — exact placement (inside the chapel antechamber vs.
  outside on the plateau), interior detailing, capacity, whether
  multiple chests exist — TBD.

## Path-specific items and capacities

Different paths carry different items and gain different cosmological
capacities. The Crucible and Censer are NOT inventory items -- they
are commit-verbs the Vagrant gains the capacity to perform at Beat 4
depending on which path he chose.

| Item / Capacity | Class-picker | Unburdened |
|---|---|---|
| Grimoire (carried) | yes | yes |
| Signing (Guide ritual, Beat 4) | accepted | refused |
| Crucible (commit-verb capacity) | yes | — |
| Censer (commit-verb capacity) | — | yes |
| Markers (carried) | yes | yes |
| Erasure (Guide ritual, materials-gated) | yes (any direction) | yes (any direction) |
| Chest (Wood-side storage) | yes | yes |

## Slots / structure

*TBD — must respect the engine's RAM ceiling (~2.5 KB total).
Inventory size is a design *and* footprint constraint.*

Items committed at setting-level (above) are **few and structurally
named**, not generic loot. The inventory is not a typical RPG bag;
it is a small named-slot system. Specifics:

- Number of slots, ordering, on-screen presentation: **TBD**
  (defer to UX implementation).
- Whether NPC rewards expand the inventory (and how many): **TBD**.
- Whether the inventory is a single screen or contextual: **TBD**.

## Item interactions

- **Signing + Crucible / Censer:** the player's choice at the Beat 4
  class-picker UI determines which vessel appears. Picking a class =
  Signing fires = Crucible appears. Refusing = no Signing =
  refusal-channel opens = Censer appears.
- **Erasure re-runs the class-picker UI.** Same modal as Beat 4
  (Penitent / Heretic / Wretched / Refuse). The new choice atomically
  re-marks the Vagrant; prior commitments refund to the new vessel
  (Beatrice-mediated), enabling path-switching in either direction.
  Materials-gated (Vagrant gathers prerequisites; Guide performs
  the ritual when conditions are met).
- **Markers:** available to both paths from acquisition onward.
  Player-placed teleport destinations; one active per circle;
  bidirectional Wood ↔ marker; sangue cost at placement and per
  use.

## NPC rewards

Per setting.md *NPCs*: each NPC provides a benefit on first encounter
(item, sangue, lore fragment, permanent unlock). **NPC roster, item
specifics, and per-NPC reward design TBD** (deferred to per-NPC
content design — see setting.md open questions).

## Limits

The inventory is small by design (named slots, not loot bag) and
small by constraint (RAM ceiling). Specifics emerge during
implementation.

## Open questions

- Slot count and on-screen layout.
- Whether NPC rewards expand the inventory or replace existing items.
- Inventory screen UX flow (how the player accesses it; whether it
  pauses combat; whether items are usable from a quick slot or a
  full menu).
- Crucible / Censer commitment UX — button press, hold-to-confirm,
  menu? Defer to UX.
- Marker placement and use cost curve — exact sangue cost per
  circle / keeper-status. Defer to balance tuning.
- Pre-Hand-was-retired healing mechanism — the Hand was the
  healing item; now retired. Healing-anywhere is a separate
  unsolved problem (TBD).

## Cross-references

- [Setting](setting.md) — *Item system*, *Markers*, *Vestigia*
  (the save register), *Sangue and the law of substance*.
- [Story](story.md) — *The Signing* (Beat 4 via class-picker UI).
- [Classes](classes.md) — Erasure switches class via re-running the
  class-picker UI.
- [Economy](economy.md) — sangue cost of marker placement and use,
  sangue installation via the Crucible.
- [UX](ux.md) — inventory screen, item-use UX, class-picker UI.

---

## Markers (2026-05-28 brainstorm; promoted to canon 2026-06-06)

> Status: locked 2026-06-06 per [[project_markers_replace_cord]].
> Replaces the old Cord mechanism entirely. Bidirectional Wood ↔
> marker. Available to both paths.

### What the bonfire problem looks like in Selva

Souls' bonfire bundles many functions (save / respawn / level-up /
equip / heal / repair / fast-travel / world-reset / tonal anchor).
Selva already distributes those across authored systems:

| Function | Selva system |
|---|---|
| Save | Vestigia (the cosmological save-register; symmetric across paths) |
| Respawn | The Wood (cosmologically, per fallback.md) |
| Stat-up / level-up | Crucible (class-picker) / Censer (unburdened) — portable from Beat 4 onward |
| Equip / loadout | Inventory screen (UX TBD, but it's a screen, not a place) |
| Heal | Pre-Hand-retired healing is a real TBD |
| Repair / craft | Imprint-making — the verb the Vagrant carries (per crafting.md brainstorming notes); no bench |
| Fast travel | Markers (bidirectional Wood ↔ marker, replaces the old Cord) |
| Tonal anchor | The Wood IS the tonal anchor; in-Hell rest is doctrinally absent |

The marker system handles fast travel bidirectionally. It is not a
bonfire; it is a placeable teleport node.

### The cosmological justification (the load-bearing piece)

**The Vagrant cannot be marked, so he can mark.** Per setting.md
*The unjudged*, Hell's killing-protocol cannot grip the Vagrant
because he carries no imprint to complete. The unjudged-exemption
is his defining cosmological property. He carries it into Hell.

He can leave a piece of it in a place: spend sangue + intent,
imprint the exemption into the geography. The spot becomes a
small zone Hell's accounting cannot touch. **Because Hell cannot
touch it, the Vagrant's body can re-arrive at it without being
processed.** The marker is a teleport destination because it is
a place Hell does not see.

This composes with crafting.md's brainstorming notes — markers
ARE imprints, same verb (imprint-making) producing a different
kind of output. Substance flows the same way: sangue + intent →
marked thing.

### What the marker is, mechanically

- **Player-placed.** The Vagrant chooses where, at the cost of a
  sangue spend (paid at placement).
- **One active marker per circle.** The exemption is finite; he
  can hold it at one location per region. Removable (he reclaims
  the exemption) and re-placeable elsewhere.
- **Bidirectional Wood ↔ marker teleport.** The Vagrant initiates
  from either end, pays sangue (depth-scaled curve TBD), arrives
  at the other end. The Wood remains the hub — markers do NOT
  enable marker → marker direct teleport.
- **No save effect.** Vestigia handle save; markers don't change
  that. Respawn on death is still the Wood, not the last marker.
  No save-scum loop possible.
- **No healing on arrival from the marker itself.** Healing is a
  separate TBD (the old Hand mechanic is retired). The marker is
  not a place to rest.
- **Optional gentle softening:** the *transit itself* partially
  restores HP — Hell briefly loses sight of the Vagrant mid-jump,
  he reconstitutes at the destination. Avoids "teleport mid-fight
  to low-HP arrival" feeling cheap without making the marker a
  rest site.

### Path implications

- **Both paths have markers.** The unjudged-exemption that powers
  the marking is present in both class-picker and unburdened
  Vagrants (the Signing creates the imprint that the imprint-fire
  uses for installation, but the unjudged-exemption — what makes
  the Vagrant cosmologically unmeasurable — is upstream of the
  Signing and survives refusal). Symmetric path support.

### Verb-name candidates (Italian register, TBD)

The marker needs a name in the world's vocabulary. Working
candidates:

- *Vestigio* (singular trace, footprint). Composes with the
  *vestigia* save system but at the geographic-marker layer
  instead of the run-state layer. Cautious: risks ambiguity with
  save vestigia.
- *Segno* (sign, mark). Direct, period-Italian, light theological
  weight (cf. *signum* in Catholic Latin).
- *Impronta* (imprint, fingerprint). Most directly tied to the
  imprint-making frame from crafting.md notes — the verb and the
  noun share a root.
- *Sigillo minore* (lesser seal). An old name from prior drafts; the
  big-S Seal item is gone (the class-pick ritual is now the Guide's
  Signing). Could survive here for markers if it doesn't read as
  carrying the old meaning.

Lean: *Impronta* if we lock the imprint-making verb. *Segno* if
we want neutrality.

### Where this doesn't go

- **Not save spots.** Vestigia stay locked.
- **Not rest sites.** No healing-at-marker, no enemy-reset-on-rest.
- **Not in the Wood.** The Wood already accepts player marks via
  the deferred *Place / mark* verbs in wood.md (cairns / etched
  names / fires). Those are *memorial*, not *transit*. Different
  system entirely.
- **Not workbenches.** Crafting is bench-less per the imprint-making
  framing.
- **Not Souls-bonfires.** Single function: teleport.

### TBD before promotion to canon

- Sangue cost curve for placement vs. transit (two separate spends
  or one combined?).
- Whether the marker is visible to the player as a geometric
  object in the world, or as an entry in an inventory/menu.
  Probably both: a small placed object the Vagrant can see at the
  spot, plus a Wood-side menu of "places I have marked."
- Whether the transit itself is fade-to-black (matches the colle's
  Acheron-crossing register) or seamless (per the seamless-traversal
  pillar). Probably fade — markers are explicitly fast travel, which
  is the canonical fade-exception per the seamless-traversal
  doctrine.
- Whether Beatrice's threshold-light marks the geography in the
  Wood that corresponds to the player's active markers (so the
  Wood-side menu has a *spatial* presentation, not a list). Beautiful
  if so; UX cost TBD.
