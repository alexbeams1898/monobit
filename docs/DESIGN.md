# Hell Escape — Design Document

> Living document. Nothing is fully locked in unless explicitly stated.
> Updated by Claude as design decisions are made.

---

## Narrative

### The Setup
You wake up somewhere unfamiliar. The world looks almost normal — a little off, maybe, but
nothing you can't explain away. You fight your way toward the exit. If you make it, you escape
and carry your progress forward. If you die, it's over — back to the meta world to try again.
Each run the world gets stranger. The architecture shifts. The enemies stop looking quite human.
The person guiding you through a radio starts saying things that don't quite add up.

You are in Hell. You just don't know it yet.

The reason you keep ending up back here — and why everything is slowly getting more hellish —
is intentionally unexplained early on and revealed through progression.

### The Twist (Marketing Hook)
The game is marketed as a real-world escape dungeon roguelike, not an escape from Hell.
The hell setting is the reveal — discovered through play, not the store page. The world
starts grounded and mundane. The uncanny creeps in gradually: strange geometry, enemies
that are almost-human but not quite, visual corruption at the edges, Bud saying things
a normal person wouldn't say. By the time it's undeniable, you're already deep in.

**Never spoil the twist in marketing material, trailers, or the title screen.**

### Characters
- **Bud** — guide, drinking buddy, meta NPC. Voice during runs. Appears between runs.
  Not playable. **Bud is a demon.** This is not revealed until late in the game. His humor
  and helpfulness mask his true nature. As the player progresses through the layers, his
  dialogue, behavior, and appearance grow increasingly uncanny and demonic. The gradual
  reveal is a key narrative arc — never telegraph it early.
- **Playable characters** — the damned. Custom character created at the start of a fresh save.

### Tone
Comedic throughout. Body-gore humor — absurd rather than disturbing. Should never feel too
extreme or mean-spirited. Hell is a fun place to be. Think Monty Python meets early Doom.

### Inspirations & Literary Influences
- **Dante's Inferno / Divine Comedy** — layers of Hell structure, escalating strangeness,
  the idea of a guide who may not be fully trustworthy. The 9 circles of Hell are a candidate
  structure for zone progression. Dante's self-reference as "the pilgrim" in the poem.
- **Paradise Lost (Milton)** — the grandeur and tragedy of fallen beings, Hell as a place
  with its own politics and hierarchy
- **No Exit (Sartre)** — "Hell is other people." Ironic for an escape game. The philosophical
  dimension of inescapable punishment.
- **Ars Goetia** — the 72 demons as a reference catalogue for enemy/boss design, naming,
  and hierarchy
- **Doom** — escape through layers of Hell, escalating demonic hostility, comedic brutality,
  episode progression from mundane to pure hell
- **Vampire Survivors** — core loop, enemy escalation, auto-attack feel
- **Dark Souls / Elden Ring** — stats, build variety, weapon scaling, crafting depth, lock-on
- **Hades** — twin-stick movement model, top-down action feel

### Design Principles from Source Material
- **Contrapasso** (Divine Comedy) — punishment mirrors sin. Used as a design principle for
  enemy behavior per zone. Each zone's enemies should have mechanically distinct behavior that
  reflects the zone's thematic sin. Not just flavor — it forces diverse enemy design naturally.

### Title Candidates
Under consideration — no final decision yet.
- **"The Pilgrim"** — Dante's self-reference in the poem. Sounds like an adventure game, has
  thematic depth, doesn't spoil the hell twist.
- **"No Exit"** — Sartre. Ironic for an escape game. No existing game with that name.

---

## Core Gameplay Loop

