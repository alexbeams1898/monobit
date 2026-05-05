# Character creation

> **Owns:** the opening flow — naming, class pick framing.
> **Status:** locked elsewhere — this doc is a pointer.

## What's locked, where

The opening flow is a *narrative sequence*, not a separate
character-creation module. It lives in [story.md *The opening
sequence*](story.md), Beats 1-5:

- **Beat 1 — Cold-open in the basic-form Wood.** The Vagrant wakes
  in the *selva oscura*. Walkable, not a cutscene.
- **Beat 2 — The beasts.** Lonza / Leone / Lupa, fought as Unburdened
  (no measurement yet, senza forma intact).
- **Beat 3 — The Guide arrives.** Naming: the Guide elicits six
  letters from the player. *The name is given, not recovered.* The
  Guide offers The Seal.
- **Beat 4 — The choice.** Use the Seal (class-pick: Penitent /
  Heretic / Wretched) or carry it (unburdened). This is the
  game's class-creation moment.
- **Beat 5 — Transition.** Vagrant proceeds to the gate of Hell;
  basic-form Wood transitions to hub-Wood.

## Naming

Six letters. Player types via input. The Guide elicits the name
("what to call him"). The name is a *given* identity, not a
*recovered* one — the Vagrant has no memory of his pre-Hell name; he
answers to whatever the player provides. NAME_ENTRY UI specifics
in [ux.md](ux.md).

## Class pick

Mechanism: **The Seal** (*Il Sigillo*). Single-use item given by the
Guide at Beat 3. Using it triggers class selection (Penitent /
Heretic / Wretched) and consumes the Seal; carrying it without using
preserves senza forma and continues the Vagrant as Unburdened.

The class-pick fork, the three classes' identities, and evolution
paths all live in [classes.md](classes.md). The Seal's mechanics and
the Erasure (the late-game class-switching item) live in
[inventory.md](inventory.md).

## Cross-references

- [Story](story.md) — *The opening sequence* (Beats 1-5), the
  narrative spine of character creation.
- [Classes](classes.md) — the three classes, evolutions, stat
  profiles.
- [Inventory](inventory.md) — The Seal, the Erasure.
- [UX](ux.md) — name-entry screen, class-pick UI, transition
  sequencing.
