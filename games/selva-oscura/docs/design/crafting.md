# Crafting

> **Owns:** the rule that all weapons and tools are crafted, not
> looted — and the substance-based split between Wood-craft and
> Hell-craft.
> **Status:** core rule locked; recipe/material specifics TBD.

## Core rule: nothing is pre-made

**Hell does not contain weapons.** Hell is punishment, not arsenal.
The damned souls do not bear arms — they are subjects of torment, not
combatants. The keepers act on the bodies of the damned with claws,
teeth, body weight, jaws, stingers, wings — what their substance
already provides. Tools are *not native to Hell.*

The Vagrant arrives empty-handed in a place full of bodies. Anything
weapon-shaped he wields, he has **made.** This is the absolute rule.
There are no chests of swords waiting to be looted. There are no
fallen weapons on the ground next to dead shades. No NPC hands him a
blade. Every weapon in his hands exists because **he extracted the
matter for it and shaped that matter himself.**

This rule applies to both paths. The Vagrant crafts in Hell; the
Vagrant also crafts in the Wood. The substrate differs by location
(see *Substrate split* below), but the rule — **all weapons are
crafted** — is invariant.

## Crafting as consumption (Hell side)

Hell-craft is the Vagrant **consuming Hell into form.** This is the
mechanical expression of the *consuming Hell* narrative (per
setting.md). To craft a weapon in Hell is to *take* — to flay a shade
for its sinew, to break and shape its bone, to bind a damned soul's
substance into an edge.

Every Hell-crafted weapon carries the imprint of what was consumed
to make it. A shade-bone dagger remembers the shade. A sinew-bound
hilt remembers the gluttons whose flesh wove it. The weapon is not
inert — it is **a fragment of Hell shaped to the Vagrant's will**,
and the act of making it is itself a small *consuming-Hell* event.

This resolves a quiet tension in the cosmology: the Vagrant has no
imprint Hell can grip (per setting.md *Second death* / *the
unjudged*), but he is not merely *immune* — he is **active.** Hell
moves through him out (riversamento, on the unburdened path) and
into him as form (crafted matter, on either path). Crafting is the
verb the *consuming Hell* narrative requires. Without it, "consuming
Hell" stays abstract. With it, the player performs the verb every
time they sit at a workbench.

## Substrate split

The substrate of crafting differs by location:

| Location | Substrate | Register |
|---|---|---|
| **Wood (selva oscura)** | Root, branch, stone, pilgrim-relic, fallen-traveler matter | Humble, found, wooden |
| **Hell** | Shade bone, sinew, damned-soul matter, hypertrophic-form residue, sangue-coagulate | Visceral, wrenched, consuming |

The crafting *mechanic* is the same on both sides. The recipe registry,
the workbench interaction, the material slots are shared system. What
changes is the material pool the player has access to — gathered from
what the location's substrate makes available.

**Wood-craft is gentle in tone.** The Vagrant builds from what fell
naturally or what was left behind. There is no violence required to
gather. A walking-staff from a deadfall branch. A sling from sinew of
a pilgrim's abandoned pack. The Wood does not bleed into the weapon.

**Hell-craft is violent in tone.** Every Hell material was *taken*
from a soul being punished. The wrenched-from-Hell register reads
through the weapon's *visual*, its *flavor text*, its *sound* when
swung. The same dagger silhouette in Wood-substrate and Hell-substrate
feels different in the hand.

## Path implications

**Both paths craft.** The unburdened path (sangue-pure, never invests
at OFFERINGS, pours sangue out via riversamento) is not denied
weapons. The unburdened still descends into Hell, still meets
keepers, still faces shades — and still needs the means to dispatch
them so they can receive second death and have their sangue return
to circulation.

The unburdened's craft inherits an additional flavor: the act of
**taking matter from a shade to make a weapon is itself a form of
forced repentance through second death** (per setting.md). The
unburdened does not invest sangue, but they do extract substance —
the very same substance — and reshape it as the means of granting
the next second death. The cycle is closed: shade → weapon → next
shade's second death → that shade's substance into the next weapon.

Class-picker craft has fewer cosmological knots — they install
sangue at OFFERINGS, they spend, they grow stat-wise. Crafting is
an orthogonal axis for them: their *equipment* gets stronger via
crafting, their *body* gets stronger via OFFERINGS. The two are
separate progression vectors.

For the unburdened, crafting is the *only* progression vector.
Stats are locked at base (per classes.md). Their power comes from
**what they have made** and how skillfully they use it. This makes
crafting load-bearing for the unburdened in a way it isn't for
class-pickers — and is one more piece of the unburdened path's
distinct register.

## System reuse: prison-escape-game's loot + crafting + item model