1. Spawn in the map — the world looks mundane at first
2. Kill enemies → earn XP → level up → **VS-style popup fires mid-run** → pick a stat (STR/DEX/END/LCK) → back to fighting immediately
3. Collect material drops from enemies and environment
4. Field-craft a base weapon from materials (no station needed)
5. Survive timed enemy waves — enemies spawn based on your position and how long you've been in the area; the map is open, explore freely between waves for chests, rest spots, and loot
6. Find rest spots scattered in the map → heal, upgrade weapons, buy items
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
| DEX | Attack speed + movement speed. Scales with light/one-handed weapons. |
| END | Max health. |
| LCK | Rare drop rate + ranged weapon accuracy. Offsets the rarity curve on most-used weapon drops. |
| DEF | **Derived — not leveled.** Calculated from STR + END + overall level + equipped armor. Works like Elden Ring's defense system. |
| Poise | **Derived — not leveled.** Calculated from equipped armor weight and tier. Determines knockback resistance and stagger threshold. High poise = barely flinch; low/no armor = gets bowled over. Mechanically simulated "mass." |

---

## Stat Formulas

Log curve applied to all stats — high early gains, soft diminishing returns at scale.
All constants live in `config/balance/formulas.json` — that file is the source of truth.
Never hardcode formula constants in engine code.

```
maxHP          = baseHP + floor(hpScale * log(END + 1))
moveSpeed      = baseMoveSpeed * (1 + floor(speedScale * log(DEX + 1)) / 100)
carryWeight    = floor(strCarryScale * log(STR + 1)) + floor(endCarryScale * log(END + 1))
dropMultiplier = 1 + floor(lckScale * log(LCK + 1)) / 100
```

Swing cooldown is weapon-physics based — see Combat System section.

### DEF Derivation
```
DEF = floor((STR * strDefScale) + (END * endDefScale) + (level * levelDefScale) + armorValue)
finalDamageTaken = max(1, incomingDamage * (1 - min(DEF, defCap) / 100))
```
Soft cap: 75% — player can never be fully invincible.

### Damage (weapon grade scaling)
```
finalDamage = baseDamage + floor(str * strGradeMult) + floor(dex * dexGradeMult)
```
Both stats always contribute. Grades control the per-point weight, not which stat is used.
Grade multipliers: S=1.5x · A=1.25x · B=1.0x · C=0.75x · D=0.5x · E=0.25x

Grade letters are cosmetic buckets. The underlying value is a float; the letter is what the player
sees. When weapon upgrades are added, reinforcing a weapon increases the underlying scaling value,
which naturally bumps the displayed grade — no special rules needed.

### XP Curve
```
xpToNextLevel = xpBase * (level ^ xpExponent)
```

### `formulas.json` shape
```json
{
  "hp": { "base": 50, "scale": 100 },
  "movement": { "base": 150, "dex_scale": 30 },
  "carry_weight": { "str_scale": 20, "end_scale": 10 },
  "defense": { "str_scale": 0.3, "end_scale": 0.5, "level_scale": 0.2, "cap": 75 },
  "luck": { "drop_scale": 15 },
  "damage": {
    "grade_multipliers": { "S": 1.5, "A": 1.25, "B": 1.0, "C": 0.75, "D": 0.5, "E": 0.25 }
  },
  "swing": { "weight_scale": 100, "stat_scale": 40, "two_handed_str_bonus": 0.3 },
  "stat_requirement": { "penalty_rate": 0.15 },
  "leveling": { "xp_base": 100, "xp_exponent": 1.5, "points_per_level": 1 }
}
```

---

## Combat System

### Attack Modes
**Manual mode is the default game.** Auto mode is a meta-store unlock — see Meta Progression.

**Manual mode** — Souls-inspired, skill ceiling
- Directional attacks (Crystalis / Symphony of the Night / Tales of Mana feel)
- Controls work like Elden Ring, simplified for top-down 2D
- Attack commitment: each swing locks you into its animation — no cancelling into dodge or another attack until it resolves
- Dodge roll with i-frames; flat per-roll cooldown (no stamina bar)
- Shield block active; parry window on timed input — negates damage and staggers attacker
- Special attacks (weapon skills) triggered via dedicated input — see Weapon Skills

