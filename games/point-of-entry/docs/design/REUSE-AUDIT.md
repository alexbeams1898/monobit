# What transfers from prison-escape-game

> **Read this before writing any system.** Prison-escape is a **reference
> implementation, not a parent** — nothing is forked. Each system below gets
> ported deliberately, when Point of Entry actually needs it, and reshaped on the
> way in.
>
> Audited from the source, not from memory. Line counts are the prison-escape
> file's size, as a rough cost signal.

## The headline

**The engine already carries the hard part.** `engines/engine/` provides
rendering, animation, audio, camera, **collision**, **flow-field pathfinding**,
**steering**, tilemap + renderer, sprites, fonts, UI and physics — shared by every
game in the workspace. Prison-escape's own `main.cpp` is **207 lines**, which is
the proof of how thin a game layer can be.

So the question is never "can I build this" — it is "which of these already-solved
problems do I want in the same shape?"

## Ports nearly verbatim

These solve the same problem Point of Entry has, with no design conflict.

| System | Lines | Note |
|---|---|---|
| **ChaseSystem** | 623 | **The most valuable thing here.** Flow-field steering: O(1) per enemy per frame, scales past 1000 enemies, routes around walls for free. This IS the Vampire-Survivors swarm requirement, already solved. |
| **AggroSystem** | ~60 | Idle → Chase on radius. Trivial, correct. (No de-aggro implemented — fine, and arguably right for a swarm.) |
| **ProjectileSystem** | ~100 | Moves projectiles, checks wall overlap. Direction-agnostic. |
| **PickupSystem** | 237 | Loot on the floor → into the bag. |
| **ParticleSystem** | ~150 | Gore, dust, spray. Wanted immediately. |
| **TintSystem** | ~80 | Hit-flash. Cheap game-feel, high value. |
| **DamageSystem** | 401 | Damage application, resistances, i-frames. |
| **DeathSystem** | 399 | Entity death, drops, cleanup. |
| **MovementSystem** | 402 | Integration + collision response. |
| **AnimStateSystem** | ~130 | Sprite state machine. Will simplify — this game is 2-direction. |
| **AmbientSoundSystem** | ~90 | Room tone. Free atmosphere. |

## Ports with real reshaping

The problem is the same; the shape Point of Entry wants is different.

### CombatSystem (1022 lines) — port the skeleton, not the body

Prison-escape's combat is **melee-hitbox-first**, with auto-aim-at-nearest bolted
on as a mode. Point of Entry is **mouse-aimed projectiles first**. So:

- **Keep:** the frame ordering (hitboxes spawn before collision so hits land the
  same frame), cooldown ticking, dodge-roll + i-frames, `spawnProjectile(owner,
  ox, oy, dirX, dirY, weapon, damage, hand)` — it already takes an **arbitrary
  direction vector**, so firing at a cursor is *passing a different vector*, not
  new code. Spread/angle handling exists too.
- **Drop:** the melee hitbox lifecycle, weapon-swing states, lock-on, and the
  auto-attack mode (its "aim at nearest" is replaced by "aim at cursor").
- **Net:** this is the biggest single file and the one most worth rewriting
  rather than porting. Take the ideas and the ordering; leave the melee.

### SpawnerSystem → **the Point of Entry**

Prison-escape spawns enemies *around the player*, just outside the camera, on a
timer. Point of Entry spawns them **from a fixed thing in the world that can be
found and sealed**.

That is a genuinely different spawner — and it is the game's core mechanic, so it
should be written fresh with the POE as a first-class entity (position, tier,
flow rate, seal state, and "what it opens onto"). The existing file is worth
reading for the timing/budget logic and nothing else.

### WaveSystem (548 lines) — probably NOT wanted

Prison-escape is wave-based: clear a wave, advance a phase, next wave. Point of
Entry's floor goal is **hunt and seal**, not survive N waves. The pressure comes
from POEs flowing until closed.

Read it for the difficulty-scaling formulas (`generateWave` from rules is a good
pattern), but the state machine is the wrong shape. **Do not port by default.**

### LevelingSystem (288) / WeaponXPSystem — reshape to the two currencies

The bones are right; the economy differs. Point of Entry has **extermination
points** (skill → levels) and **dollars** (material wealth → the hardware store),
kept separate. Port the level-up plumbing, rewrite what feeds it.

### EquipmentSystem (439) / InventoryOps / CraftingOps

All fine as machinery, all full of prison-specific content. Port the ops, leave
every item.

### RestSpotSystem (181) → the bonfire

Currently a heal-on-touch stub with a cooldown. Point of Entry needs it to also
**level up** and **teleport to the surface**. Small extension of an existing idea
— and the fact it exists at all means the concept was already there.

### TileMapLoader + Room (ASCII templates) → the gadget

Prison-escape generates floors from **ASCII room templates** (`config/rooms/*.room`)
at 80×60 growing to 160×120 tiles. That authoring format is exactly what Point of
Entry wants — **hand-authored pieces, assembled procedurally.**

The change is *when* and *why* it runs: generation is triggered by **using the
gadget**, happens **once per space**, and the result **persists**. Prison-escape
regenerates per level entry. See PITCH.md → "The gadget".

## Leave behind

- **All content** — rooms, items, enemies, weapons, crafting recipes, loot
  tables. ~1000 files of prison fiction.
- **Screens** — HighScores, RunSummary, and the run-based framing generally. Point
  of Entry is persistent, not run-scored.
- **The scoring system** — prison-escape computes a score from `RunStats` at run
  end. Point of Entry's equivalent (extermination points) is a persistent
  currency, not an end-of-run tally.
- **LadderSystem** — descent between fixed floors. Point of Entry has no ladders:
  a sealed POE is the way onward.
- **UpdateChecker, AppearanceOps** — unrelated.

## Worth stealing from elsewhere in the workspace

- **`wayworn-hush`**: `unlock::Condition` (the one gate primitive — clauses OR,
  fields AND, gates on flags/stats/items/time), the JSON-config loader pattern,
  the systems-index doc discipline, and `SpiritHud` (a souls counter that climbs
  toward what has been *announced*) — Point of Entry wants exactly that counter.
- **`engines/engine/`**: everything, by default.

## The honest summary

Roughly **half** of prison-escape's game layer transfers with light editing, most
of the rest is a useful reference for a fresh implementation, and the content is
all discarded. The two things that are genuinely, immediately valuable:

1. **Flow-field chase at 1000+ enemies.** The swarm is solved.
2. **ASCII room templates + procedural assembly.** The floor pipeline is solved,
   and the gadget gives it a fiction.

Everything else is a week each, and better written knowing what this game is.
