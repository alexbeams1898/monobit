# Inventory

> **Owns:** what the player carries — categories, capacity rules,
> the Wood-side chest, and the world-container loot model.
> **Status:** structural locks; per-screen UX TBD.

## Storage model

Inventory is a **category-bucketed bag with infinite total capacity
and per-item-stack caps** — the Elden Ring shape, not the original
Selva "small named-slot system." Tabs in the UI map to item
categories; the player switches between tabs to see weapons, armor,
consumables, materials, key items, accessories, and incants
separately.

- **No global slot count.** The player can carry any number of
  distinct item types.
- **Per-item carry cap.** Each `ItemDef` declares a `max_carry`
  value (default 99 for stackables, 99 for instanced). When adding
  an item would exceed `max_carry`, the overflow auto-routes to the
  Wood-side chest (see *The chest*, below). No "inventory full"
  refusal — the substance always lands somewhere the Vagrant can
  retrieve it.
- **Stackables merge.** Materials, consumables, ammo, and any other
  stackable categories combine into single entries with a `count`
  field. Instanced items (weapons, armor, accessories) take one
  inventory entry per copy with independent quality / durability /
  weapon XP.

## Tabs

The inventory UI presents one tab per category. Categories are
declared in the engine's `ItemCategory` enum and inherited from
prison-escape-game's working model (per crafting.md *System reuse*):

| Tab | Category | Storage shape |
|---|---|---|
| Weapons | `Weapon` | instanced |
| Armor | `Armor` | instanced |
| Consumables | `Consumable` | stackable |
| Materials | `Material` | stackable |
| Key Items | `KeyItem` | instanced |
| Accessories | `Accessory` | instanced |
| Incants | `Incant` (new) | instanced |

The `Money` category from prison-escape is **not** ported — Selva
has no money. Sangue is the only economy and lives on the HUD
counter, outside inventory (per economy.md *Currency: sangue*).

The `Incant` category is **new for Selva**. Incants are abilities
the Unburdened uses as a substitute for weapons, and that
class-pickers may learn but scale poorly with (per *Path
implications*, below). Incants live in inventory with item-shape
data (name, description, icon, rarity) but equip into ability slots,
not weapon slots. The full incant system is designed in a separate
doc when it ships; this category exists from day one so future-incant
data has a home.

## Equipment

The `Equipment` component on the Vagrant holds **indices** into the
`Inventory.by_category` map — equipping an item never moves it out
of inventory.

- `right_hand`, `left_hand` — weapon slots
- `head`, `chest`, `legs`, `feet` — armor slots
- `accessory_1`, `accessory_2` — accessory slots
- `incant_1`, `incant_2`, ... — incant slots (slot count TBD)
- `unarmed` — **always-valid index** to a persistent `unarmed`
  weapon instance every character carries from game-start.

### Unarmed is a weapon

Every character starts with one `unarmed` `ItemInstance` in their
Weapons tab, equipped to `Equipment.unarmed`. It cannot be unequipped
or dropped. It has stats (low damage, fast speed, 1/1-stat-reqs),
gains weapon XP per kill, has its own evolution tree (see
weapon-evolution.md *Unarmed evolution*), and is the fallback combat
verb when the equipped weapon slot is empty.

Class differentiation rides on scaling: Feral's class passive buffs
unarmed damage substantially; Penitent / Heretic scale unarmed
weakly; Unburdened scales unarmed not at all. **The item is the
same; the scaling differs by class.** This is the cosmologically
clean way to express "the Feral's body IS his weapon" without
fragmenting the data model.

### Stat-gated equip

Each weapon `ItemDef` declares `str_requirement` / `dex_requirement`
/ `end_requirement`. A weapon below the wielder's stats equips
normally; above them, it equips but takes a damage penalty (Souls
convention — TBD whether Selva softens or hardens this). The
Unburdened's 1/1/1/1 locked stats prevent equipping almost every
weapon in the game; this is intended, not a bug. See *Path
implications* for the cosmological reading.

