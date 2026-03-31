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

## Progression Model

Two layers coexist. The meta layer is primary — it defines who your character is.

### Meta progression (persistent RPG) — DECIDED
Each run starts at the player's current meta-progression stats. The character carries power
forward across runs. The world scales to match — a veteran character faces harder enemies
than a fresh one. Meta progression is the true RPG layer; runs are the gameplay unit.

**Death penalty:** Minimal or none. VS philosophy — losing a run should never feel like
wasted time. Any death consequence would be very minor. Exact mechanic TBD.

### In-run progression (roguelike) — MECHANICS UNDECIDED
How the player gains power during a run is not locked. Options under consideration:
- XP → level up → pick a stat (current placeholder implementation)
- Rare item drops that raise stats or Essence mid-run
- A hybrid where how you play (which stats you use) influences meta-progression outcomes
- Some combination of the above

The interesting design space: in-run behavior feeding back into meta character development
in a meaningful way. Full design exploration tracked separately (see GitHub issue TBD).

### Essence — CONCEPT CONFIRMED, MECHANICS TBD
Per-stat meta resource (str/dex/end/lck fields, 0–100+ scale). Earning/spending mechanics,
caps, and relationship to in-run leveling are all open. The `Essence` component exists in
code as a placeholder; how it's modified is not yet designed.

---

## Character Creation

Happens once at the start of a fresh save. Should take under two minutes.

- **Name** — purely flavor
- **Starting perk** — chosen from a list. Sets tonal build identity.
- **Appearance** — locked after creation. A meta store item allows redesign.
- **Starting armor** — the clothes on your character's back. No special gear. Everything else
  is found in runs.

(No stat allocation at creation — stats start at 1/1/1/1 always. Build identity comes from
perk choice and what you invest Essence into over time.)

---

## Stats

| Stat | Function |
|------|----------|
| STR | Attack power. Scales with heavy/two-handed weapons. |
| DEX | Attack speed + movement speed. Scales with light/one-handed weapons. |
| END | Max health. |
| LCK | Rare drop rate + ranged weapon accuracy. Offsets the rarity curve on most-used weapon drops. |
| DEF | **Derived — not leveled.** Calculated from base defense + STR + END + overall level + equipped armor. See "HP vs DEF" section below. |
| Poise | **Derived — not leveled.** Calculated from equipped armor weight and tier. Determines knockback resistance and stagger threshold. High poise = barely flinch; low/no armor = gets bowled over. Mechanically simulated "mass." |

---

## HP vs DEF — Design Philosophy

### What is HP?

HP (hit points) was invented for naval wargames in the 1960s — ships had hit points
representing structural integrity. A battleship could absorb more cannon hits than a
destroyer. Pure physical durability.

