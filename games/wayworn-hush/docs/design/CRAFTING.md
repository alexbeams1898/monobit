# Wayworn Hush — Crafting (design direction)

> **Status:** direction, not built, not locked. Mechanizes the crafting doctrine
> already in [GAME-SYSTEMS.md](GAME-SYSTEMS.md) ("craft = key, observe = door,
> understanding = the way through"). Kept general on purpose — the shape, not the
> content. Specifics (recipes, items, copy, exact numbers) live in config + are
> decided when built.

## Shape

Crafting reads as the game's **objective spine** — the player pursues it as the
mechanical path forward. Underneath, every worthwhile recipe is gated behind
*understanding*, so pursuing the spine walks the player through the game's quieter
substance. The surface loop is real and satisfying; the gate is where meaning
lives.

## Two layers, kept separate

- **The act is fast.** Select materials → one confirm → an immediate, juicy result.
  Never a dialog tree, never tedious. All depth is in the *content* and the
  *discovery*, not in the moment-to-moment friction.
- **Discovery is the substance.** How a recipe becomes known is where the game's
  identity is — see below.

## Discovery through attempt

You are on your own; figuring things out yourself is the game. Recipes are not
handed over as schematics. You **attempt combinations** — and a valid one teaches
the recipe. Attempting is the verb, understanding the reward — the same primitive
as the rest of the game.

What you can realize is gated by two forces, both expressed with the **existing
unlock-condition primitive** (no new gate system):

- **Understanding** — a recipe can require prior insight (a landed thought, or an
  observation reached to depth). You realize a making only once you understand its
  parts.
- **Capability** — a recipe can require a level of the doing-layer stats.

A failed attempt makes nothing and **costs nothing** (materials returned) — free
experimentation. A derived **closeness** signal (how much a tried combination
overlaps a real one) guides without hand-authoring. Near-misses are the guidance,
in keeping with "failure is content."

## Outcome, XP, and mastery

- **Outcome quality** (for consumable results) is a formula of the recipe's
  scaling, the doing-layer stat, and a bounded roll — better inputs, better result.
  Deterministic results (keys, tools) don't scale; they open or they don't.
- **XP** feeds the doing-layer stats, attributed to the making. The amount runs
  **inverse to mastery** — reaching above your level pays more, routine work pays
  little — so the curve is anti-grind by construction, matching the growth
  doctrine.
- **Mastery** is the doing-layer stat itself growing; no separate track.

## Reuse, not reinvention

Crafting is assembled from systems already built: the unlock-condition primitive
(the gate), the inventory ops (consume inputs, grant output), the formula pattern
(outcome + XP math in code, coefficients in config), the roll source (the lottery),
and the interaction layer (a making happens at an interactable). It is one system;
a result is simply flagged consumable or permanent. Making can also *open*
understanding (a two-way loop), not only consume it.

## Open threads (decided when built)

- The craft act's surface (how materials are selected + confirmed) — the biggest
  feel risk; must stay fast.
- How permanent/structure results interact with the world, and whether *place*
  informs a making.
- The first making, and how its recipe is known at the start.
- The exact XP/outcome curves — a sibling of the doing-layer progression.

## First slice (once agreed)

1. Recipe data model + loader + pure tests (gating, closeness, outcome, XP), no UI.
2. A few recipes gated on existing understanding, to prove the gate.
3. The fast craft act (select → confirm → grant + feedback), with the closeness
   signal on misses.
