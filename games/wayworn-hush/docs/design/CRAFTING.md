# Wayworn Hush — Crafting (design direction)

> **Status:** built (first slice), still open at the edges. Mechanizes the
> crafting doctrine in [GAME-SYSTEMS.md](GAME-SYSTEMS.md). Kept general on
> purpose — the shape, not the content. Specifics (recipes, items, copy, exact
> numbers) live in config + are decided when built.

## Shape

Crafting is **Little-Alchemy simple**: the **ingredients are the only
requirement**. The right things in the pot make the thing — always, whether or
not you'd ever "learned" it. There is no unlock gate, no prerequisite, no
schematic to earn first. You experiment with what you carry; a valid combination
forms.

The depth is in the *content* (what combines into what, what's worth making) and
in *discovery* (stumbling onto a making you didn't know) — never in
moment-to-moment friction.

## The act is fast

Select materials → one confirm → an immediate result. Never a dialog tree, never
tedious. A failed attempt makes nothing and costs nothing (materials untouched) —
free experimentation. A derived **closeness** signal (how much a tried
combination overlaps a real one) guides without hand-authored hints; near-misses
are the guidance, in keeping with "failure is content."

## Learning is a record, not a gate

Making something you didn't already know **records it** — the first time, you
"learn" the recipe (a quiet acknowledgement + a small reward, and a recipe book
reads the record later). This is bookkeeping *after* the fact, never a
requirement *before* it. You never need a recipe to make its thing; you get the
record *by* making it (or by an event, below).

Certain **events can hand a recipe over early** — a moment in the world teaches
it before you'd otherwise stumble on it. That's a shortcut to the record, a
piece of authored content, not a lock. Whether taught or discovered, a recipe is
made the same way: the ingredients.

## Outcome, XP, and mastery

- **Outcome quality** (for consumable results) is a formula of the recipe's
  scaling, a doing-layer stat, and a bounded roll — better inputs and skill,
  better result. Deterministic results (keys, tools) don't scale; they open or
  they don't.
- **XP** feeds the doing-layer stats, attributed to the making, **inverse to
  mastery** — reaching above your level pays more, routine work pays little — so
  the curve is anti-grind by construction.
- **Mastery** is the doing-layer stat itself growing; no separate track.

The learn-reward (what discovering a recipe grants) is a small, config-driven
down-payment on the wider growth economy, tuned when that system is built.

## Reuse, not reinvention

Crafting is assembled from systems already built: the inventory ops (consume
inputs, grant output), the formula pattern (outcome + XP math in code,
coefficients in config), the roll source (the lottery), the notification band
(outcomes surface as toasts), and the interaction/record layers. It is one
system; a result is simply flagged consumable or permanent. Making can also
*open* understanding (a first craft can set a world flag), not only consume
materials.

## Open threads (decided when built)

- More recipes — the actual content; the system is proven, the table is nearly
  empty.
- How permanent/structure results interact with the world, and whether *place*
  informs a making.
- The recipe book surface (reads the learned record).
- The exact XP/outcome curves + how the learn-reward folds into the wider
  progression system.
