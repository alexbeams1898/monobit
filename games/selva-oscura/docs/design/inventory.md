# Inventory

> **Owns:** what the player carries — slots, item types, capacity
> limits, the path-specific carry rules.
> **Status:** structural locks; per-screen UX TBD.

## Items locked at the setting level

Per setting.md *Item system*:

- **The Grimoire** — hub-side, lore-text and unlock viewer. *Hell's
  voice* — the artifact of Hell's passive self-awareness; the player
  reads it because the player is Hell-as-observer (per setting.md
  *The Grimoire is Hell's voice*). The Vagrant does not read it; he
  carries / it sits hub-side, and entries appear to the player when
  triggered. Persistent across cycles. Always available.

- **The Seal** (*Il Sigillo*) — given at the beasts (story.md *Beat
  3*). **Single-use.** Class-pick mechanism: using opens the imprint
  Hell can grip, locks the Vagrant out of the unburdened path
  permanently. *Carriable indefinitely if not used.* Carrying it
  keeps the Vagrant imprint-free; the unburdened can use it at any
  later moment to permanently break PURITY for that save.

- **The Cord** — mortal artifact lifted from *Inferno* XVI:106-108.
  Teleport from any circle back to the *selva oscura*. Unlimited
  uses; each use costs sangue (charged at the moment of use, not
  pre-charged). Cost depends on circle and keeper-status: pre-keeper
  = higher (Hell resists the Cord), post-keeper = lower (Hell's
  containment in this circle has been spent). **Not available to
  unburdened.**

- **The Erasure** — late-game. **Single-use within its scope.**
  Class-pickers can switch among Penitent / Heretic / Wretched.
  Cannot grant unburdened status (cannot un-measure).

- **The Hand** — acquired at the climax (Guide's death — kill or
  handover). Per story.md *The Hand*: heal (universal), plus
  path-specific functions (class-picker: offerings access at any
  time; unburdened: portable riversamento site). **Permanent across
  cycles.** Once acquired, the Vagrant has it for the rest of the
  save.

## Path-specific carry rules

Different paths have different available inventories.

| Item | Class-picker | Unburdened |
|---|---|---|
| Grimoire | yes | yes |
| Seal | consumed at beasts | carried indefinitely |
| Cord | yes | **no** |
| Erasure | yes (switch class) | n/a (cannot reach) |
| Hand | yes (severed at kill) | yes (handover) |

## Slots / structure

*TBD — must respect the engine's RAM ceiling (~2.5 KB total).
Inventory size is a design *and* footprint constraint.*

Items committed at setting-level (above) are **few and structurally
named**, not generic loot. The inventory is not a typical RPG bag;
it is a small named-slot system. Specifics:

- Number of slots, ordering, on-screen presentation: **TBD**
  (defer to UX implementation).
- Whether NPC rewards expand the inventory (and how many): **TBD**.
- Whether the inventory is a single screen or contextual (Hand
  available everywhere; Cord usable in Hell only; etc.): **TBD**.

## Item interactions

- **Seal + Cord:** an unburdened Vagrant has no Cord. The Seal
  carriers can use the Seal at any moment, including mid-Hell, to
  class-pick — at which point the Cord becomes available (because
  they are now class-picker).
- **Erasure + Seal:** Erasure switches class only among already-
  measured states. It cannot un-measure. The Seal is the only path
  *out* of unburdened; once used, the Erasure is the only path
  *between* class-picker classes.
- **Hand functions:** path-specific. Class-picker uses it as
  portable OFFERINGS access (sangue → stats anywhere). Unburdened
  uses it as portable riversamento site (riversa anywhere). The
  underlying mechanic — *the Hand grants the function the Wood-Guide
  used to provide* — is the same for both paths; the function differs
  because the path's mechanics differ.

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
- The Hand's exact mechanic shape — does it require a button press to
  invoke, a hold-to-confirm, a menu? Defer to UX.
- Cord cost curve — exact sangue cost per circle / keeper-status.
  Defer to balance tuning.

## Cross-references

- [Setting](setting.md) — *Item system*, *The Cord*, *Vestigia*,
  *Sangue and the law of substance*.
- [Story](story.md) — *The Seal* (Beat 3-4), *The Hand* (climax).
- [Classes](classes.md) — Erasure switches class.
- [Economy](economy.md) — sangue cost of Cord uses, sangue investment
  via OFFERINGS / Hand.
- [UX](ux.md) — inventory screen, item-use UX.

---

## Brainstorming notes (2026-05-28) — teleport markers (working name TBD)

> Status: in-progress design conversation, NOT canon yet.
> Captured here so the thinking persists across sessions.

### What the bonfire problem looks like in Selva

Souls' bonfire bundles many functions (save / respawn / level-up /
equip / heal / repair / fast-travel / world-reset / tonal anchor).
Selva already distributes those across authored systems:

| Function | Selva system |
|---|---|
| Save | Vestigia (autosave on death / retreat; manual Inscribe TBD) |
| Respawn | The Wood (cosmologically, per fallback.md) |
| Stat-up / level-up | OFFERINGS at the Wood; Hand makes portable late-game |
| Equip / loadout | Inventory screen (UX TBD, but it's a screen, not a place) |
| Heal | Hand (anywhere, late-game); pre-Hand healing is a real TBD |
| Repair / craft | Imprint-making — the verb the Vagrant carries (per crafting.md brainstorming notes); no bench |
| Fast travel out | The Cord (Hell → Wood) |
| Tonal anchor | The Wood IS the tonal anchor; in-Hell rest is doctrinally absent |

So "a checkpoint" in Selva has only one *unmet* function: **fast
travel INTO Hell** (the Cord covers OUT). The marker system is
designed for that one purpose. It is not a bonfire; it is a
return-point.

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
- **Wood → marker teleport.** The Vagrant initiates from the Wood,
  pays sangue (depth-scaled, same curve as the Cord), arrives at
  the marker. The Wood remains the hub — markers do NOT enable
  marker → marker direct teleport.
- **Cord still works**: Hell → Wood. Marker fills the inverse leg.
- **No save effect.** Vestigia handle save; markers don't change
  that. Respawn on death is still the Wood, not the last marker.
  No save-scum loop possible.
- **No healing on arrival from the marker itself.** Healing is the
  Hand's job (late-game) and consumables (TBD). The marker is not
  a place to rest.
- **Optional gentle softening:** the *transit itself* partially
  restores HP — Hell briefly loses sight of the Vagrant mid-jump,
  he reconstitutes at the destination. Avoids "teleport mid-fight
  to low-HP arrival" feeling cheap without making the marker a
  rest site.

### Path implications

- **Class-picker has markers** (parallel to the Cord, which is
  class-picker-only).
- **Unburdened does not have markers** (parallel to no Cord). The
  unburdened path is canonically harder; they walk back. This
  reinforces the path distinction without inventing new
  asymmetry.

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
- *Sigillo minore* (lesser seal). Connects to *Il Sigillo* (the
  Seal, single-use class-pick) but in a smaller, plural, reusable
  register. Beautiful but possibly confusing — they ARE different
  things mechanically.

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
