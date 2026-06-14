# Weapon evolution

> **Owns:** how weapons transform into higher-tier weapons through
> branching trees — the upgrade DAG, the act of evolving, the
> material + sangue cost, and the Feral acquired-stat interaction
> for unarmed.
> **Status:** structural locks; specific tree shapes and balance
> curves TBD at content authoring.

## Core model

**Weapon evolution is a branching DAG per weapon family.** A
weapon at sufficient XP level can transform into one of several
possible next-tier weapons, consuming materials and sangue in the
act. Each evolution path is a one-way choice — once a weapon
evolves down a branch, the prior form is gone.

**Inherited verbatim from prison-escape-game** (per crafting.md
*System reuse*), with Selva-specific layering for cosmology and
the imprint-making verb.

## The data model

### Evolution registry

Per-weapon-family JSON files at
`games/selva-oscura/config/evolution/{blades, bludgeons, ranged,
unarmed, ...}.json`. Each file defines a registry of
`EvolutionNode` entries:

```json
{
  "weapon_config_path": "config/items/weapons/wood_pilgrim_knife.json",
  "evolutions": [
    {
      "target_node": "config/items/weapons/wood_bone_knife.json",
      "min_level": 4,
      "material_config_path": "config/items/materials/larval_residue.json",
      "material_qty": 3,
      "sangue_cost": 50
    },
    {
      "target_node": "config/items/weapons/wood_flint_knife.json",
      "min_level": 4,
      "material_config_path": "config/items/materials/flint_shard.json",
      "material_qty": 2,
      "sangue_cost": 50
    }
  ]
}
```

### Fields per evolution path

- `target_node` — the `ItemDef` config path the weapon evolves
  INTO. Same family as the source.
- `min_level` — required `weapon_xp_level` on the source weapon.
  The weapon cannot evolve below this level.
- `material_config_path` + `material_qty` — material consumed
  from the player's inventory.
- `sangue_cost` — sangue spent at the evolution moment. Reflects
  the cosmological act: substance flowing into the new imprint.

A node may have **zero or more** evolution paths. A leaf node
(no `evolutions` entry) is a terminal form — that weapon does not
evolve further.

## The act of evolving

**There is no workbench.** Evolution is performed through the
**imprint-making verb** — the same verb that makes new weapons
from scratch (per crafting.md *Crafting is imprint-making*). The
Vagrant re-marks the existing weapon's substance into the new
form. He does this wherever he is, with sangue + materials in
hand.

### Step-by-step

1. Player opens the imprint UI (UX TBD — likely a menu accessible
   from inventory or a contextual action).
2. Player selects a weapon in inventory that has at least one
   available evolution path (`weapon.level >= path.min_level` for
   at least one path AND player has the material + sangue).
3. Player picks which branch to evolve down (if multiple are
   available).
4. Materials consumed from inventory.
5. Sangue spent from the wallet.
6. Old weapon **removed from inventory entirely**.
7. New weapon **spawned in the same inventory slot** (or wherever
   the equip indices were pointing — `Equipment.right_hand` stays
   valid through the swap).
8. `evolution_bonus` carries forward from old to new (per
   weapon-xp.md *Evolution bonus*).
9. New weapon starts at `weapon_xp_level = 1`, `weapon_xp_current
   = 0`. XP accumulation continues from this base.

### Branching

A weapon may have **one OR multiple evolution paths** declared.
When multiple exist, the player chooses. **The choice is
permanent** — once the weapon evolves down branch A, branch B is
not reachable from this weapon. To get branch B, the player must
acquire / craft / pick up a fresh copy of the source weapon and
level it from scratch.

This gives evolution choices teeth — the player commits to a
build direction with each evolution. Souls-genre convention; works
because Selva has crafting-as-renewable-source for any
non-key-item weapon (per crafting.md).

## Family-locked

A weapon **cannot evolve out of its family.** A dagger never
evolves into a mace. Family boundaries are intrinsic to the
weapon-family tree files (`blades.json`, `bludgeons.json`, etc.) —
nothing in `blades.json` can declare a `target_node` that
resolves to a weapon in `bludgeons.json`.

This is the prison-escape convention, preserved. Rationale:
families correspond to combat-feel categories (weapon class,
moveset, animation set), and cross-family evolution would require
re-binding moveset / animations mid-evolution. Out of scope.

## Substrate persistence applies

The Wood-vs-Hell substrate rules (per crafting.md *Hell-imprint
vs. Wood-imprint — persistence rules*) flow through evolution.
**A weapon's substrate is intrinsic to the `ItemDef`, not the
instance**, so evolution can change a weapon's substrate by moving
it to a target node with a different substrate flag:

- **Wood → Wood evolution**: stays persistent across cycles.
- **Hell → Hell evolution**: stays reclaim-on-death.
- **Wood → Hell evolution**: becomes reclaim-on-death. Player
  trades durability for power.