**Auto mode** — VS-inspired, unlocked via meta-store
- Weapons auto-fire on cooldown with smart targeting (nearest/most dangerous enemy)
- Player still controls movement manually — skill expression is positioning, not attack timing
- No attack commitment — weapons fire independently on their own timers
- Shield auto-blocks attacks within the player's frontal arc (±90° from facing direction, determined by movement input). Side and back hits bypass the shield — positioning still matters.
- Special attacks require manual input in auto mode (unless Auto-Parry upgrade is purchased — see Meta Progression)

Both modes use the same swing cooldown formula. Auto fires when the timer expires; manual fires on player input if the timer has elapsed.

### Swing Cooldown (Physics Formula)
Cooldown is driven by the weapon's weight and a stat blend determined by its scaling profile.
The weapon's `dex_scaling` grade maps to a `dexBias` float (S → ~1.0, E → ~0.0); `strBias = 1 - dexBias`.

```
effectiveStat = (STR * strBias) + (DEX * dexBias)
swingCooldown = (weaponWeight * weightScale)
              / (1 + floor(effectiveStat * statScale * log(effectiveStat + 1)) / 100)
```

Two-handed: STR contribution amplified by `two_handed_str_bonus` (shifts dexBias toward 0).
All constants in `formulas.json` under `"swing"`.

**Emergent behavior:**
- Light DEX weapon (shiv) + high DEX → blazing fast
- Light DEX weapon + high STR / low DEX → decent speed, but clearly suboptimal
- Heavy STR weapon (mace) + high STR → surprisingly snappy for the weight
- Heavy STR weapon + high DEX → painfully slow — fighting the physics
- No hard gates: wrong-stat builds feel clunky, not broken

### No Stamina Bar
Stamina replaced by per-system mechanics:
- **Attacks** — limited by animation lock (commitment)
- **Dodge rolls** — flat cooldown per roll
- **Shields** — guard break: absorbing too many consecutive blocked hits staggers the player. Punish moment, not gradual drain.

### Weapon Skills (Ashes of War equivalent)
Every weapon can have a special attack — a unique skill attached to it, exactly like Elden Ring's Ashes of War. Triggered via a dedicated input (L2/LT on controller, separate key on keyboard).

- Each weapon defines its own skill in config — a spinning attack, a dash strike, a ground slam, etc.
- Skill has its own cooldown, tuned per skill, longer than the regular swing cooldown. Both cooldowns are visible in the UI separately.
- No FP or resource cost — just the cooldown timer. No extra bars.
- Skills **evolve alongside weapon upgrades** — the same upgrade path that improves base stats also changes or enhances the skill (e.g., a basic lunge becomes a piercing lunge that hits through enemies at tier 3)
- In **auto mode**: skills require manual input — never auto-fired
- In **auto mode with Auto-Parry upgrade**: parry specifically can auto-trigger (see Meta Progression)

### Stat Requirements (Soft Penalty)
Weapons and armor have stat requirements. Below threshold applies an exponential penalty to damage and swing speed:

```
deficit       = max(0, requirement - stat)
penaltyFactor = exp(-deficit * penaltyRate)
```

`penaltyRate` in `formulas.json` under `"stat_requirement"`. Steep enough curve that heavily under-spec'd weapons are effectively unusable; "barely below" is workable in a pinch. Creates a satisfying "build coming online" moment when the threshold is hit.

### Ranged Weapons (Guns)
**Deferred — design intent only.**

Guns will not use the melee swing formula. Attack power is attributed to the gun itself (barrel, bullet type, mechanism — all crafted). Guns still have `weight` for carry weight purposes.

