# Design

This tree holds the **game design** for the RPG. It is the parallel of
the engineering docs at the top level of `docs/` (platform-parity,
scene-paging, fx-flash, etc.). Engineering docs answer *how the engine
behaves*; these answer *what the game is*.

Each section file owns one aspect of the design. Sections are written
to be readable independently — open `economy.md` for an afternoon of
balance work, open `dialogue.md` for a writing session, etc. Cross-
references between sections are by link.

> **Status legend:** Each section starts as `TBD`. As we iterate, the
> status moves through `drafting` → `stable` → `frozen-for-v0.1`.
> A section is never "done" — it just stops being the bottleneck.

## Index

| Section | Status | What it owns |
|---|---|---|
| [Setting](setting.md) | drafting | World, cosmology, register |
| [Story](story.md) | drafting | Main arc, acts, endings, reveals |
| [Classes](classes.md) | structural | Penitent / Heretic / Wretched, evolutions, stat triad |
| [Character creation](character-creation.md) | pointer | Opening flow, naming, class pick (locked in story / classes / inventory) |
| [Inventory](inventory.md) | structural | Items locked at setting level, path-specific carry rules |
| [Dialogue](dialogue.md) | structural | Register doctrine, per-speaker register table, state-aware branching |
| [Companions](companions.md) | structural | The Guide as sole companion; Beatrice as boss-not-companion |
| [Economy](economy.md) | structural | Sangue currency, sources/sinks, accounting, pressure mechanics |
| [UX](ux.md) | TBD | Screen flow, HUD, menus, control scheme |
| [Options](options.md) | TBD | Settings menu contents |
| [PC vs NPC](pc-vs-npc.md) | structural | Entity symmetry rule in design terms |
| [Fallback](fallback.md) | structural | Death, retry, save semantics, vestigia |
| [Combat](combat.md) | drafting | Melee/physical combat — bindings, stance, on-hand/off-hand grammar |
| [Bestiary](bestiary.md) | drafting | *Figura umana* rule — shared human skeleton for damned souls, canonical exceptions for Hell's classical guardians |
| [Crafting](crafting.md) | drafting | All weapons/tools are crafted, not looted; Wood-substrate vs Hell-substrate split; crafting as the verb of *consuming Hell* |
| [Wood](wood.md) | drafting | The Selva as a place: inner canonical core + outer procgen ring; Hell-leak as the threshold's defining condition; time-stop; persistence of player marks |
| [Creatures](creatures.md) | drafting | Selva-organism ecology spawned from leaked Hell-substance; biology not spirit; four-layer evolution model; capture as personal choice not mission target |

**Status legend:**
- *drafting* — sections being actively written; load-bearing for
  other docs.
- *structural* — cosmological / framework constraints locked from
  setting / story; per-implementation detail still TBD.
- *pointer* — locked elsewhere; this doc is a thin wrapper pointing
  to where the content lives.
- *TBD* — not yet written; open until the question comes up.

## Reference material

- [`docs/dante-cliffs.md`](../dante-cliffs.md) — Park-notes summary
  of the *Commedia* itself. Source-material reference only; the
  active game design is in this tree. Useful for settling on-theme
  questions and looking up canto / figure citations.
- [`docs/business-model.md`](../business-model.md) — platform
  priority, engine/platform split, monetization. Constrains every
  design decision in this tree.

## Authorship rule

These section files capture **the user's design decisions**, in his
words. Don't fill gaps with plausible-sounding extrapolation — leave
gaps visible (`TBD`, open questions) so they can be answered
deliberately. See `.claude/rules-design.md` for the full doctrine.
