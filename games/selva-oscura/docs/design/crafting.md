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

The Vagrant arrives empty-handed in a place full of bodies. Most of
what he wields, he has **made**. The damned souls do not bear arms;
the keepers act with the substance of their own bodies. **No NPC
hands him a blade. No shade-corpse drops a weapon. Enemy loot tables
yield materials and substance, never tools.**

There is a single tightly-bounded exception: **world-placed weapons
left by prior Vagrants and fallen Wood-travelers**. See *World-
placed weapons* below. Every such weapon is hand-authored and
hand-placed; there is no looted-from-mob path. The rule "the Vagrant
crafts what he wields" remains the default; world-placed weapons
exist because *someone else* once imprinted them and the residue
persisted.

This rule applies to both paths. The Vagrant crafts in Hell; the
Vagrant also crafts in the Wood. The substrate differs by location
(see *Substrate split* below), but the rule — **all weapons except
the hand-authored world-placed ones are crafted** — is invariant.

## World-placed weapons

A small set of weapons exist in the world from game-start, hand-
placed in specific locations the player will reach (the descent
stairs, pilgrim camps, fallen-Vagrant remains, ossuary niches).
These are the third loot source per inventory.md *Loot sources,
summary* — distinct from enemy drops (materials + sangue only) and
the Wood-side chest (player storage).

Cosmologically, world-placed weapons exist for two reasons:

- **Wood-craft persistence.** A pilgrim or Wood-traveler imprinted
  the weapon from Wood-substrate (deadfall, bound stone, sinew) and
  died. Wood-substrate sits outside Hell's accounting (per setting.md
  *The Wood*), so the imprint did not dissolve. Centuries of
  pilgrim-falls have scattered humble tier-0 / tier-1 weapons along
  the descent routes. The starting weapon on the Acheron descent
  stairs is one of these.

- **Prior-Vagrant Hell-craft residue.** Hell's reclamation protocol
  is broken (per setting.md *Hell's failure*). A prior Vagrant
  imprinted a Hell-craft weapon and died; his weapon's substance
  *should* have returned to Hell at his second death, but the broken
  protocol failed to fully reclaim it. What's left is residue — a
  fragmentary Hell-craft weapon, often partial or degraded. Deeper-
  in-Hell containers (ossuaries, pyres, niches) hold these.

Both flavors are hand-placed in region JSON. Neither rolls
procedurally. Neither drops from enemies. The player encounters
them as spatial discoveries; the act of picking one up is the
counterpart to crafting — *someone before me made this and left
it*. The Vagrant does not extract or shape the substance; he
inherits the imprint as-is.

Mechanically, world-placed weapons enter inventory through world-
container interactions (per inventory.md *World containers*).
They have a fixed quality at placement (no roll); their stat
requirements gate equip; they gain weapon XP through use like any
other weapon (per weapon-xp.md); and the substrate persistence
rule decides whether they survive second death (Wood-craft
persists; Hell-craft residue reclaims, finishing what the broken
protocol started).

## Crafting is imprint-making

**Crafting has no bench.** The verb is in the Vagrant's hand. He
marks matter the way Hell marks souls — he is the inverse of Hell's
killing-protocol. Hell takes a soul, presses an imprint into it,
fixes its punishment-shape. The Vagrant takes substance and presses
an imprint into it, fixes its weapon-shape. Same cosmological act,
opposite direction.

Per setting.md *Sangue and the law of substance*, **everything is
sangue, arranged.** A weapon is sangue arranged in blade-form. The
Vagrant arranging it IS the cosmological mechanism — substance
flowing through his hand acquires the shape his intention provides.
This is the *imprint-making* verb. No forge, no anvil, no recipe-
sheet. He does it where he is, with what is at hand.

**Sangue flows into everything he makes at the moment of creation.**
The wallet ticks down; the substance routes into the object-form.
The amount allocated determines the weapon's quality (Crude → Common
→ Fine → Superior → Masterwork — see *Rarity vs. quality*). A
Masterwork shade-bone dagger took more sangue to imprint than a
Crude one. The cost is real and the result expresses the cost.

### Why this works cosmologically

The Vagrant has no imprint Hell can grip (per setting.md *Second
death* / *the unjudged*), but he is not merely *immune* — he is
**the inverse verb.** Hell's killing-protocol imprints souls into
punishment-shape. The Vagrant's imprint-making imprints substance
into tool-shape. Both operate on sangue. Hell does it by
compression; the Vagrant does it by intention.