- **Hell → Wood evolution**: becomes persistent. Rare /
  cosmologically expensive — substance from Hell anchored into
  Wood-substrate. Specific tree shapes TBD.

The substrate transition is a meaningful build-direction lever.
Wood-tree starting weapons can branch into Hell-tier endgame
forms; Hell-tier finds can branch into preserved Wood-imprinted
artifacts (rare).

## Sangue cost — load-bearing

**Evolution costs sangue, not just materials.** The sangue spent
at evolution time is substance flowing into the new imprint —
parallel to how crafting from scratch spends sangue (per
crafting.md *Sangue flows into everything he makes*).

Implication: **evolution competes with stat installation
(Crucible) and marker placement for the sangue budget**. The
player is always choosing what to spend substance on. A
class-picker descending with a stockpile of sangue may pour it
into an evolution mid-run rather than commit to a stat raise;
that choice is meaningful and cosmologically legible.

The sangue cost scales with evolution tier — higher-tier
evolutions cost more sangue. Curve TBD at tuning.

## Unarmed evolution

**Unarmed is a weapon (per inventory.md *Unarmed is a weapon*),
so unarmed has its own evolution tree.** The unarmed evolution
file (`config/evolution/unarmed.json`) defines per-stage forms:
fists → callused-hands → claws → teeth-and-claws → something
feral-coded at terminal.

**Feral acquired-stat tier-thresholds gate unarmed evolution
branches.** Per [[project_class_acquired_stat_doctrine]], the
Feral evolves form at acquired-stat thresholds (L1 → L2 → L3 —
the class form-evolution that thins his body / changes his
silhouette). His acquired-stat tier acts as **an additional
gating condition on unarmed evolution paths**: only when his form
has crossed the corresponding threshold do the next teeth/claw
evolution branches become available.

Cosmological reading: the Feral's body-form change is what makes
the next teeth/claw evolution available. He cannot evolve to
"claws" if his body has not yet thinned into the form that grows
claws. The unarmed weapon evolves *with* the Feral's body, not
separately from it.

**Other classes can also evolve their unarmed**, but along
different branches. A Penitent's unarmed might evolve toward
hardened knuckles / weight-of-hand forms; a Heretic toward
quick-strike / palm-strike forms. Cross-class branches share
no leaf nodes — the unarmed tree branches at the very first
evolution point based on class. Specific tree shape TBD at
content authoring.

The Unburdened's unarmed effectively does not evolve in practice —
he has no sangue budget for it (every drop goes to riversamento)
and no class passive driving the verb. The branches exist; he just
won't reach them. This is consistent with his combat capability
coming from incants, not from unarmed.

## Evolution and second death

The standard substrate reclamation rule applies (per
weapon-xp.md *Substrate persistence*):

- Hell-substrate evolved weapons reclaim on second death.
  Everything — the form, the XP, the `evolution_bonus` — dissolves
  with the weapon.
- Wood-substrate evolved weapons persist.

A class-picker who evolved a Hell-craft dagger into a tier-3
Hell-craft form will lose it all on second death. The player's
decision to invest in Hell-craft endgame is a real risk. Wood-
craft endgame is durable but slower-growing.

## Save / load

Evolution state lives in the weapon `ItemInstance` — the
`config_path` field changes when the weapon evolves (it now
points to the target node). `weapon_xp_level` and
`weapon_xp_current` reset to 1 / 0; `evolution_bonus` updates
per the carry-forward formula. Save round-trip handles this
automatically with no special logic.

## Open questions

- **Tree shapes per family.** Specific evolution graphs for
  blades, bludgeons, ranged, unarmed. Content authoring.
- **Sangue cost curve per tier.** TBD at tuning.
- **Evolution-bonus carry-forward formula.** Fraction of prior
  XP? Linear with level? TBD at tuning, see weapon-xp.md.
- **Material specifics per evolution.** What materials gate
  which branches. Content authoring.
- **Imprint UX for evolution.** Whether the UI is shared with
  general crafting or has its own surface. Defer to UX pass.
- **Visual evolution moment.** Whether evolution has a brief
  cinematic / hand-gesture beat or is instant menu-action.
  Probably a small in-hand visual flourish — the weapon
  re-shapes briefly. TBD.

## Cross-references

- [Inventory](inventory.md) — where evolved weapons land; equip
  indices stay valid through evolution.
- [Weapon XP](weapon-xp.md) — XP gates evolution; bonus carries
  forward.
- [Crafting](crafting.md) — same imprint-making verb; substrate
  persistence rules; sangue-into-imprint cosmology.
- [Classes](classes.md) — Feral acquired-stat gates unarmed
  branches; class differentiation in starting unarmed branches.
- [Economy](economy.md) — sangue cost composes into the run
  budget alongside stat installation and marker placement.
- [Combat](combat.md) — evolved weapons feed combat damage
  formulas; family-locked rule keeps moveset bindings stable.