Planned stat mapping:
- **DEX** → accuracy + fire rate (steady hands, quick trigger)
- **LCK** → spread behavior, crit chance
- **STR** → no contribution (strength doesn't move bullets faster)

This creates a distinct build identity: gun builds are DEX/LCK, melee builds are STR/DEX.
Will be considered for implementation within this milestone once melee is built and feeling good.

### Sprite Direction
**4-directional with dominant-axis selection.** Left/right flip only is ruled out — incompatible
with directional parry/poise combat.

Direction determined by dominant axis of the facing vector. Ties go vertical (South).
Uses `FacingDirection.render_dx/dy` (smoothed) for visual direction selection.

### Split-Body Rendering (Player)
Player rendered as two overlapping sprite layers: lower body (legs) faces movement direction,
upper body (torso/head) faces aim direction (mouse). Each body part is a child entity with
independent Animation and Sprite components.

| Player Action | Lower Body | Upper Body |
|---|---|---|
| Standing idle | Idle, last velocity dir | Idle, aim dir |
| Walking | Walk, velocity dir | Idle, aim dir |
| Walking + attacking | Walk, velocity dir | Attack, aim dir |
| Standing + attacking | Idle, last dir | Attack, aim dir |
| Hit | Hit | Hit |
| Death | Death | Death |

**LPC asset limitation (accepted):** LPC sprites have no torso-twist frames. The vertical
split at y=35 (within each 64x64 frame) means only head and slight shoulders visually rotate.
Full torso rotation requires custom art — planned for future, likely alongside character
creation system.

**Enemies** use single-sprite animation (no split-body). Skeleton sprites sourced from the
LPC skeleton universal sheet.

---

## Weapons

### Core Rules
- All weapons are crafted — no exceptions (edge cases TBD, e.g. story moments)
- Fists are always available. Not inherently weak — can be a strong build with right stats/upgrades.
- Fist-only run = viable challenge / achievement
- Enemies never drop whole weapons — enemy weapons break on death, dropping parts and materials only
- Not all recipes are known from the start — discovered during runs, permanently unlocked via
  meta progression

### Weapon Config Fields
Every weapon definition includes:
- `weight` — float. Drives swing cooldown physics and carry weight. Light weapons (shiv ~0.5) swing fast; heavy weapons (mace ~3.0, two-handed sword ~5.0) swing slow but hit hard.
- `str_scaling` / `dex_scaling` — grade (S/A/B/C/D/E). Drives both damage bonus and swing speed bias. Both always contribute additively — grade determines weight, not which stat wins.
- `str_requirement` / `dex_requirement` — stat floor for full effectiveness. Below threshold applies exponential penalty (see Combat System).

### Weapon Scaling Design Intent
Every weapon should have a logical real-world reason for its scaling split. Think through each
weapon: what physical qualities does it reward? A heavy weapon rewards raw power (STR). A fast
precise weapon rewards control and timing (DEX). Most weapons reward both to varying degrees.

Design these thoughtfully per weapon — don't assign grades arbitrarily. The grade split is part
of the weapon's identity and affects which builds it supports.

**Confirmed weapons:**

| Weapon | str_scaling | dex_scaling | Notes |
|--------|-------------|-------------|-------|
| Fists  | B           | C           | Power is the engine; DEX tightens timing and speed |

_More to be added as weapons are designed._

### Ranged Weapons
Guns and thrown projectiles follow different rules than melee:
- **High flat base damage** — significantly more than melee baseline. No stat scaling on damage.
- **Light attack = fire** — the trigger maps to the standard light attack input.
- **LCK scales accuracy** — all shots have a slight angular spread. Higher LCK tightens the cone.
  Low LCK should feel noticeably imprecise; high LCK feels surgical. Nothing punishing at baseline —
  the variation is flavor and build incentive, not a death sentence.
- **STR = hard requirement only** — needed to hold and control the weapon. Does not scale damage.
- **DEX = turn/aim speed** — how fast the player can track a target. Not damage.
- Guns are found/dropped, not crafted from scratch. Ammo is the constraint.

### Weapon Slots
- **Dual one-handed** — two weapons attacking independently (VS-style chaos feel)
- **One two-handed** — single weapon, bonus STR scaling, hits harder
- **One-handed + shield** — gives up dual-wield for parry and guard break protection. STR/END build path.
- Two-handed favors STR builds; dual one-handers favor DEX builds; shield builds favor STR/END

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

## Encumbrance
Carrying too much slows you down. Equipment weight (weapons + armor) is compared against
carry capacity (derived from STR + END). Three tiers:
- **Light** (under ~40% capacity) — full speed, no penalty
- **Medium** (40–70%) — moderate speed reduction
- **Heavy** (70–100%) — significant speed reduction
- **Overencumbered** (over 100%) — unable to run; walk only

The carry weight formula (`str_scale: 20, end_scale: 10`) already exists in `formulas.json`.
Encumbrance tiers and exact speed penalties are to be tuned during gameplay balancing.
_Not yet implemented._

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

## Loot & Drop System

Two distinct drop types — different pickup mechanics, different purpose.

### Money Drops
- Chance-based per kill; LCK influences rate and money drop chance
- **Auto-collected on proximity** (VS-style) — no manual action needed
- Also found in chests and other TBD sources
- Displays as bill/money icon ($1 / $5 / $20 / $50 / $100)
- Carries over on death into meta progression

### Inventory Item Drops (materials, crafting components)
- **Manual pickup** — small souls-style proximity radius; player consciously decides what to grab
- Displays as item icon; bean with rarity glow as performance fallback at scale
- These are the crafting materials the weapon evolution system runs on

### Rarity Tiers
Communicated via glow intensity, size, and animation — not just color.

| Tier | Visual |
|------|--------|
| Common | No glow |
| Uncommon | Soft glow |
| Rare | Medium glow |
| Epic | Strong glow |
| Legendary | Large, pulsing, animated. Unmistakable presence. |

### Adaptive Drop Seeding
Engine tracks the player's most-used weapon and nudges rare drops toward completing
that weapon's upgrade path. Player feels lucky; the game is being fair. LCK offsets
the rarity curve further on top of this.

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
- **Hybrid deliberate/chaos.** Default state: guards are individually dangerous, sparse, require
  thought. Alarm state: VS-style enemy flood, earned by being messy. The emotional core is a heist
  movie — tension, earned chaos, recovery. Never both dangerous AND numerous simultaneously unless
  the player caused it.
- Start small, expand via schema — every enemy is a config entry
- Rank hierarchy maps directly to wave progression
- Appearance and drops become progressively stranger and more off-putting
- Final boss of each map = fully unhinged, clearly demonic royalty
- Gore is comedic — absurd rather than disturbing
- Gore animation and sound design must scale — design for simultaneous events from day one

### Rank Hierarchy (first map)
Names are placeholders — TBD with Alex.

1. **Lost Soul** — basic grunt. The scraped-together damned. Minimal threat alone.
2. **Shade** — slightly smarter, slightly meaner
3. **Imp** — first "real" demon. Faster, more aggressive
4. **Greater Demon** — mid-level. Better drops. Starts to feel dangerous in groups.
5. **Fiend** — commands others. Serious gear. Rare material drops.
6. **Demon Lord** — final boss/elite. Fully unhinged. Specifics TBD.

### Equipment & Drop Mapping
- Lost Souls / Shades: bone fragments, cursed trinkets → stage 1 weapon materials
- Imps / Greater Demons: hellfire components, demon hide pieces
- Higher ranks: rare infernal materials → higher upgrade tier recipes
- All drops are materials/parts — never whole weapons or armor

### Wave Structure
- **Open world, timed intervals** — the map is freely explorable; no room gating, no safe rooms.
  Enemy waves spawn at timed intervals based on the player's current position and time elapsed
  in the area. The longer you stay, the worse it gets.
- Between waves: explore for chests, rest spots, crafting materials, upgrade stations
- Enemy density and composition scale with time and position — pro-gen, not pre-scripted
- Always an objective — the final boss/elite is the escape condition, not wave survival
- Final boss/elite kill = run complete = escape

---

## Alarm / Escalation System

Subject to tuning based on feel.

Guards patrol by default. Triggering an alarm floods the area with enemies — the VS moment.
Three levels maximum to keep it readable. Players should always know what state they're in
and why. Escalation is theatrical and legible, never a hidden meter that punishes unexpectedly.

| Level | State | Enemies |
|-------|-------|---------|
| 0 | Calm | Guards patrol. Sparse, individually dangerous. Heist-movie tension. |
| 1 | Alert | More guards, faster response. Still manageable with care. |
| 2 | Alarm | VS-style flood. Earned chaos. Quantity over individual threat. |

- **Resting resets alarm level** — gives rest spots a second purpose beyond healing
- Alarm triggers TBD — being spotted, killing loudly, taking too long, player choice
- Visual/audio cues must make the current state unmistakable at a glance
- The transition between states is the game's emotional rhythm: stealth/tension → chaos → recovery

---

## Rest Spots

Service hubs scattered procedurally throughout the map. Found by exploring — not guaranteed nearby.
**Not respawn points.** Death ends the run regardless.

What a rest spot offers (expandable):
- **Heal** — restore HP
- **Weapon upgrades** — using materials collected during the run
- **Items** — buy consumables (TBD)
- Anything else we want to add later

In-world name TBD — "rest spot" is a placeholder. Could be a campfire, a soul anchor,
a cursed altar, a fellow damned NPC who patches you up. Alex decides.

---

## Map & Procedural Generation

### Map Feel
- Open roaming space — no key/door/room gating, movement is free like VS
- **Aesthetic arc**: early zones feel like corrupted familiar spaces — liminal, uncanny, distorted
  but recognizable. Zones get progressively more hellish as the player descends. Reference:
  Doom 1 episode progression (E1 = UAC base with hints of something wrong, E2 = corrupted
  facility, E3 = pure hell). The shift is gradual and diegetic — the player experiences the
  reveal, not a cutscene.
- Tone: silly, low fidelity, practical. Funny where possible.
- Visual reference points: Dante's Inferno (circles, fire, brimstone), Doom (demon design,
  escalating brutality, episode progression), early real-world environments as the mundane
  anchor. Alex decides the execution.