Without crafting, "consuming Hell" stays abstract. With crafting as
imprint-making, the player performs the cosmological verb every time
they shape substance.

### Hell-imprint vs. Wood-imprint — persistence rules

Where the Vagrant imprints determines what happens on second death.
This is load-bearing for both paths.

- **Hell-imprinted weapons reclaim on death.** Substance imprinted
  inside Hell is *substance Hell remembers*. When the Vagrant dies,
  Hell's killing-protocol fires and reclaims its substance — the
  weapon dissolves with the body, returning to Hell's reservoir. The
  imprint dissolves with it. The Vagrant respawns in the Wood
  without it.
- **Wood-imprinted weapons persist across cycles.** Substance
  imprinted in the Wood is in *the substrate outside Hell's
  accounting* (per setting.md / wood.md). Hell cannot reclaim it on
  death. The weapon stays — in the Wood, or carried back to the Wood
  before death — and survives the cycle. The Vagrant respawns and
  the weapon is still where it was, or still in his hand if he died
  in the Wood.

This is the **mechanical expression of the substrate split.** Wood-
imprinted gear accumulates across cycles. Hell-imprinted gear is
single-cycle. The two operate as a permanence axis: do you imprint
fast and cheap in Hell knowing you'll lose it, or slow and durable
in the Wood knowing it survives?

The path distinction layers on top: see *Path implications* below.

## Substrate split

The substrate of crafting differs by location:

| Location | Substrate | Register | Persistence |
|---|---|---|---|
| **Wood (selva oscura)** | Root, branch, stone, pilgrim-relic, fallen-traveler matter | Humble, found, wooden | Across cycles |
| **Hell** | Shade bone, sinew, damned-soul matter, contrapasso residue, sangue-coagulate | Visceral, wrenched, consuming | Single cycle (reclaimed on death) |

The crafting *mechanic* — imprint-making, sangue-cost-at-creation — is
the same on both sides. What changes is the substance the location
makes available and what happens to the imprint on second death.

**Wood-craft is gentle in tone.** The Vagrant builds from what fell
naturally or what was left behind. There is no violence required to
gather. A walking-staff from a deadfall branch. A sling from sinew of
a pilgrim's abandoned pack. The Wood does not bleed into the weapon.
Wood-imprint persists.

**Hell-craft is violent in tone.** Every Hell material was *taken*
from a soul being punished. The wrenched-from-Hell register reads
through the weapon's *visual*, its *flavor text*, its *sound* when
swung. The same dagger silhouette in Wood-substrate and Hell-substrate
feels different in the hand. Hell-imprint reclaims on death.

## Path implications

**Both paths craft.** Both paths grow as characters (per economy.md
*Path-specific economic shape*). The difference is in **what
direction sangue moves through the Vagrant** while he imprints:

- **Class-picker imprint**: sangue moves OUT of the held state INTO
  the blade-shape (Hell-side) OR into the lasting object (Wood-side).
  The substance the Vagrant is shaping is the substance Hell has
  already installed in him; crafting redirects some of it into
  gear. His body grows separately through commit installation.
- **Unburdened imprint**: same held-state → object motion (the Vagrant
  holds accumulated substance), BUT the substance
  flowing through the Vagrant during the imprint also contributes
  to **what's routing toward Beatrice's reservoir**.
  The act of imprinting is also a small riversamento — the
  unburdened's imprint-making is *the substance passing through him
  arrested briefly in tool-form before continuing onward*. Tools
  imprinted on this path have a quality the class-picker's don't:
  they carry the register of what is on its way to Beatrice. (TBD
  whether this is purely lore-flavor or has a mechanical surface.)

**Crafting is a critical progression vector for the unburdened.**
Per setting.md *The unburdened path* and classes.md *Stat schema
model*, the unburdened's stats stay locked at 1/1/1/1 baseline for
the entire run (he has no imprint-fire / no metabolic-fire to drive
installation). His mechanical strength in Hell comes from:

- **Crafted gear** — what he has imprinted into weapon-shape
- **Items / abilities** found in the world
- **Riversamento-unlocked capability** at Svuotato and Diaphanous
  (passives, combat-technique development; non-stat)
- **Skill**