When Dave Arneson brought HP to Dungeons & Dragons (1974), the meaning shifted. Gary Gygax
explicitly wrote that HP is *not* just meat — it abstracts:
- Physical toughness (the body's ability to endure)
- Combat instinct (dodging, rolling with blows)
- Luck and divine favor
- Will to keep fighting

That's why a level 20 fighter survives a 100-foot fall that kills a commoner — they're not
physically 20x tougher, they're better at *surviving*. HP is **narrative resilience**: the
total capacity to endure punishment before going down.

### What is DEF?

DEF is fundamentally different. DEF is the **physical reality of what happens when force meets
material**. It's not abstract — it's concrete. When a club strikes bone, how much energy
transfers into harm vs deflects off the surface? DEF is damage *mitigation per hit*.

A glass cannon with high HP and 0 DEF takes full damage on every hit but survives many. A tank
with lower HP but high DEF barely feels small hits. These are genuinely different survival
strategies.

### Base Defense = Material Science

Every creature has an inherent damage threshold — the minimum force needed to cause real harm.
Below that threshold, the hit is absorbed by the body's physical structure without meaningful
injury. This is **base defense**: the body's innate material resistance, independent of any
stat investment, training, or equipped armor.

This mirrors how `base_damage` works for weapons: a fist has inherent striking power from its
mass and rigidity — any punch hurts at least a little, regardless of STR. By the same logic,
a body has inherent shock absorption from its mass and structure, regardless of END.

Base defense is **per-entity** because it represents what the creature is physically made of:

| Creature | Base DEF | Reasoning |
|----------|----------|-----------|
| Human (player) | 3 | Skin, muscle, fat padding, bone structure — absorbs minor impacts |
| Skeleton | 2 | Bone is hard but brittle. Strikes glance off curved surfaces, no soft tissue to cut. But no muscle mass for shock absorption. |
| Slime (future) | 0 | Amorphous, no structure — everything passes through. High HP compensates. |
| Demon (future) | 5+ | Supernatural hide, thick scales, otherworldly resilience |

This value is defined per entity in JSON config as its own component (`"body": { "base_defense": 3 }`),
separate from `"stats"` — because stats are levelable character traits, while body properties
are fixed material science. The `Body` component will grow to include damage type resistances,
hit effect material type, and mass as those systems are built.

**Natural weapons (unarmed attack)** also live in `Body` — a creature's physical composition
determines how hard it hits barehanded, just as it determines how hard it is to damage.
The player's unarmed scaling is intentionally stronger than most enemies: living human muscle
responds to STR/DEX training better than magically-animated bones or mindless flesh.
A trained, unarmed player should still feel dangerous.

| Creature | Unarmed DMG | STR Scale | DEX Scale | Reasoning |
|----------|-------------|-----------|-----------|-----------|
| Human (player) | 5.0 | 1.0 | 0.75 | Full muscle engagement, benefits heavily from training |
| Skeleton | 3.0 | 0.25 | 0.0 | No muscle mass behind the strike — magic provides motion, not force |
| Demon (future) | 8.0+ | 0.8 | 0.3 | Supernatural claws, raw strength matters more than finesse |

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
DEF = floor(base_defense + (STR * strDefScale) + (END * endDefScale) + (level * levelDefScale) + armorValue)
finalDamageTaken = max(1, incomingDamage * (1 - min(DEF, defCap) / 100))
```
`base_defense` is per-entity (material science — see above). Stat scaling and armor stack on top.
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

### XP Drops (Log Scaling)
Enemy XP uses logarithmic scaling instead of linear multiplication. The leveling cost
curve (level^1.5) does the heavy lifting for diminishing returns — the drop formula stays
relatively flat so individual kills feel consistent.

```
xpDrop = base_xp * (1 + log_scale * ln(enemy_level))
```

At `base_xp=20, log_scale=0.6`:
- Level 1: 20 XP, Level 5: 39 XP, Level 10: 47 XP, Level 20: 55 XP

Compare to the old linear formula (`base * level`): Level 10 would give 200 XP. The log
formula gives 47 — much closer to Dark Souls pacing where the XP economy is driven by
the cost curve, not the drop size.

### Essence (Natural Talent)
Per-stat natural ability score, 0-100. Think Pokemon IVs that can be increased. Rolled
randomly for enemies at spawn. Player starts at 0; modifiable through meta-progression
items (rare, intentionally gated).

```
essence_bonus = floor(essence_value * base_stat / 100)
```

Essence is a percentage multiplier on the stat it applies to, calculated after wave level
scaling. An enemy with STR=5 and essence_str=60 gets `floor(60*5/100) = 3` bonus STR.

Stored as a separate `Essence` component so the bonus can be inspected and displayed
independently of the final stat values.

### Wave Level Scaling (Power Progression)
Enemies get stronger each wave via a level system. Enemy level scales linearly with wave
number; each level adds a flat stat bonus to all stats before essence rolls.

```
enemy_level = 1 + floor((wave - 1) * level_growth)
stat_bonus  = (enemy_level - 1) * stat_per_level
```

At `level_growth=0.5, stat_per_level=1`: wave 1 = Lv1 (no bonus), wave 3 = Lv2 (+1 all),
wave 5 = Lv3 (+2 all), wave 11 = Lv6 (+5 all).

### `formulas.json` shape
```json
{
  "hp":               { "base": 5, "scale": 100 },
  "movement":         { "base": 150, "dex_scale": 30, "sprint_multiplier": 1.6, "sprint_blend": 8.0, "walk_blend": 20.0, "backpedal_multiplier": 0.5, "sprint_anim_speed": 0.65, "backpedal_anim_speed": 1.4 },
  "carry_weight":     { "str_scale": 20, "end_scale": 10 },
  "defense":          { "str_scale": 0.3, "end_scale": 0.5, "level_scale": 0.2, "cap": 75 },
  "luck":             { "drop_scale": 15, "quality_scale": 3, "quality_thresholds": [30, 60, 80, 95] },
  "damage":           { "grade_thresholds": { "S": 1.5, "A": 1.25, "B": 1.0, "C": 0.75, "D": 0.5, "E": 0.25 } },
  "swing":            { "weight_scale": 5, "stat_scale": 160, "two_handed_str_bonus": 0.3 },
  "stat_requirement": { "penalty_rate": 0.15 },
  "leveling":         { "xp_base": 100, "xp_exponent": 1.5, "points_per_level": 1 },
  "poise":            { "end_scale": 2.0, "str_scale": 1.0, "weight_scale": 20, "stagger_duration": 0.5, "decay_window": 5.0 },
  "essence":          { "min": 0, "max": 100 },
  "xp_drop":          { "log_scale": 1.5, "min_fraction": 0.1, "level_penalty": 0.15 },
  "fist":             { "weight": 0.5, "base_damage": 5.0, "str_scaling": 1.0, "dex_scaling": 0.75 },
  "weapon_xp":        { "kill_multiplier": 1.0, "hit_multiplier": 0.05, "base_xp": 50.0, "exponent": 2.0,
                         "growth_bonus_per_quality": 0.1, "decay_rate": 0.05, "carry_factor": 0.15,
                         "power_level_weight": 1.0, "power_hp_weight": 0.1, "power_dmg_weight": 0.5,
                         "power_stat_weight": 0.2 },
  "equip_load":       { "base_capacity": 40.0, "str_scale": 3.0, "end_scale": 1.5,
                         "light_threshold": 0.3, "medium_threshold": 0.7, "heavy_threshold": 1.0,
                         "light_speed": 1.0, "medium_speed": 0.9, "heavy_speed": 0.7, "overloaded_speed": 0.4 }
}
```
Note: `base_defense` is per-entity (in entity JSON `"body"` block), not a formula constant.
See "HP vs DEF" section above.

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

### Stamina (Elden Ring style)
Single stamina pool, drained by combat actions at rates proportional to weapon weight.
Heavier weapons cost more per action. Higher END stat increases the pool via `base + end_scale * ln(END + 1)`.

| Action | Cost formula | Fist (w=0.5) |
|--------|-------------|--------------|
| Swing | weight * 3.0 | 1.5 |
| Dodge | weight * 5.0 | 2.5 |
| Skill | weight * 4.0 | 2.0 |
| Sprint | weight * 1.0 | 0.5/s |

Recovery: 2.5/s after 1.0s delay since last deduction. Recovery delay is longer than any
weapon cooldown, so rapid attacks get zero regen. At END=1, the player has ~7 stamina --
enough for 4 fist swings or a swing-dodge-swing combo before running dry.

Hitting 0 stamina = 0.6s exhaustion stagger (Staggered component). Below 40% stamina,
visual desaturation + heartbeat audio kick in as a warning.

**Shields** — guard break: absorbing too many consecutive blocked hits staggers the player. Punish moment, not gradual drain.

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
- `str_requirement` / `dex_requirement` — stat floor for full effectiveness. Below threshold applies exponential penalty (see Combat System). **Rule: all weapons must have at least 1 STR and 1 DEX requirement.** Fists (unarmed) are the only exception (0/0).

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
- **No stat requirements for crafting.** Any player can craft any recipe as long as they have the
  ingredients. Stat requirements only gate *using* (equipping) the crafted item, not making it.
- **Field crafting** — combine world-drop materials using a basic starting tool. No station needed.
- **Upgrade stations** — scattered randomly in the map. Used for all upgrades beyond base.

### Weapon Tiers (Growth Classes)
Each weapon belongs to a **tier** — a broad mechanical class that defines default per-level
growth rates when the weapon gains XP. Tiers are loaded from `config/balance/weapon_tiers.json`.

| Tier | damage_per_level | scaling_per_level | Archetype |
|------|-----------------|-------------------|-----------|
| dagger | 0.8 | 0.015 | Fast, light blades |
| sword | 1.2 | 0.02 | Balanced blades |
| club | 1.5 | 0.01 | Slow, heavy blunt |

Individual weapons can override tier defaults via `"damage_per_level"` and `"scaling_per_level"`
in their item JSON. Override value of -1 (or omitted) = use tier default.

### Weapon XP & Leveling — DECIDED

Weapons level through combat use. No hard level cap — growth naturally decays via quality-driven
diminishing returns. Primary XP source is kills; secondary is a per-hit trickle.

**XP sources:**
- **Kill:** `enemy_power * kill_multiplier` (default 1.0)
- **Hit:** `enemy_power * hit_multiplier` (default 0.05)

**Enemy power rating:** Weighted sum of enemy stats:
```
power = level_weight * level + hp_weight * max_hp + dmg_weight * base_damage + stat_weight * total_stats
```
All weights configurable in `formulas.json` under `"weapon_xp"`.

**Level-up formula:**
```
xp_to_next = base_xp * pow(level, exponent / quality_factor)
quality_factor = 1.0 + quality_tier * 0.1
```
Higher quality weapons have a gentler XP curve (quality_factor softens the exponent).

**Stat growth per level:**
```
growth_factor = quality_factor / (1 + level * decay_rate / quality_factor)
damage_gain = damage_per_level * growth_factor
scaling_gain = scaling_per_level * growth_factor
```
Growth decays toward zero as level rises. Higher quality = slower decay. No hard cap needed —
the math naturally flattens.

### Weapon Evolution Trees — DECIDED

**Terminology** (Monster Hunter convention):
- **Tree** — the entire weapon lineage for a weapon class (e.g. all bladed weapons)
- **Branch** — a divergent path within the tree
- **Base weapon / root** — the starting weapon (e.g. shiv)
- **Final form** — terminal node with no further evolutions
- **Node** — a specific weapon in the tree

**File organization:** One JSON file per weapon class in `config/evolution/`:
- `blades.json` — shiv → dagger → short sword → longsword (anything with an edge)
- `bludgeons.json` — pipe → club → mace → warhammer (planned)
- `ranged.json` — slingshot → crossbow → gun (planned)

Files are named by weapon class, not by the root weapon. ConfigLoader scans the directory —
adding a new tree = adding a new JSON file.

**Starter weapons:** The root of each tree is a starter weapon — no stat requirements, low
damage/scaling, common rarity. Designed to flat-evolve into the first real weapon in the tree.
Every player can pick one up and use it immediately.

**Evolution path types:**
- **Flat upgrade** — level requirement only (no materials). `"target": "dagger", "min_level": 5`
- **Branch upgrade** — level + materials. `"target": "short_sword", "min_level": 10, "material": "config/items/materials/bone_shard.json", "material_qty": 3`

**Cross-tree evolution:** Rare special evolutions that cross into a different weapon class
(e.g. sharpening a pipe into a blade). Signaled by an optional `"target_tree"` field on the
evolution entry. If absent, target is in the same tree. If present, references a node in
another tree file. Not yet implemented — will be added when a second tree exists.

**Carry-forward bonus:** When a weapon evolves, a portion of progress carries forward:
```
evolution_bonus = old_bonus + old_level * carry_factor
```
The bonus accumulates across multiple evolutions, rewarding deep progression.

**Implementation:** `EvolutionRegistry` stores all trees. `weapon_to_node` map provides O(1)
reverse lookup from any weapon config_path to its family + node. Evolution execution via
`InventoryOps::evolveWeapon()`.

### Stat Requirements
- Must meet stat requirements to equip a base weapon (souls-style)
- Upgrade path inherits base requirements — no new stat gates per upgrade

---

## Encumbrance — IMPLEMENTED

Equipment load compares total equipped weight against carry capacity. Affects movement speed.

```
capacity = base_capacity + STR * str_scale + END * end_scale
equip_load_ratio = total_weight / capacity
```

| Tier | Ratio | Speed Multiplier | Default |
|------|-------|-----------------|---------|
| Light | < 30% | 1.0x | Full speed |
| Medium | 30–70% | 0.9x | Slight reduction |
| Heavy | 70–100% | 0.7x | Significant reduction |
| Overloaded | > 100% | 0.4x | Near-crawl |

All thresholds and speed multipliers configurable in `formulas.json` under `"equip_load"`.
Weight is summed from ALL 8 equipment slots (weapon, shield, 4 armor pieces, 2 accessories).
`ArmorStats.load_tier` is recomputed by `EquipmentSystem` every frame equipment changes.
`MovementSystem` reads `load_tier` and applies the speed multiplier.

---

## Equipment Slots — DECIDED

8 equipment slots, each holding one `ItemInstance`:

| Slot | Category | Effect |
|------|----------|--------|
| Main Hand | Weapon | Synced to Weapon component by EquipmentSystem |
| Off Hand | Shield (armor with max_guard > 0) | Synced to Shield component |
| Head | Armor (ArmorSlot::Head) | Contributes defense + poise |
| Chest | Armor (ArmorSlot::Chest) | Contributes defense + poise |
| Legs | Armor (ArmorSlot::Legs) | Contributes defense + poise |
| Feet | Armor (ArmorSlot::Feet) | Contributes defense + poise |
| Accessory 1 | Accessory | Stat bonuses (planned) |
| Accessory 2 | Accessory | Stat bonuses (planned) |

`EquipmentSystem` detects slot changes each frame and syncs to runtime components
(Weapon, Shield, ArmorStats). Tab cycling rotates through inventory weapons + fists.

### ArmorStats — DECIDED

Aggregated from all equipped armor pieces by `EquipmentSystem::recomputeArmorStats()`:
- `total_defense` — flat damage reduction, applied BEFORE percentage-based DEF
- `total_poise_bonus` — sets Poise.max (determines knockback/stagger resistance)
- `total_weight` — sum of weight from all 8 equipped slots (not just armor)
- `equip_load_ratio` — total_weight / carry capacity (see Encumbrance)
- `load_tier` — 0=light, 1=medium, 2=heavy, 3=overloaded

**Damage pipeline order:**
1. Stat-requirement penalty on attacker (exp decay)
2. Flat armor DR: `rawDamage = max(0, rawDamage - ArmorStats.total_defense)`
3. Percentage DEF: `finalDamage = max(1, rawDamage * (1 - DEF/100))`

### Shield Frontal Arc — DECIDED

Shield blocking only works against attacks from the front. Attacks from behind (>90 degrees
from facing direction) bypass the shield entirely.

Implementation: dot product of normalized (attacker_pos - defender_pos) vector with defender's
FacingDirection. `dot > 0` = front (blocked), `dot <= 0` = behind (bypasses shield).
This applies to both normal blocks and parry windows.

---

## Armor & Drip

### Functional Armor
- Dropped by enemies as materials/parts — never as a complete item
- Crafted or assembled like weapons
- Base: flat DR (ArmorStats.total_defense) applied before percentage-based DEF
- Also contributes poise bonus for stagger resistance
- Weight affects equip load tier and movement speed
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

Unified drop system — all drops (materials, money, crafting components) use the same
pipeline. Every drop is an item in the drop table with a rarity tier and drop chance.

### Pickup Mechanics
- **All pickups auto-collect on proximity** — walk near it, pick it up
- Player does NOT know what an item is until they collect it — only the rarity glow is
  visible on the ground. The reveal happens on pickup.
- Money items (category "money") add to Wallet instead of inventory
- Also found in chests and other TBD sources

### Money as Items
Money denominations are regular items in the drop table, each with its own rarity:

| Item | Rarity | Value |
|------|--------|-------|
| $1 Bill | Common | $1 |
| $5 Bill | Uncommon | $5 |
| $20 Bill | Rare | $20 |
| $50 Bill | Epic | $50 |
| $100 Bill | Legendary | $100 |

Money carries over on death into meta progression.

### Item Quality
Each item instance has a quality tier, rolled on drop, influenced by LCK:

| Tier | When |
|------|------|
| Crude | Low LCK, bad roll |
| Common | Default |
| Fine | Moderate LCK |
| Superior | High LCK |
| Masterwork | Very high LCK or lucky roll |

Quality of crafting materials determines quality of crafted output (averaged).
Formula: `score = random(0,100) + lck * quality_scale + total_essence * essence_quality_scale`,
mapped against configurable thresholds in `formulas.json`. Enemy Essence (sum of all four
stats) boosts the quality roll -- tougher enemies drop better-quality items.

### Rarity Tiers (Visual)
Communicated via purple-tinted glow — intensity escalates with rarity. Player sees
the glow but not the item identity until pickup (Elden Ring style).

| Tier | Glow | RGB |
|------|------|-----|
| Very Common | No glow (neutral grey) | (0.35, 0.35, 0.35) |
| Common | Dim grey-violet | (0.45, 0.4, 0.5) |
| Uncommon | Soft purple | (0.55, 0.4, 0.7) |
| Rare | Medium violet | (0.6, 0.3, 0.85) |
| Epic | Bright violet | (0.75, 0.25, 0.95) |
| Legendary | Brilliant white-violet | (0.9, 0.8, 1.0) |

Future: glow intensity, size, and animation will reinforce rarity beyond just color.

### Adaptive Drop Seeding
Engine tracks the player's most-used weapon and nudges rare drops toward completing
that weapon's upgrade path. Player feels lucky; the game is being fair. LCK offsets
the rarity curve further on top of this.

---

## Discovery / Compendium — DECIDED

Tracks every unique item the player has ever picked up or evolved into. Persists across runs
(meta-progression).

**Behavior:**
- When an item is picked up for the first time, it's added to the Compendium
  (`unordered_set<string>` of config_paths).
- The pickup notification shows "(NEW!)" in a distinct color for newly discovered items.
- `ItemInstance.newly_discovered` flag is set on the instance for UI badge display.
- When a weapon is evolved, the new weapon is automatically discovered.

**Future plans:**
- Compendium UI screen to browse all discovered items
- Completion percentage tracking
- Discovery-gated rewards (meta store unlocks, achievements)

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

### AI Abilities (Intelligence Scaling)
Enemy AI behaviors unlock based on level thresholds or are innate to the enemy type. Think
Pokemon move-learning: some enemies hatch knowing how to dodge, others earn it at a certain
level. The player never sees a tooltip — they just notice the enemy is harder to fight.

**Two paths to an ability:**
- **Innate** — the enemy type has it from spawn (e.g. a mini-boss always parries)
- **Level-gated** — earned at a threshold (e.g. skeleton learns dodge at level 3)

**Scaling with mastery:** Abilities are not binary. They carry data (cooldown, reaction time,
success chance) that improves with level. The scaling input is levels above the unlock
threshold — or full level for innate abilities. A skeleton that unlocked dodge at level 3
and is now level 7 has 4 levels of mastery: faster reaction, shorter cooldown. An innate
dodger at level 7 has 7 levels of mastery.

**Config shape (per entity JSON, `ai_abilities` block):**
```json
"ai_abilities": {
  "dodge": {
    "level": 3,
    "base_cooldown": 2.0,
    "cooldown_per_level": -0.15,
    "base_reaction": 0.5,
    "reaction_per_level": -0.03
  },
  "parry": {
    "level": 5,
    "base_window": 0.2,
    "window_per_level": 0.01
  },
  "flank": {
    "innate": true,
    "base_angle": 45,
    "angle_per_level": 2
  }
}
```

**ECS implementation:** Each ability maps to a component + system. `DodgeSystem` only
processes entities with `CanDodge`. When an enemy spawns at or levels past a threshold, the
ability component is emplaced with data computed from the mastery level. No monolithic AI
function — cost is proportional to how many enemies carry each ability.

**Design lever:** A level 10 skeleton that unlocked dodge at level 3 (7 levels of mastery)
feels different from a mini-boss with innate dodge at level 10 (10 levels of mastery). Same
ability, different feel. Enemy personality emerges from config alone.

**Candidate abilities (initial set, expandable):**
- **Dodge** — evade incoming attacks. Scales: reaction time, cooldown, distance
- **Parry** — deflect and counter. Scales: parry window duration, counter damage
- **Flank** — approach from the side/rear. Scales: flanking angle, commitment
- More TBD as enemy types are designed

### Wave Structure
Waves are auto-generated from rules in `config/waves.json`. Player starts each wave manually
(R key during rest phase). Enemy count, spawn interval, burst size, and composition all scale
per wave via configurable formulas.

- **Enemy count:** `floor(start_count * count_growth^(wave-1))`, clamped to `max_count`
- **Spawn interval:** `start_interval * interval_decay^(wave-1)`, floored at `min_interval`
- **Burst size:** `start_burst + floor((wave-1) / burst_growth_every)`, capped at `max_burst`
- **Safe rooms:** every Nth wave (configurable), player gets a rest phase
- **Enemy pool:** each enemy type has a `from_wave` (earliest wave it appears) and `weight`
  (relative spawn proportion). New types phase in as waves progress.
- **Manual overrides:** specific wave numbers can be hand-authored (boss fights, events)
  via `overrides` in `waves.json`. Overrides replace auto-generation entirely for that wave.
- **Infinite mode:** `max_waves=0` means waves never stop. The escalation IS the endgame.

Between waves: explore for chests, rest spots, crafting materials, upgrade stations.
Always an objective — the final boss/elite is the escape condition, not wave survival.
Final boss/elite kill = run complete = escape.

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

## Rest Spots & Sanctuary — DECIDED

Service hubs scattered procedurally throughout the map. Found by exploring — not guaranteed nearby.
**Not respawn points.** Death ends the run regardless.

**Behavior:**
- **Auto-heal on proximity** — walking into the rest spot radius automatically restores HP
  and stamina. No menu interaction needed. Cooldown prevents exploitation.
- **Sanctuary menu auto-opens** — when the player enters the radius, the Sanctuary screen
  opens automatically. Closes when the player leaves.

**Sanctuary menu options:**
- **Evolve Weapon** — if the equipped weapon has available evolution paths (level met +
  materials in inventory), evolve it. Consumes materials, applies carry-forward bonus.
- **Craft** — opens the crafting screen to build items from collected materials.
- **Leave** — close the menu and keep moving.

**Not a menu option:** Healing is automatic, not manual. The player doesn't choose to heal —
they heal by being near the rest spot.

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

> **Current status:** The procgen system is temporary scaffolding. Random room placement +
> corridor carving serves as a testbed for spawning, AI, and combat. Hand-authored maps
> will replace it. Don't over-invest in the procgen itself — keep changes cheap and
> easy to rip out.

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

### Backpedal
When the player moves in the opposite direction of their aim (dot product < 0), they
backpedal at reduced speed (`backpedal_multiplier`, default 0.5x). Both body parts face the
aim direction, and the walk animation plays in reverse. Sprinting is disabled while
backpedaling. Foundation for Souls-style retreating combat.

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

## UI Architecture

### Engine Layer: UIRenderer
`UIRenderer` (engine) provides batched screen-space drawing primitives: `drawRect`,
`drawTexturedRect`, `drawText`. All UI is built from these. No draw-line or draw-circle —
complex shapes use dot grids (see AIDebugOverlay). Font management via `FontManager` with
`FontHandle` IDs.

### Game State Machine
Two enums drive screen routing:

- **`GameState::Phase`** — top-level app state: `MainMenu`, `CharCreate`, `LoadGame`,
  `Playing`, `Victory`, `GameOver`, `RunSummary`, `HighScores`. Determines which full-screen
  renders.
- **`UIState::Screen`** — in-game overlay state: `None`, `Menu` (pause), `LevelUp`,
  `Sanctuary`, `Crafting`. Active only during `Playing` phase.
- **`UIState::Tab`** — pause menu tab: `Status`, `Inventory`, `Equipment`.

`GameLoop` dispatches rendering based on these states. Screens are static namespaces with
`init()` and `render()` functions — no inheritance, no virtual dispatch.

### Screen Inventory

**Full-screen menus** (own `GameState::Phase`):
- `MainMenuScreen` — title, New Game / Load / High Scores / Quit
- `CharCreateScreen` — name entry, start/back
- `LoadGameScreen` — save slot list with delete confirmation
- `HighScoresScreen` — score table
- `RunSummaryScreen` — post-run stats breakdown
- `GameOverScreen` — death screen
- `VictoryScreen` — escape success screen

**In-game overlays** (active during `Playing`):
- `PauseMenu` — tabbed: Status (stats + equipped gear), Inventory (grid), Equipment (slots).
  Resume / Escape Run / Quit buttons.
- `LevelUpScreen` — stat picker popup (STR/DEX/END/LCK)
- `CraftingScreen` — recipe list with material requirements
- `SanctuaryScreen` — rest spot menu (Evolve / Craft / Leave) using `MenuDialog`
- `InventoryScreen` — inventory grid (used as a sub-view within PauseMenu)

**Reusable dialog templates:**
- `ConfirmDialog` — centered Yes/No popup. Auto-sizes to content. Keyboard + mouse input.
- `MenuDialog` — centered option list with labels + descriptions. Auto-sizes. Used by
  SanctuaryScreen.

Both measure text content first, then compute panel dimensions — never hardcode panel sizes.

### Persistent HUD
`HudRenderer` draws HP bar, stamina bar, XP bar, money, wave info, weapon name, and a
clickable Menu button. Rendered every frame during `Playing` phase.

### Other Renderers
- `InteractionPromptRenderer` — "Press F" prompts near interactable entities
- `ItemStatRenderer` — weapon/armor stat comparison tooltip. Owns `rarityColor()` for
  consistent rarity coloring across all UI.
- `DebugOverlay` (F3) — FPS, entity count, player coords, wave state
- `AIDebugOverlay` (F4) — enemy AI state visualization (dot-based)
- `AIRecorder` (F5) — CSV state dump of last 5s of AI data

### Shared UI Utilities

**`screens/ScreenColors.h`** — shared color palette used by all screens:
- `TEXT_WHITE`, `TEXT_DIM` — standard text colors
- `OVERLAY` (alpha 0.75) — in-game popup background
- `OVERLAY_OPAQUE` (alpha 0.92) — full-screen menu background
- `PANEL_BG` — default panel background
- `BTN_NORMAL`, `BTN_HOVER`, `BTN_BG`, `BTN_BG_HL` — standard button colors

Screens import via `using namespace screen_colors;`. Per-screen overrides (e.g.
ConfirmDialog's red title, per-screen panel backgrounds) stay as local `constexpr` with
unique names.

**`screens/ScreenInput.h`** — shared input helpers:
- `keyPressed(em, scancode)` — SDL scancode check against frame's key-down events
- `mouseClicked(em, button)` — mouse button check (SDL_BUTTON_LEFT, SDL_BUTTON_RIGHT)
- `hoveredRow(mx, my, cx, cy, cw, row_h, count)` — row hit-testing for list UIs

### Notification System
`NotificationSystem` manages timed popup messages (item pickups, level-ups, discoveries).
Messages queue and display at screen top with fade-out. "(NEW!)" badge for first-time item
discoveries.

---

## Config Directory Structure

```
config/
  animations/     — sprite sheet layout definitions (sidecar JSONs)
  audio/          — sound effect and music mappings
  balance/        — formulas, scoring, weapon tiers (how the game BEHAVES)
  entities/       — defines what things ARE (player, enemies)
  evolution/      — weapon evolution trees (one file per weapon class: blades.json, etc.)
  items/          — item definitions, organized by category:
    weapons/      — weapon JSONs (shiv.json, dagger.json, longsword.json, ...)
    armor/        — armor JSONs (bone_helm.json, bone_cuirass.json, ...)
    shields/      — shield JSONs (bone_shield.json, ...)
    accessories/  — accessory JSONs (bone_ring.json, ...)
    materials/    — crafting material JSONs (bone_shard.json, ...)
    money/        — money denomination JSONs ($1.json, $5.json, ...)
  recipes/        — crafting recipe JSONs (one per recipe)
  rooms/          — ASCII room templates for procedural generation
  spawns/         — enemy wave composition and spawn rules
  waves.json      — auto-wave generation rules and enemy pool
  tilemap.json    — tile definitions and generation parameters
```

**Mod override strategy:** deep merge — mod files override only the keys they define;
base game values fill everything else. Engine validates all configs at load time.

---

## Repository Architecture

```
prison-break-game/
  engine/                  Static library — reusable 2D game infrastructure
    include/               Public headers
    src/                   8 systems + core (Engine, ConfigLoader, TextureManager)
    tests/                 Engine-only tests (link: engine)

  game/                    Everything specific to THIS game
    include/systems/       Game system headers
    src/                   main.cpp, GameLoop.cpp, 12 game systems
    tests/                 Game tests (link: game-systems -> engine)
    assets/                Sprites, SFX, tiles (synced to build/bin/)
    config/                Entity defs, balance, waves, animations
    scripts/               Asset generators, test runner
    docs/                  DESIGN.md, PERFORMANCE.md

  cmake/                   Engine dependency fetching (SDL2, entt, etc.)
  tools/                   Engine dev tools (Tracy analyzer)
```

**Engine (8 systems):** RenderSystem, CameraSystem, CollisionSystem, AnimationSystem,
AudioSystem, TileMapRenderer, FlowFieldSystem, SteeringSystem.

**Game (18 systems):** WaveSystem, CombatSystem, DamageSystem, DeathSystem,
LevelingSystem, PickupSystem, RestSpotSystem, ParticleSystem, AggroSystem, ChaseSystem,
MovementSystem, SpawnerSystem, TintSystem, InputMappingSystem, AnimStateSystem,
EquipmentSystem, WeaponXPSystem, NotificationSystem, CraftingSystem.

**Boundary rule:** "Could this system work unchanged in a completely different 2D game?"
Yes = engine. No = game. The engine knows nothing about the game — it provides a
callback slots: `GameUpdateFn` (fixed-step tick) via `engine.setGameUpdate()` and
`PerFrameFn` (per-frame, before ticks) via `engine.setPerFrameUpdate()`.

```
Engine::run()
  per-frame callback              game handles mouse-facing, per-frame input
  fixed-step loop
    Engine::processEvents()        engine handles SDL events
    Engine::update(dt)
      game_update(engine, em, dt)  GAME code runs here (16 systems)
      body-part position sync      engine generic feature
    Engine::render()
      AnimationSystem::update()
      TileMapRenderer::render()
      RenderSystem::render()
```

**Build-time enforcement:** `engine` and `game-systems` are separate static libraries.
If engine code accidentally includes a game header, the build breaks.

**Runtime paths** (`"assets/..."`, `"config/..."`) are relative to the executable, not
the source tree. CMake syncs `game/assets/` and `game/config/` to `build/bin/` on every
build.

---

## Passive Leveling

**Deferred — design intent only. Not scheduled for implementation yet.**

- Additive on top of the main level-up system
- Actions grow relevant stats via micro-XP (Oblivion-inspired): frequent sword swings
  nudge STR/DEX, taking hits nudges END, etc.
- Soft cap required — passive gains must not break build identity or upset balance
- Implementation approach TBD