### Procedural Generation Architecture
**Core principle: separate structure (owned by engine) from visuals (supplied by modders).**
The engine enforces rules — how spaces connect, wall placement, room flow. Modders supply
sprites mapped to tile types.

#### Key Patterns
- **Tile-based with constraints** — grid of predefined tile types with engine-enforced connection rules
- **Room templates / prefabs** — pre-authored chunks stitched together procedurally. Each template
  is a tile-type grid with metadata tags.
- **BSP (Binary Space Partitioning)** — strong candidate for base generation algorithm.

#### Room Templates
ASCII format, stored in `config/rooms/` as plain text files. Dropping a new `.room` file in the
directory is all a modder needs to add a room type.

| Character | Tile type |
|-----------|-----------|
| `.` | Floor |
| `W` | Wall |
| `D` | Door frame |
| `X` | Obstacle |
| `E` | Enemy spawn point |
| `C` | Chest spawn point |
| `R` | Rest spot |

Engine parses at load time. Additional marker characters can be added as new entity types are
designed — the mapping lives in config, not hardcoded.

#### Seeding
Each run gets a new random seed. Seed is stored and logged for debug reproducibility only —
players never see it. Same seed = same map for bug reproduction purposes. Seed is logged to
console at generation time and stored in the run's metadata.