## The chest

The chest is a **Wood-side storage object** in or immediately
adjacent to the chapel. Cosmologically mundane (no Hell-grip, no
installation behavior, accessible to any soul including the
Unburdened) but aesthetically loaded (13th–15th-century Italianate
gothic, cathedral-reliquary register — same Beatrice-built
infrastructure that produced the chapel exterior).

- **Player-managed storage.** Put items in, pull items out, any
  visit to the chapel.
- **Both paths.** Class-picker and Unburdened both use the same
  chest.
- **Persistent across cycles.** The Wood remembers (per wood.md
  *The Wood remembers*). Items in the chest survive second death.
- **Overflow target.** When inventory `max_carry` is exceeded, the
  overflow item auto-routes to the chest. The player is notified
  ("an item was sent to the chest") but does not need to manage it
  manually mid-run.
- **No save effect.** Vestigia handle saves; the chest is pure
  storage, not a save point.

Specifics — exact placement, capacity per item, whether there are
multiple chests — TBD at implementation.

## World containers

**The third loot source, distinct from enemy drops and the
Wood-side chest.** Hand-placed objects in the world the player can
loot once. Cosmologically: residue from prior Vagrants whose Hell-
imprints partially survived the broken reclamation protocol, and
Wood-craft objects left by fallen pilgrims and Wood-travelers that
persist forever because Wood is outside Hell's accounting.

Variants:

- **Pilgrim packs** — Wood-side. Abandoned by fallen pilgrims.
  Contain Wood-craft items. Humble register.
- **Reliquaries** — Wood-side, gothic. Larger. May contain
  higher-tier Wood-craft or pilgrim-blessed items. Chapel register.
- **Corpses** — both sides. A fallen pilgrim's or prior Vagrant's
  body carries what they had when they died. Souls staple.
  Cosmologically: their stuff didn't dissolve because the
  reclamation protocol that should have taken it failed.
- **Ossuaries / pyres** — Hell-side. Heaps where Hell tried to
  compress accumulated residue but failed. Contain prior-Vagrant
  Hell-craft fragments. Visually grotesque.
- **Niches / shrines** — both sides. Small placed items, often a
  single notable object. Reads as ritualistic placement.

### Mechanical model

- Each world container is an entity in the region JSON with a
  `loot_table` (same data shape as enemy `loot.drops` but
  **hand-authored, not rolled per-cycle**).
- Interact (E) opens a small UI showing the container's contents
  → player takes some or all → container becomes empty.
- **Emptied state is per-character-persistent.** Once a container
  is looted, the save records it as empty; subsequent visits show
  it empty until a new cycle / character.
- World containers may hold any item category: weapons, armor,
  consumables, materials, key items, accessories, incants.

### The starting weapon is in a world container

The Vagrant's first weapon is **found on the descent stairs**, in
a hand-placed world container (pilgrim pack or fallen-Vagrant
corpse — register TBD). It is a tier-0, 1/1-requirement, Wood-
substrate item — humble, what someone before him left when they
fell. The player crosses Acheron with fists; descends; finds the
container; picks up the weapon; suddenly has a verb in hand.

**This replaces the earlier "starter kit at the Signing" framing
for weapons.** The Signing remains the commit-verb unlock (Crucible
appears as cosmological capacity) but it does NOT gift a starting
weapon. The weapon is a *world event*, not a *menu event*. Class-
pickers still receive a *starter ability kit* at the Signing (per
classes.md *Class differentiation rides on top, three layers
deep*) — that's distinct from a starter weapon.

## Loot sources, summary

The full pipeline of how items enter the Vagrant's inventory:

1. **Enemy drops** — materials + sangue + rare flavor items.
   **Never weapons.** Per crafting.md *Core rule*: the damned do
   not bear arms. Specific drop tables in archetype JSON per
   bestiary.md.
