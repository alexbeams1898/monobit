# Prison Break Game — Design Document

> Living document. Nothing is fully locked in unless explicitly stated.
> Updated by Claude as design decisions are made.

---

## Narrative

### The Setup
Bud is a corrupt cop and the player's drinking buddy. He guides a rotating cast of inmates out
of the prison one run at a time via walkie talkie. The player character keeps waking up in their
cell — the reason is intentionally unexplained early on and slowly revealed through progression.
Bud exists in some kind of alternate layer that is hinted at but not yet defined.

### Characters
- **Bud** — corrupt cop, drinking buddy, meta NPC. Voice on the walkie talkie. May appear in
  the meta store / between-run screens. Not playable. TBD on full role.
- **Playable characters** — inmates. Custom character created at the start of a fresh save.

### Tone
Comedic throughout. Body-gore humor — absurd rather than disturbing. Should never feel too
extreme or mean-spirited. Think Monty Python meets early Doom.

---

## Core Gameplay Loop

1. Spawn in prison map
2. Kill enemies → earn XP → level up → allocate stat points
3. Collect material drops from enemies and environment
4. Field-craft a base weapon from materials (no station needed)
5. Fight through escalating enemy waves separated by safe rooms
6. Find upgrade stations in the map → upgrade weapons using materials
7. Defeat the final boss/elite enemy → escape → run complete
8. Carry money and meta progress forward

### Run End Conditions
- **Win** — defeat the final boss/elite enemy. The exit is earned, not found.
- **Loss** — death. Back to main menu. No respawn. (Souls-style hybrid worth revisiting later.)

---

## Character Creation

Happens once at the start of a fresh save. Should take under two minutes.

- **Name** — purely flavor
- **Stat allocation** — fixed pool distributed across STR, DEX, END, LCK before the first run.
  This is the primary build decision.
- **Starting perk** — chosen from a list. Sets tonal build identity.
- **Appearance** — locked after creation. A meta store item allows redesign.
- **Starting armor** — the clothes on your character's back. No special gear. Everything else
  is found in runs.

---

## Stats

| Stat | Function |
|------|----------|
| STR | Attack power. Scales with heavy/two-handed weapons. |
| DEX | Attack speed. Scales with light/one-handed weapons. |
| END | Max health. |
| LCK | Rare drop rate + ranged weapon accuracy. Offsets the rarity curve on most-used weapon drops. |
| DEF | **Derived — not leveled.** Calculated from STR + END + overall level + equipped armor. Works like Elden Ring's defense system. |
| Poise | **Derived — not leveled.** Calculated from equipped armor weight and tier. Determines knockback resistance and stagger threshold. High poise = barely flinch; low/no armor = gets bowled over. Mechanically simulated "mass." |

---

## Weapons

### Core Rules
- All weapons are crafted — no exceptions (edge cases TBD, e.g. story moments)
- Fists are always available. Not inherently weak — can be a strong build with right stats/upgrades.
- Fist-only run = viable challenge / achievement
- Enemies never drop whole weapons — enemy weapons break on death, dropping parts and materials only
- Not all recipes are known from the start — discovered during runs, permanently unlocked via
  meta progression

### Weapon Slots
- **Dual one-handed** — two weapons attacking independently (VS-style chaos feel)
- **One two-handed** — single weapon, bonus STR scaling, hits harder
- Two-handed favors STR builds; dual one-handers favor DEX builds

### Crafting Tiers
- **Field crafting** — combine world-drop materials using a basic starting tool. No station needed.
- **Upgrade stations** — scattered randomly in the map. Used for all upgrades beyond base.

### Weapon Evolution Tree
- Multiple materials can produce the same base weapon type (e.g. keys → shiv, toothbrush → shiv)
- Source materials influence starting stats via component data — same weapon type, different stat
  bias depending on crafting path
- Falls out naturally from ECS component data — no special rules needed
- Early enemy drops feed directly into stage 1 crafting. Higher rank = better materials up the tree.

### Stat Requirements
- Must meet stat requirements to equip a base weapon (souls-style)
- Upgrade path inherits base requirements — no new stat gates per upgrade

---

## Armor & Drip

### Functional Armor
- Dropped by enemies as materials/parts — never as a complete item
- Crafted or assembled like weapons
- Base: boosts DEF (derived stat)
- Rarer pieces add secondary stat bonuses
- Full souls-style mix-and-match build variety is the long-term goal — start simple

### Drip System (Cosmetics)
- Purely cosmetic layer on top of functional armor
- Applies to both clothing/armor and weapons
- Unlockable through meta progression, achievements, and modding
- References real clothing styles, brands, aesthetics — highly scalable
- Custom drip is a first-class modding feature
- Long-term engagement hook — players eventually start runs fully kitted, focused on obliterating

---

## Crafting & Drops