#### Modder Asset Schema
Modders supply:
- Sprites for each tile type
- Room templates as tile-type grids (ASCII `.room` files)
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
- **Auto-Attack Module** (see below)

### Auto-Attack Module
Permanent unlock. Grants the ability to toggle auto mode during runs. Buy once, available on all future runs.

- **Tier 1 — Auto-Attack**: Weapons auto-fire on cooldown with smart targeting. Shield auto-blocks attacks within frontal arc. Special attacks (weapon skills) still require manual input.
- **Tier 2 upgrade — Auto-Parry**: Parry timing is handled automatically when a shield is equipped. Purchased as a separate follow-up upgrade.

This is intentionally a QOL/accessibility item, not a power unlock. Manual combat is the intended base game. Players who want the VS-style experience earn it through meta progression.

### QOL: Portable Weapon Upgrader
- Allows weapon upgrading without finding a station in the map
- Ranked system: rank 1 handles tiers 1-3, rank 2 handles tiers 3-6, etc.
- Coexists with map upgrade stations — does not replace them
- Highest tier upgrades always require a map station — keeps late-run exploration meaningful

### Rare In-Map Discoveries
- Extremely rare items found during runs that permanently affect meta progression
- Example: a crafting guide that permanently unlocks a recipe for future runs
- Creates tension between rushing to the exit and exploring the map