The mechanical system is **inherited wholesale from
`games/prison-escape-game/`**. Selva does not invent a new crafting
or item system; it adopts the working one and re-skins it with
Dantean substrate. Specifically:

- **ItemDef + ItemRegistry** (per `ecs/ItemConfig.h`) — items live
  as JSON files at `config/items/{materials,weapons,armor,
  accessories,consumables,...}/<name>.json`.
- **ItemCategory** — `Weapon / Armor / Consumable / KeyItem /
  Material`. Inherited as-is. Selva-specific items (Cord, Hand,
  Erasure per inventory.md) slot into these categories.
- **ArmorSlot** — `Head / Chest / Legs / Feet`. Inherited as-is.
- **Rarity** (6 tiers: `VeryCommon → Common → Uncommon → Rare →
  Epic → Legendary`) — set per-ItemDef. Intrinsic to the *kind*
  of item; a "shade-bone dagger" template has a fixed rarity.
- **QualityTier** (5 tiers: `Crude → Common → Fine → Superior →
  Masterwork`) — set per-instance at craft/drop time. The same
  item template can produce instances of different quality based
  on craft conditions (workbench tier, material quality, future
  luck/skill stat).
- **RecipeDef + RecipeRegistry** — recipes live at
  `config/recipes/<name>.json` with the schema
  `{name, inputs: [{item, quantity}], output, output_quantity}`.
- **Drop tables on enemies** — per-archetype `loot.drops` array of
  `{item, min, max, chance}` entries, rolled on death.
- **WeaponTierRegistry + evolution trees + compendium** — also
  available to inherit when needed.

This means crafting in Selva starts with a proven, tested data
model: enemies drop materials, materials are spent at workbenches
following recipe JSONs, recipes produce items of a known
rarity (intrinsic) and rolled quality (per-instance). The
cosmological re-skin (substrate split, second-death reclamation)
layers on top.

### Rarity vs. quality — Dantean reading

Both axes survive intact, but they read differently in Selva's
register than they do in prison-escape:

- **Rarity** = how rare it is for Hell or the Wood to *yield* this
  kind of substance. A *Legendary* material is something Hell
  almost never sheds — keeper-tier residue, Lucifer-feather, a
  named-traitor's tooth. The rarity flag is metaphysical, not just
  a drop-rate dial.
- **Quality** = how well the Vagrant *shaped* what he extracted.
  A *Masterwork* shade-bone dagger and a *Crude* shade-bone dagger
  are made from the same substrate; the difference is in the
  Vagrant's hand at the bench. This is consistent with the
  *consuming Hell* verb — the player is the agent of form, even
  when the substance is the same.

This split also resolves cleanly with the path distinction:
class-pickers' stat growth gives them better craft-quality outcomes
over time (their hand gets steadier); the unburdened's stat-locked
state means their quality variance has different sources (TBD —
candidates: ritual condition, riversamento count, NPC favor).

## Drops and gathering

Materials come from defeating enemies (drop tables, exactly as in
prison-escape) and from gathering at environmental nodes. The
principle: **what the Vagrant kills, he can reshape; what the world
offers, he can take.**

- **Kills.** Each archetype gets a `loot.drops` array in its
  archetype JSON (see `config/enemies/`). Shade kills yield shade-
  substrate; keeper kills yield keeper-tier materials; husks of the
  unbound yield their distinct residue. Drop-table specifics TBD.
- **Environmental gathering.** Wood-side nodes (deadfall branches,
  stone, pilgrim-relic sites) and Hell-side canonically-loaded
  nodes (Wood of the Suicides bleeding-branches, Phlegethon sangue-
  scoop, Cocytus tears-ice, heretic-tomb fragments) yield materials
  on interaction. Gathering reuses the same item-drop pipeline as
  combat loot. Specifics TBD.

## Recipe registry

Recipe JSONs follow the prison-escape schema (per ItemConfig.h
`RecipeDef`):

```json
{
  "name": "Shade-bone Dagger",
  "inputs": [{ "item": "config/items/materials/shade_bone.json", "quantity": 3 }],
  "output": "config/items/weapons/shade_bone_dagger.json",
  "output_quantity": 1
}
```

Basic recipes are known from the start; advanced recipes are
discovered through play. Recipe-learn triggers TBD (candidates:
NPC dialogue per `setting.md`, Hell-side benches imprinted by past
Vagrants, recipe-stones in the Wood). Recipe tree shape, branching,
unlock conditions all TBD at gameplay tuning.

## Workbench interaction

The Vagrant interacts with a **workbench** (or its equivalent) to
craft. Workbenches exist in both substrates:

- **Wood-side benches** — found objects at safe sites in the Wood.
  Specifics (where they appear, how they look) TBD.