- **Enemy drops** — basic materials (common), weapon/armor parts (uncommon/rare)
- **Chests** — rare+ crafting materials, large money sums, occasionally something special
- **Boss/elite drops** — specific meaningful items tied to progression. Separate drop table.
- The more a weapon is used, the rarer the materials needed to progress it become
- LCK directly offsets this rarity curve
- **Adaptive seeding** — engine tracks most-used weapon and nudges rare drops toward completing
  that upgrade path. Player feels lucky; the game is being fair.
- **Chest frequency is critical to balance** — too many breaks the economy, too few feels dry.
  Revisit during development.
- **This system needs further design work**

---

## Terminology

| Term | Definition |
|------|------------|
| **Perk** | A character trait assigned at creation. Can be buff only, debuff only, or both. |
| **Buff** | A positive gameplay modifier. |
| **Debuff** | A negative gameplay modifier. Can be applied by enemies mid-run. |
| **Drip** | Purely cosmetic customization. Never affects gameplay. |
| **Status Effect** | A temporary condition applied to the player or enemy (e.g. leech debuff). |

---

## Status Effects

- Certain enemies apply debuffs that attach to the player on contact or loot pickup
- **Leech debuffs** are a key mechanic in later levels — punishes greedy looting behavior
- Status effect system is schema-based — modders can define custom effects

---

## Enemy Design

### Philosophy
- Start small, expand via schema — every enemy is a config entry
- Rank hierarchy maps directly to wave progression
- Appearance and drops become progressively stranger and more off-putting
- Final boss of each map = fully unhinged, clearly supernatural/demonic
- Gore is comedic — absurd rather than disturbing
- Gore animation and sound design must scale — design for simultaneous events from day one

### Rank Hierarchy (first map)
1. Correctional Officer — basic grunt, minimal gear
2. Senior Officer — slightly more experienced
3. Sergeant — first supervisory rank
4. Lieutenant — mid-level, better drops
5. Captain — commands others, serious gear, firearm parts
6. Warden — final boss/elite. Fully unhinged. Specifics TBD.

### Equipment & Drop Mapping
- Basic officers: radio components, keys, baton pieces → stage 1 weapon materials
- Riot/tactical officers: helmet fragments, shield pieces, vest parts
- Higher ranks: firearm parts, rare materials → higher upgrade tier recipes
- All drops are materials/parts — never whole weapons or armor

### Wave Structure
- Wave-based with brief safe rooms between waves
- Safe rooms contain: weapon upgrade station, healing. Kept simple.
- Always an objective — waves should never feel aimless
- Final boss/elite kill = run complete = escape

---

## Map & Procedural Generation

### Map Feel
- Open roaming space with prison visual theming — not a realistic simulation
- No key/door/room gating — movement is free like VS
- Prison aesthetic is dressing, not a mechanical system
- Tone: silly, low fidelity, practical. Funny where possible.

### Procedural Generation Architecture
**Core principle: separate structure (owned by engine) from visuals (supplied by modders).**
The engine enforces rules — how spaces connect, wall placement, room flow. Modders supply
sprites mapped to tile types.

#### Key Patterns
- **Tile-based with constraints** — grid of predefined tile types with engine-enforced connection rules
- **Room templates / prefabs** — pre-authored chunks stitched together procedurally. Each template
  is a tile-type grid with metadata tags.
- **BSP (Binary Space Partitioning)** — strong candidate for base generation algorithm.

#### Modder Asset Schema
Modders supply:
- Sprites for each tile type
- Room templates as tile-type grids
- Metadata tags per room (combat area, loot area, boss arena, upgrade station)
- Rank hierarchy enemy subset for their map
- Engine fills any missing assets with base game defaults

#### Engine Responsibility
- Wave composition, mob density, and difficulty scaling handled algorithmically
- Modders define content — engine handles balance
- **Procedural seeding is the most critical engine focus** — crafting recipe completion must
  always be achievable without feeling predetermined

---

## Meta Progression

### Currency
Money (bills — $1, $5, $20, $50, $100) earned during runs. Carries over on death.

### Meta Store
Bud may serve as the meta store NPC — the one you interact with between runs. TBD on execution.

Purchases include:
- Character unlocks
- Cosmetics / drip
- Crafting materials (softens bad runs)
- QOL improvements
- Character appearance redesign token

### QOL: Portable Weapon Upgrader
- Allows weapon upgrading without finding a station in the map
- Ranked system: rank 1 handles tiers 1-3, rank 2 handles tiers 3-6, etc.
- Coexists with map upgrade stations — does not replace them
- Highest tier upgrades always require a map station — keeps late-run exploration meaningful

### Rare In-Map Discoveries
- Extremely rare items found during runs that permanently affect meta progression
- Example: a crafting guide that permanently unlocks a recipe for future runs
- Creates tension between rushing to the exit and exploring the map
