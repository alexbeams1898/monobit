# Weapon XP

> **Owns:** how individual weapon instances grow in power through
> use — what XP means cosmologically, what it does mechanically,
> who can gain it, and how it interacts with the substrate
> persistence rules.
> **Status:** structural locks; specific curves TBD at tuning.

## What weapon XP is

**Weapon XP is sangue installed into the weapon's imprint over
time.** Every kill landed with the weapon routes a small amount of
the kill's sangue into the imprint, hardening its form. The
weapon's level rises as substance accumulates in it.

This composes directly with the substance economy (per economy.md
*Sangue and the law of substance*). Stats are sangue installed in
the Vagrant's substrate; weapons are sangue arranged in object-form;
weapon XP is **continued sangue installation into an existing
imprint**. Same cosmological mechanism, different target. Crafting
makes the imprint; use deepens it.

The metaphor at the surface: a blade that has killed a hundred
shades is not the same blade. The substance of those kills has
worked its way into the imprint. The Vagrant's hand keeps
re-marking the weapon every time it strikes — each strike is a
micro-imprinting; the imprint deepens.

## Mechanical model

**Inherited verbatim from prison-escape-game** (per crafting.md
*System reuse*), reframed cosmologically for Selva.

### Per-instance fields

Every weapon `ItemInstance` carries:

- `weapon_xp_level: int` — starts at 1, grows with use.
- `weapon_xp_current: float` — XP accumulated toward the next
  level. Resets to 0 at level-up.
- `evolution_bonus: float` — permanent damage / scaling bonus
  carried forward across evolutions. Persists when the weapon
  transforms into its next-tier form. Starts at 0.

These live on each `ItemInstance`, not on the `ItemDef` — two
copies of the same item have independent XP states.

### XP gain

XP is granted on **kills landed with the weapon equipped**. The
amount per kill is a function of:

- The enemy's archetype-level (or boss / mob tier)
- The wielder's LCK stat (small scaling)
- A weapon-family multiplier (TBD; balance hook)

Specifics — exact XP-per-kill curve, level-thresholds — deferred
to gameplay tuning. The principle is **kills with this weapon
make it stronger**; the curve flattens at higher levels.

### What leveling does

A weapon level-up increases:

- `base_damage` — additive damage step per level (curve TBD).
- `scaling_per_level` — incremental stat-scaling step (e.g. STR
  scaling C → C+ → B over levels, curve TBD).

The weapon's stat *requirements* do **not** change with level —
those are intrinsic to the `ItemDef`. A high-level dagger still
needs the same STR/DEX to wield.

### Evolution bonus

When a weapon evolves (per weapon-evolution.md), the new weapon
starts at `weapon_xp_level = 1` and `weapon_xp_current = 0`, but
carries forward `evolution_bonus` from the prior weapon. The
bonus represents the accumulated sangue-substance that was
re-shaped, not reset, during the evolution act — the new imprint
inherits the prior imprint's depth.

This means **a weapon evolved at level 5 is meaningfully stronger
than a freshly-crafted version of the same evolved weapon**, even
though both read `level 1`. The evolution_bonus encodes the
difference.

## Universal mechanism, asymmetric in practice

**Every wielder gains weapon XP on the weapons they wield.** The
mechanism is universal — no special-casing by class. Sangue
installs into the weapon's imprint regardless of who's holding it.

In practice, however, **the Unburdened almost never accumulates
weapon XP**, because the 1/1/1/1 stat lock prevents him from
equipping most weapons (per inventory.md *Stat-gated equip*). He
can equip the tier-0 1/1-req descent-stair starting weapon and a
small handful of comparable tier-0 tools, but anything higher is
locked out of his hand. Practical consequence: **weapon XP is a
class-picker progression mechanic, by stat-gate side effect, not
by cosmological carve-out.**

The Unburdened's progression vector is incants (per inventory.md
*Path implications*), gated by riversamento volume + Svuotato /
Diaphanous unlocks — a separate system, not weapon XP. See the
future incant design doc (TBD).

## Unarmed is a weapon — gains XP universally

Per inventory.md *Unarmed is a weapon*, every character starts
with an `unarmed` `ItemInstance` in inventory, equipped to
`Equipment.unarmed`. **It gains weapon XP same as any other
weapon.**

Class differentiation rides on **scaling**, not on the item:

- **Feral** class passive massively buffs unarmed damage scaling.
  His acquired-stat (predation / feasting volume — per
  [[project_class_acquired_stat_doctrine]]) further multiplies it
  at threshold tiers. His unarmed XP curve and evolution branches
  are the dominant progression path for his class.
- **Penitent / Heretic** scale unarmed weakly. Useful as a
  fallback when no weapon is equipped or when the player has lost
  their weapon to Hell-reclamation on death; not a primary verb.
- **Unburdened** scales unarmed not at all. Unarmed XP still
  accumulates (the mechanism is universal) but the level-ups
  produce vanishingly small damage gains because the scaling
  multipliers are zero. He'll mostly use unarmed as gesture-only
  combat until incants come online.

**Pre-Beat-4 fists-only combat already gains XP.** Every Vagrant
fights with fists from game-start through the descent. By the
time the player reaches the descent-stair starting weapon, their
unarmed is already L2 or so. This is intended.

## Substrate persistence

The Wood-vs-Hell substrate split (per crafting.md
*Hell-imprint vs. Wood-imprint — persistence rules*) applies to
weapon XP **without modification**:

- **Wood-substrate weapons** persist across cycles. Their
  accumulated XP and `evolution_bonus` persist with them.
- **Hell-substrate weapons** reclaim on second death. The weapon
  dissolves; the XP and `evolution_bonus` dissolve with it.

There is **no separate lifetime weapon-XP register** that survives
death. XP lives in the weapon; if the weapon dies, the XP dies.
This is the same rule as uncommitted vessel sangue (per economy.md
*Reclamation*).

The risk-reward implication composes cleanly: a high-XP
Hell-substrate weapon is a powerful tool with a fragility
attached. Wood-substrate weapons grow slowly (lower base power)
but bank the investment durably. The player's gear choices have
a cosmological clock built in.

## Save / load

Each `ItemInstance` serializes its `weapon_xp_level`,
`weapon_xp_current`, and `evolution_bonus` to the save. Loading
restores them verbatim. No migration needed for fresh-game saves
(defaults are level=1, current=0, bonus=0).

## Open questions

- **XP-per-kill curve** — per enemy archetype, per wielder LCK.
  Tuning.
- **Level-up damage step / scaling step** — curve TBD per
  weapon family.
- **Evolution-bonus formula** — fixed-fraction-of-prior-XP vs.
  linear-with-level. Tuning.
- **Weapon-family XP multipliers** — whether daggers, swords,
  greatswords, ranged, etc. have distinct XP curves. Tuning.
- **Unburdened scale=0 vs scale=trivial** — whether unarmed
  level-ups produce any visible damage growth for the Unburdened
  or are functionally noise. Tuning.

## Cross-references

- [Inventory](inventory.md) — where weapon instances live; equip
  rules; stat-gated wielding; unarmed-is-a-weapon doctrine.
- [Weapon evolution](weapon-evolution.md) — what happens when a
  weapon's XP gates an evolution path.
- [Crafting](crafting.md) — imprint-making produces the weapon
  whose XP this doc owns; substrate persistence rules.
- [Economy](economy.md) — sangue as the substance XP IS.
- [Classes](classes.md) — class passives that buff unarmed
  scaling; Feral acquired-stat tier interaction.
- [Combat](combat.md) — damage formulas, stat scaling, how
  weapon level + evolution_bonus feed into damage output.
