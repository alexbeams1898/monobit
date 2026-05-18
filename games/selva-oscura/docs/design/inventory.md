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