- **Hell-side benches** — TBD. Working hypothesis: each circle has
  a bench-equivalent the Vagrant can use. Form is open — could be
  a literal forge, could be a body the Vagrant works at, could be
  ritual-circles drawn in shade-ash. Whatever it is, it reads as
  *visceral and improvised* rather than *industrial.*

Whether a workbench is portable (carried with the Vagrant, like the
Hand) or always stationary is TBD.

## Cosmological framing

Crafting is — like the sangue economy (per economy.md) — not separate
from the cosmology. It *is* the cosmology, expressed at a different
register:

- The sangue economy is **substance moving between souls and the
  Vagrant.**
- Crafting is **substance moving from souls/world into form the
  Vagrant carries.**

Both are flows. Both implement the same underlying rule: Hell is
made of *substance* (per setting.md *Sangue and the law of
substance*); the Vagrant interacts with that substance by extracting
it. The economy reads its flows as quantities; crafting reads its
flows as shapes.

A Hell-crafted weapon is therefore *not* metaphorically "made of
Hell" — it is *literally* made of Hell. The matter inside it was
substrate at the moment before the Vagrant took it. This means a
weapon held by the Vagrant on his second death **returns to Hell
along with his wallet sangue** (per economy.md *Reclamation*), since
both are substance Hell remembers and reclaims.

Unless — TBD — there is a category of weapon (Wood-substrate only?
Or items that have been *named* somehow?) that persists across
runs. This connects to the vestigia system (per fallback.md) but
specifics are open.

## Crafted weapons and the second-death framework

When the Vagrant dies (per the second-death system), all
**Hell-crafted weapons in inventory are reclaimed by Hell** alongside
his wallet sangue. They are substance; Hell takes its substance back.

Wood-crafted weapons — TBD — may persist, since their substrate
never belonged to Hell. This is the same logic as why the unburdened
can carry sangue-pure: matter that originated outside Hell's
accounting is not subject to Hell's reclamation. Specifics open.

This creates a meaningful contrast: **Hell-craft is powerful but
ephemeral**; **Wood-craft is humble but durable**. The player's
relationship with their gear has a cosmological clock built in. TBD
at tuning whether this is a soft pressure (light HP/durability hit on
Wood gear) or a hard rule (Hell gear gone on death, full stop).

## Inherited items doc

The `inventory.md` doc (currently lighter than `economy.md`) needs
a pass to align with this inherited model — specifically to declare
which categories Selva uses, which ArmorSlots are active for the
soulslike (probably all four), and how the existing locked items
(Cord, Hand, Erasure) slot into the ItemDef schema. Deferred until
crafting moves from design to engineering.

## Open questions

- **Recipe specifics.** Full recipe tree, material requirements,
  output stats, unlock conditions — all TBD at gameplay tuning.
- **Material drop tables.** Per-shade, per-keeper, per-husk drop
  tables. TBD.
- **Workbench form and placement.** Wood-side bench appearance;
  Hell-side bench appearance per circle; whether benches are
  portable. TBD.
- **Wood-craft persistence rule.** Whether Wood-crafted weapons
  survive second death, and if so under what conditions. TBD.
- **Recipe-learn triggers.** Whether recipes drop from kills, are
  found at sites, are taught by NPCs, or some mix. Per setting.md
  NPCs provide a benefit on first encounter — recipe-learn is a
  candidate form. TBD.
- **Crafting UI register.** How the workbench interaction feels in
  the hand (menu vs. ritual vs. physical-puzzle). The Wood-side
  and Hell-side registers differ; whether the *UI* visibly differs
  between them is open.
- **Field-craft vs. bench-craft.** Whether some basic items can be
  crafted in the field (a torch from a branch + flint) vs. all
  crafting requiring a bench. TBD.
- **Item categories.** Whether crafting produces only weapons +
  consumables, or also armor / tools / sangue-interacting items
  (the Cord, the Hand — per inventory.md). The Cord and Hand are
  Hell-given per current docs; whether crafted variants exist is
  open.

## Cross-references

- [Setting](setting.md) — *Sangue and the law of substance*,
  *Consuming Hell*, *Forced repentance through second death*, *The
  Vagrant / the unjudged*.
- [Economy](economy.md) — substance flow; Hell reclamation on
  second death; the unburdened path's lack of OFFERINGS spend.
- [Inventory](inventory.md) — item slots; Cord, Hand, Erasure;
  weapon equip and use. Crafting outputs land in inventory.
- [Classes](classes.md) — stat progression via OFFERINGS (class-
  picker only); the unburdened's stats are locked and crafting is
  their progression vector.
- [Fallback](fallback.md) — vestigia / persistent state across
  second deaths; Wood-craft persistence question lives here.
- [Bestiary](bestiary.md) — what each enemy archetype drops;
  per-archetype substrate flavor.