2. **World containers** — hand-placed; any item category. Most
   weapons in the game enter inventory through these.
3. **Crafting (imprint-making)** — sangue + materials → new item.
   Player creates weapons / consumables / etc. from the substance
   he extracted from kills. Designed in crafting.md.
4. **NPC rewards** — first-encounter benefit per NPC (item /
   sangue / lore / unlock). Per setting.md *NPCs*. Per-NPC reward
   design TBD.

## Path implications

| Item / Capacity | Class-picker | Unburdened |
|---|---|---|
| Grimoire (carried) | yes | yes |
| Signing (Guide ritual, Beat 4) | accepted | refused |
| Crucible (commit-verb capacity) | yes | — |
| Censer (commit-verb capacity) | — | yes |
| Markers (carried) | yes | yes |
| Erasure (Guide ritual, materials-gated) | yes | yes |
| Chest (Wood-side storage) | yes | yes |
| World containers (loot) | yes | yes |
| Found weapons (wieldable) | yes (stat-gated) | almost never (1/1/1/1 locked) |
| Crafted weapons (wieldable) | yes (stat-gated) | almost never |
| Unarmed weapon | yes | yes |
| Incants | yes (learn, scale weakly) | yes (primary combat verb at L2+) |

**Unburdened combat is almost entirely incants.** The 1/1/1/1
stat-lock means most weapons fail their stat-requirements. He can
wield tier-0 1/1-req tools (the descent-stair starting weapon
included), but anything higher is locked out. His distinctive
combat verb — Stillness / interruption-of-Hell's-mechanism per
economy.md — emerges through Svuotato / Diaphanous riversamento
gates and expresses as incants, not melee. **This is cosmologically
correct, not a balance accident**: the unmade body has nothing to
swing with.

## Markers

> Status: locked 2026-06-06 per [[project_markers_replace_cord]].
> Bidirectional Wood ↔ marker. Available to both paths.

(Markers section preserved verbatim from prior canon — see the
original inventory.md for the full Markers brainstorming notes.
Summarized here:)

- Player-placed teleport destinations, one active per circle.
- Per-use sangue cost at placement and at transit.
- Cosmological justification: the Vagrant cannot be marked, so he
  can mark. Leaves a piece of his unjudged-exemption in the
  geography — creates a zone Hell cannot see.
- Wood ↔ marker bidirectional only; no marker → marker.
- No save effect; no healing at marker; not a rest site.

## NPC rewards

Per setting.md *NPCs*: each NPC provides a benefit on first
encounter (item, sangue, lore fragment, permanent unlock). NPC
roster, item specifics, and per-NPC reward design TBD.

## Open questions

- **Carry-cap defaults per category.** Whether weapons / armor /
  key items have lower caps than materials.
- **Chest capacity.** Whether the chest also has per-item caps
  (probably no — it's the overflow destination).
- **Stat-requirement under-spec penalty.** Hard refuse, soft damage
  penalty, or stamina cost. TBD at combat tuning.
- **Incant slot count.** How many incants can be equipped at once.
  TBD with the incant system design.
- **Inventory screen UX.** Tab navigation, sort modes, comparison
  display, quick-use shortcuts. Defer to UX pass.

## Cross-references

- [Setting](setting.md) — *Item system*, *Markers*, *Vestigia*,
  *Sangue and the law of substance*.
- [Crafting](crafting.md) — imprint-making produces items that
  land here.
- [Weapon XP](weapon-xp.md) — per-instance progression on weapons
  (including unarmed).
- [Weapon evolution](weapon-evolution.md) — branching DAG of
  weapon transformations.
- [Classes](classes.md) — class differentiation via stat scaling
  + class passives + starter ability kit.
- [Economy](economy.md) — sangue cost of markers; no other
  economy in inventory.
- [Story](story.md) — *The Signing* (Beat 4 via class-picker UI;
  unlocks commit-verb capacity, NOT a starter weapon).