---

## Controls

Hades movement model + Souls lock-on layer. Subject to tuning based on feel.

### Movement & Aiming
- **WASD** — world-relative movement. W always means up on screen. Predictable dodge direction.
- **Mouse** — character always faces toward cursor when unlocked. Independent of movement direction.
  Twin-stick feel: WASD moves, mouse aims.

### Input Modes
Three modes, contextually switched:

1. **Free aim (default)** — WASD moves, mouse aims. Twin-stick. Primary PC mode.
2. **Lock-on** — Tab/MMB toggles. Facing snaps to target enemy. A/D become strafes,
   S becomes backstep (Dark Souls model). Unlock to return to free aim.
3. **Keyboard-only fallback** — when no mouse movement is detected, WASD controls both
   movement and facing (pre-mouse behavior). For gamepad / accessibility / "runs on a
   calculator" portability.

Lock-on is a toggle, not a hold.

### Aim Smoothing (Turn Sensitivity)
Visual facing uses an exponential blend toward the gameplay facing direction. This filters
mouse micro-tremor while keeping rotation fluid. The blend factor is exposed as a player
setting ("Aim Smoothing" or "Turn Sensitivity"):

- **High sensitivity (0.5-0.8)** — snappy, near-instant, competitive
- **Medium (0.2-0.4)** — smooth, filtered (default 0.25)
- **Low (0.05-0.15)** — heavy smoothing, cinematic

Gameplay facing (`FacingDirection.dx/dy`) is always instant — combat targeting has zero
latency. Only the visual representation (`render_dx/dy`) is smoothed.

Future: encumbrance or heavy armor could reduce the blend factor, making turning feel
sluggish. DEX could push it higher. Subtle but felt.

### Input Map

| Action | PC | iOS virtual |
|---|---|---|
| Move | WASD | Joystick |
| Aim | Mouse position | Joystick direction |
| Light attack | LMB | Attack btn |
| Sprint | Space (hold ≥200ms) | Sprint btn (hold) |
| Dodge / roll | Space (tap <200ms) | Dodge btn (tap) |
| Block | RMB (hold) | Block btn (hold) |
| Parry / Weapon skill | Q — context-sensitive: fires parry window if blocking, weapon skill otherwise | Skill btn |
| Lock-on toggle | Tab / MMB | Lock btn |
| Use item | R | Item btn |
| Cycle item | Scroll | Swipe |
| Interact | F | Interact btn |
| Stat upgrade (level-up popup) | [1] STR  [2] DEX  [3] END  [4] LCK | Tap card in popup |

iOS target: 6 virtual buttons (attack, dodge, block, skill, lock, item) + joystick. Manageable.

---

## Config Directory Structure

```
config/
  entities/   — defines what things ARE (player, enemies, weapons, armor)
  spawns/     — defines enemy wave composition and spawn rules
  balance/    — defines how the game BEHAVES (formulas, leveling, tuning)
    formulas.json
```

**Mod override strategy:** deep merge — mod files override only the keys they define;
base game values fill everything else. Engine validates all configs at load time.

---

## Passive Leveling

**Deferred — design intent only. Not scheduled for implementation yet.**

- Additive on top of the main level-up system
- Actions grow relevant stats via micro-XP (Oblivion-inspired): frequent sword swings
  nudge STR/DEX, taking hits nudges END, etc.
- Soft cap required — passive gains must not break build identity or upset balance
- Implementation approach TBD