Crafting carries a special weight on the unburdened path because
**every imprint is also a contribution to the riversamento toward
Beatrice**. The unburdened imprinting a blade IS a cosmological act
of devil-making toward Paradiso (per setting.md *Sangue saturation
of Beatrice*) in the same way every other riversamento is. The
class-picker imprinting a blade is a more ordinary act — substance
redirecting within his closed system.

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
  Masterwork`) — set per-instance at imprint-time. The same item
  template can produce instances of different quality based on the
  sangue allocated, the material quality of the substrate consumed,
  and the Vagrant's stat-state (LCK and possibly path-specific
  unlocks).
- **RecipeDef + RecipeRegistry** — recipes live at
  `config/recipes/<name>.json` with the schema
  `{name, inputs: [{item, quantity}], output, output_quantity}`.
- **Drop tables on enemies** — per-archetype `loot.drops` array of
  `{item, min, max, chance}` entries, rolled on death.
- **WeaponTierRegistry + evolution trees + compendium** — also
  available to inherit when needed.

This means crafting in Selva starts with a proven, tested data
model: enemies drop materials, sangue + materials are spent at the
imprint-making moment following recipe JSONs, recipes produce items
of a known rarity (intrinsic) and rolled quality (per-instance). The
cosmological re-skin (no-bench imprint-making, substrate split,
Hell-imprint reclamation, Wood-imprint persistence) layers on top.

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
  Vagrant's hand at the moment of imprinting (how much sangue he
  allocated; what his current stat-state supports). This is
  consistent with the *imprint-making* verb — the player is the
  agent of form, and the form he can produce depends on what he
  has become.

This split resolves cleanly with the path distinction: both paths
grow stats per setting.md / classes.md, and both paths' growth
contributes to craft-quality outcomes. The unburdened's
distinctive variance comes from the *Conduit / Threshold-state*
unlocks at Svuotato / Diaphanous, which scale weapon-imprint
quality in their own register (the substance passing through him
at imprint-time carries a distinctive signature). Specifics TBD at
gameplay tuning.

## Drops and gathering

Materials come from defeating enemies (drop tables, exactly as in
prison-escape) and from gathering at environmental nodes. The
principle: **what the Vagrant kills, he can reshape; what the world
offers, he can take.**

- **Kills.** Each archetype gets a `loot.drops` array in its
  archetype JSON (see `config/enemies/`). Shade kills yield shade-
  substrate; keeper kills yield keeper-tier materials; contrapasso
  husks yield their distinct residue. Drop-table specifics TBD.
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
NPC dialogue per `setting.md`, Hell-side ritual sites left by past
Vagrants, recipe-stones in the Wood). Recipe tree shape, branching,
unlock conditions all TBD at gameplay tuning.

## No workbench, no bench, no forge

**The verb is in the Vagrant's hand.** Imprint-making does not
require a workbench, an anvil, a forge, a ritual circle, or any
piece of equipment. Wherever the Vagrant is, with whatever substance
is at hand, with enough sangue in the wallet to cost the imprint —
he can make. No interaction site is required.

This is structurally important: the Vagrant is *himself* the verb
of imprint-making (see *Crafting is imprint-making*, above). Adding
a workbench would imply the verb is elsewhere — in the bench, in
the tools, in the ritual setup. It isn't. He is the cosmological
mechanism. The substance, the intention, and the cost are the only
inputs.

The UX expression of imprint-making (menu? hold-button?
contextual?) is TBD at implementation. The lore-frame is locked:
no bench.

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
along with his uncommitted vessel contents** (per economy.md
*Reclamation*), since both are substance Hell remembers and reclaims.

Unless — TBD — there is a category of weapon (Wood-substrate only?
Or items that have been *named* somehow?) that persists across
runs. This connects to the vestigia save-register (per fallback.md)
but specifics are open.

## Crafted weapons and the second-death framework

When the Vagrant dies (per the second-death system), all
**Hell-crafted weapons in inventory are reclaimed by Hell** alongside
his uncommitted vessel contents. They are substance; Hell takes its
substance back.

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
(markers, Erasure ritual, the Guide's relic)
slot into the ItemDef schema. Deferred until crafting moves from
design to engineering.

## Open questions

- **Recipe specifics.** Full recipe tree, material requirements,
  output stats, unlock conditions — all TBD at gameplay tuning.
- **Material drop tables.** Per-shade, per-keeper, per-contrapasso
  drop tables. TBD.
- **Imprint-making UX.** How the imprint moment is expressed in the
  hand — menu, hold-button, contextual visual, ritual mini-event.
  The Wood-side and Hell-side registers differ visually; whether
  the *UX* visibly differs between them is open.
- **Recipe-learn triggers.** Whether recipes drop from kills, are
  found at sites, are taught by NPCs, or some mix. Per setting.md
  NPCs provide a benefit on first encounter — recipe-learn is a
  candidate form. TBD.
- **Item categories.** Whether crafting produces only weapons +
  consumables, or also armor / tools / sangue-interacting items
  (markers — per inventory.md).
  These are Beat-4-given or unjudged-exemption-derived per current
  docs; whether crafted variants exist is open.

## Cross-references

- [Setting](setting.md) — *Sangue and the law of substance*,
  *Consuming Hell*, *Forced repentance through second death*, *The
  Vagrant / the unjudged*.
- [Economy](economy.md) — substance flow; Hell reclamation on
  second death; the unburdened path's no-installation cosmology.
- [Inventory](inventory.md) — item slots; commit verb, markers,
  Erasure ritual; weapon equip and use. Crafting outputs land in
  inventory.
- [Classes](classes.md) — stat progression via commit
  (class-picker only); the unburdened's stats are locked and
  crafting is their progression vector.
- [Fallback](fallback.md) — vestigia / persistent state across
  second deaths; Wood-craft persistence question lives here.
- [Bestiary](bestiary.md) — what each enemy archetype drops;
  per-archetype substrate flavor.

---

## Brainstorming notes (2026-05-28) — imprint-making frame

> Status: in-progress design conversation, NOT canon yet. Captured
> here so the thinking persists across sessions. Promote pieces to
> main canon body (and remove the workbench language) once locked.

### "Crafting" is not crafting — it is *imprint-making*

The workbench framing reads as industrial. Hell is not industrial.
"Craft" implies a place you go to; the Vagrant carries his agency,
he doesn't visit it. Reframe candidate:

**The Vagrant marks matter the way Hell marks souls.** Hell's
killing-protocol imprints sin onto a soul. The Vagrant — *the one
soul Hell cannot imprint* — can do the inverse: imprint matter with
his sangue and intent. Crafting is the inverse of being judged.

This composes cosmologically:
- The Vagrant's defining trait (per setting.md *The unjudged*) is
  that Hell's protocol cannot grip him because he carries no
  imprint to complete. He is the un-marked one. Reframing crafting
  as imprint-*making* makes the verb structurally parallel to the
  cosmological mechanic that defines him.
- It eliminates the workbench. The verb is in the Vagrant's hand,
  wherever he is. Substrate availability determines what he can make;
  no station-based interaction.
- It explains the cosmological flow: sangue + substrate + intent →
  imprinted object. Substance moves from the Vagrant's wallet into
  the matter. The matter becomes a *small fragment of him* — the
  unjudged-exemption stamped onto raw substrate.

### Sangue flows into everything he makes

Already implied by the "Hell reclaims its substance on second death"
rule in the main body. Making it explicit: **the player spends
sangue at imprint-making time**, and that sangue is *in* the weapon
from then on. Implications:

- **Quality is the player's sangue allocation choice.** Spend more
  sangue, higher quality imprint. Crafting becomes a real
  competitor with installation (commit) for the sangue budget.
- **Hell-imprinted weapons reclaim on death** (matches existing
  rule). They're substance Hell remembers; on second death, both
  the uncommitted vessel contents and the sangue-in-weapons return
  to Hell.
- **Wood-imprinted weapons persist** (resolves the open question in
  the main body about Wood-craft survival). The Wood is outside
  Hell's accounting; sangue spent on Wood-substrate imprints is
  cosmologically rerouted, not reclaimed.
- **Path register through the same mechanic:** class-picker sangue
  is Hell-tainted; their imprints read dark, sangue-tainted, hot.
  Unburdened sangue is pure; their imprints read pale, light-bound,
  almost luminous. Same craft verb, different output flavor.

### Imprint-making as the verb the cosmology requires

`crafting.md` line load-bearing for this:

> *"To craft a weapon in Hell is to take — to flay a shade for its
> sinew, to break and shape its bone, to bind a damned soul's
> substance into an edge."*

That's already imprint-shaped. The Vagrant *binds*, *shapes*,
*marks*. The substrate is given by the situation; the act is in
his hands. We just promote what's implicit into explicit doctrine
and remove the bench from the picture.

### TBD before promotion to canon

- Italian register name for the verb. *Impronta* (imprint),
  *segnare* (to mark), *legare* (to bind) all candidates. Italian
  grammar / register check needed.
- Whether basic items (torch, wrap-cloth, walking-staff) require
  the imprint verb at all, or are just *picked up* + held. Probably:
  pickups exist; imprint-making is for weapons + sangue-bearing
  objects. The torch as MVP is a pickup, not an imprint.
- UX shape: hold-to-imprint with HP/sangue tick? Menu select? Quick
  gesture? Defer to UX pass.
