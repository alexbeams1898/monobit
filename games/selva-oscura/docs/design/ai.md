# AI

> **Owns:** the architecture of enemy decision-making — perception,
> scheduling, behavior, action selection, and what intelligence *feels*
> like in this game.
> **Status:** drafting (perception + scheduler shipped; behavior tree
> + action data + reactive re-decision in progress)

## Scope

This doc covers **how enemies decide what to do.** It does not own:

- Per-enemy archetype design — see [`bestiary.md`](bestiary.md). That
  doc names *who* the enemies are; this doc owns *how* they think.
- Combat mechanics — see [`combat.md`](combat.md). That doc owns the
  player-side combat grammar; this one owns the enemy-side counterpart.
- Pathfinding / world topology — not yet built. When it is, it gets
  its own section here.

The goal of the AI architecture is to make enemies *feel* intelligent
without writing per-enemy code. Adding a new enemy archetype to the
bestiary should mean: drop a JSON file, no C++ changes. The
architecture earns its complexity by paying that bill once.

## The reference model — Souls AI

Souls / Elden Ring enemies look like they "think" because four
disciplined systems work together:

1. **A behavior tree, not a state machine.** A state machine's edges
   grow O(states²). A behavior tree's depth grows O(log behaviors).
   Bosses with 30-50 distinct actions are intractable as state
   machines; trivial as trees.

2. **Range-banded action selection.** Every action declares a
   preferred distance (jump-attack at 4-8m, slash at 1.5-3m, strafe
   at 0-1.5m). The tree picks an action whose range matches the
   current player distance. Players read "the boss adapted" but it's
   just data lookup.

3. **Recovery-window re-decision.** Every animation has a frame-tagged
   "AI thinking window" — usually at the tail of recovery. The boss
   commits to a 2-second slash; the moment it enters recovery, the
   tree re-evaluates. From outside this looks like reactive parry;
   internally it's "every 0.4–1.2s, ask the tree what to do."

4. **Weighted-random within the legal set, plus cooldowns.** Each
   action has a per-action cooldown. The tree, given range + cooldown
   filtering, picks weighted-random from what remains. Two players
   fighting the same boss see slightly different sequences — reads
   as "the boss is adapting" but it's just shuffling.

A fifth pillar — **perception is a separate layer.** The AI never
reads world state directly. It reads a *perceived state* the perception
system populates. This is what makes stealth and dis/engagement work.

Our architecture mirrors this five-piece design.

## Architecture overview

```
                                          per render frame
┌──────────────────────────────────────────────────────────┐
│ tickPerception()  (60Hz)                                 │
│   read world state → write actor.perception              │
│   • cone-cast vision                                     │
│   • last_seen_time, last_known_player_pos                │
│   • awareness state machine                              │
└──────────────────────────────────────────────────────────┘
                       │
                       ▼
┌──────────────────────────────────────────────────────────┐
│ shouldTickAi()  (gate — fires ~10Hz, 1.5× in Combat)     │
│   wallclock-based scheduling                             │
│   per-actor next_ai_tick_time + jitter                   │
└──────────────────────────────────────────────────────────┘
                       │ when true:
                       ▼
┌──────────────────────────────────────────────────────────┐  ◀── future
│ Behavior tree evaluation  (Sprint 4)                     │
│   leaves: pick action by range, fire one-shot, set       │
│   cooldowns, set locomotion intent                       │
│   tree composition: JSON per archetype                   │
└──────────────────────────────────────────────────────────┘
                       │
                       ▼
┌──────────────────────────────────────────────────────────┐  ◀── future
│ Locomotion intent + clip-firing (existing systems)       │
│   actor.velocity_xz, sampler.playOneShot                 │
└──────────────────────────────────────────────────────────┘
```

The split between 60Hz (perception) and 10Hz (decisions) is the
single most important architectural choice. It means:

- Perception stays responsive to fast player movement.
- Decision logic is cheap enough to scale to dozens of enemies.
- Each layer can be tuned, traced, and replaced independently.

## Perception (Sprint 1 — shipped)

### Awareness state machine

```
Unaware    ─[saw player]──────────────────►  Suspicious
Suspicious ─[N confirmed sightings]───────►  Alerted
Suspicious ─[no sighting for Ts]──────────►  Unaware
Alerted    ─[in engage_range + visible]───►  Combat
Alerted    ─[no contact for Ta]───────────►  Suspicious
Combat     ─[outside leash_range for Tc]──►  Alerted
```

Four levels. Each transition has a tunable threshold. The
**Suspicious → Alerted gating** is what gives Souls enemies their
"double-take" feel: they notice you, hesitate for a few frames,
then commit. Without it, every enemy snap-aggros on first sight.

**Combat retention is distance-based, not vision-based.** Once an
enemy enters Combat (via vision OR getting hit OR any future
trigger), they "remember" the player exists and track the player's
world position every frame regardless of LOS. Combat decays back
to Alerted only when the player has been continuously outside
`combat_leash_range` for `combat_disengage_seconds`. This matches
Souls / Elden Ring convention — the leash is what determines
disengage, not vision. Vision-based retention has a critical
failure mode (player runs around behind the enemy, breaks LOS, the
enemy forgets they exist) that the leash model avoids by design.

While in Combat, `last_known_player_pos` is overwritten every
frame with the player's actual position. This is what makes
"the enemy follows you around walls" work in Souls and is the
reason an enemy that was knocked-down stays engaged with the
player after recovery.

The vision cone is **engagement-only** (Unaware → Suspicious →
Alerted → Combat). After Combat, it serves as a freshness gate
for the action picker (don't swing at stale data; see Sprint 4
*Behavior tree*) but doesn't drive state retention.

### Vision model

- Cone-cast forward from actor position, centered on `actor.yaw`
  (convention: yaw=0 faces −Z).
- XZ only — pitch doesn't factor in for ground combat.
- FOV (default 90°) and range (default 12m) are tunable per
  archetype (Sprint 3+) and globally for testing.
- Math is `dot(forward, to_target_normalized) >= cos(FOV/2)` —
  one trig pair per actor per frame, allocation-free.
- **No occlusion test yet.** When level geometry has interior walls
  blocking sight, we add a ray vs. world-collider check. For now
  (open clearing), every target inside the cone is visible.

### Hearing

Reserved in the schema but not wired in Sprint 1. Sound events
(player sprint, dodge land, attack swing) will emit world-space
events with a radius; perception will check actor distance against
that radius on its next tick. Adds the "search for missing player"
behavior that Sekiro stealth relies on.

### What perception writes

```
struct PerceptionState
{
    Awareness awareness;                  // 4-state enum
    float     last_seen_time;             // wallclock — vision-only
    glm::vec3 last_known_player_pos;      // overwritten in Combat
    float     awareness_entered_time;     // drives Suspicious/Alerted decay
    int       suspicious_sighting_count;  // gates Suspicious→Alerted
    float     outside_leash_since;        // Combat→Alerted hysteresis
};
```

Consumers:
- Behavior tree branches on `awareness`.
- LeafMoveToTarget reads `last_known_player_pos` to face / approach.
- LeafPickAction's freshness gate reads `last_seen_time` — actions
  only fire when vision is fresh, regardless of awareness state.

## Scheduler (Sprint 2 — shipped)

### Why throttle decisions

Perception is event-reactive — a 100ms lag between "player crossed
into cone" and "enemy registers" feels laggy. **Decision-making is
not.** A 100ms lag between "decided to swing" and "swing fires" is
invisible to the player and saves enormous work.

Souls runs decisions at ~10Hz. We do the same.

### Scheduling rule

```
shouldTickAi(actor) = wallClock() >= actor.next_ai_tick_time
on fire: actor.next_ai_tick_time = wallClock() + 1/hz + jitter
```

- `hz` = `ai_decision_tick_hz` (default 10).
- In Combat awareness, `hz *= ai_decision_tick_combat_hz_multiplier`
  (default 1.5×). Alerted enemies think faster.
- Jitter is ±10% of the period. Prevents accidental re-synchronization
  over hours of play.

### Spawn-time phase offset

If 30 enemies spawn on the same frame, they'd all schedule for
`now + 0.1s` and all tick on the same frame — defeating the
load-spreading purpose.

`seedAiTickPhase(actor, pool_index)` staggers the first tick across
the decision-tick window. Index 0 ticks at +0, index 1 at +1/N, etc.
After that, jitter handles ongoing drift.

### Cost

One float compare + one branch per actor per render frame for the
gate. Negligible. The savings show up later when behavior tree
traversal is the work being gated.

## Behavior tree (Sprint 4 — planned)

Standard three-node toolkit:

- **Selector** ("try children in order, succeed if any succeed") —
  the classic priority list.
- **Sequence** ("run children in order, fail if any fail") —
  conditions before actions.
- **Leaf** (the actual work) — typed: `LeafPickAction`,
  `LeafExecuteAction`, `LeafMoveTo`, `LeafSearch`, `LeafIdle`.

Trees are **JSON, not code.** Adding an enemy means writing JSON,
not C++. The runner is generic.

### Condition leaves

`IfAwarenessAtLeast`, `IfPlayerInRange`, `IfActionOffCooldown`,
`IfHealthBelow`. Composable as Sequence prefixes.

### The action selection leaf

`LeafPickAction` is where the Souls-feel illusion lives:

1. Filter actor's action list to those whose `range_min..range_max`
   contains the current player distance.
2. Filter to those off cooldown.
3. Weighted-random within the remaining set.
4. Fire via `LeafExecuteAction` (which calls `sampler.playOneShot`,
   sets the action's cooldown, sets the cancel-window).

No hard-coded action sequences. The boss's apparent intelligence is
emergent from range bands + cooldowns + weighted choice.

## Action data schema (Sprint 3 — next)

Per-action JSON declares everything the runtime needs:

```json
{
  "id": "shade_swing",
  "clip": "jab",
  "range_min": 0.0,
  "range_max": 1.8,
  "cooldown_seconds": 1.5,
  "weight": 1.0,
  "min_awareness": "Combat",
  "poise_damage": 12
}
```

Per-archetype JSON binds an action list and a tree:

```json
{
  "id": "limbo_shade",
  "actions": ["shade_swing", "shade_approach", "shade_idle"],
  "tree": "trees/humanoid_basic.json",
  "perception": {
    "vision_fov_degrees": 60,
    "vision_range_meters": 8.0
  }
}
```

The figura umana rule (see [`bestiary.md`](bestiary.md)) means almost
every humanoid enemy shares the same tree + similar actions; archetype
data is mostly weight and timing tuning, not new actions per enemy.

## Reactive re-decision (Sprint 5 — planned)

Recovery-window re-decision is what makes Souls bosses feel like
they're *watching* you, not running on a script.

Hook into `PoseSampler` one-shot lifecycle: when an actor's one-shot
enters BlendOut (the recovery phase), fire that actor's AI tick
**immediately** rather than waiting for the next scheduled slot.

This produces the "boss reacts the moment its swing ends" feel. The
scheduling math doesn't need to change — we just bypass the gate for
that one frame.

## What we deliberately don't do

- **Group AI / formations.** Enormous complexity for marginal
  player-perceived value. Souls bosses are solo; mob enemies don't
  coordinate. Don't build it until a specific encounter demands it.
- **Tactical evaluation** ("what's my best move given the
  situation"). Looks smart, is brittle and slow. Range-band +
  weighted-random within legal set produces 90% of perceived
  intelligence at 5% of complexity.
- **Pathfinding (real A*).** The selva is an open clearing. Direct
  steer-toward-target + tree-avoidance is enough until we build
  interior spaces.
- **Per-enemy bespoke code.** The architecture is data-driven on
  purpose. Adding an enemy is a JSON file, not a C++ patch.

## Deferred work — known-shape, waiting on content/systems

Items the architecture supports cleanly but that are deferred until
their consumers (a level, a second archetype, a boss, etc.) exist.
None of these are bandaids on the current model; they're real
extensions whose value is zero today and load-bearing later.

- **LOS-aware tracking.** Combat-aware enemies pathfind around walls
  to reach the player when LOS is broken. Requires occlusion ray-
  casting against world colliders AND a real pathfinder (A* / nav
  mesh). The selva is open clearings only; tree push-out gets us
  90% of the way. Lands when interior areas (gate of Hell, Limbo
  proper) ship.

- **Hearing channel.** Sprint sounds, swing sounds, dodge-land
  sounds wake nearby enemies. `PerceptionState.last_heard_time`
  field is reserved. A few hours of work. Lands when stealth
  gameplay matters (Sprint 7+) — building it today would be
  schema-without-consumer.

- **Per-archetype leash override.** Same pattern as the existing
  `vision_fov_degrees` / `vision_range_meters` `std::optional<float>`
  override on `EnemyArchetype`. Adds `combat_leash_range_meters`.
  ~15 lines of code. Lands when a second archetype with different
  leash semantics ships (Cerberus would want shorter, a boss would
  want longer).

- **Return-to-spawn.** When the player out-leashes, the enemy walks
  back toward `spawn_pos` and resumes its patrol. New BT leaf
  `LeafReturnToSpawn`. Pure additive — no perception or scheduler
  change. Lands when first proper patrol-spawn-point gameplay
  ships; in the dev hub there's no visible difference.

- **Faction-attack aggro.** Currently `playEnemyHitReact` aggros on
  any hit but only the player attacks today. Extends naturally to
  ally NPCs / faction conflict. ~5 lines once the second faction
  exists.

- **In-boss change system — TBD.** Selva's bosses may not use Souls-
  style HP-threshold phase transitions. Whatever in-encounter
  behavior shifts happen will be designed lore-first per-boss
  (consequence-driven, not template-driven). Implementation deferred
  until the design lands. Lupa (Beat 2 opening boss) is single-
  encounter / single-behavior for v1 — no phase change. The
  `BehaviorTreeRegistry` already supports multiple trees if/when the
  shift mechanism is designed; today there's no swap-on-trigger
  infra and no urgency to build one. See
  [[selva-wood-lore-locked-2026-05-31]].

## Per-circle considerations

Limbo's enemy is the **Limbo Shade** (see [`setting.md`](setting.md)
*Per-circle reactivity* and [`bestiary.md`](bestiary.md) figura
umana rule). Lore-coherent behavior:

- **Unaware:** standing dignified, no movement, mostly statues.
- **Suspicious:** sleepwalking, slow head-turn toward last-known
  position. Not yet aggressive.
- **Alerted:** approach with confused warrior reflex. Not malice,
  not hunger — *immune response*. The shade is rejecting a foreign
  category, not pursuing prey.
- **Combat:** weak strikes, low frequency, easily interrupted by
  the player's stagger. Bottom of the food chain.

Tone: sad, mechanical, sleepwalking. Not predatory.

Lower circles will use the same architecture with more aggressive
tuning: faster decision tick, higher action weights on attacks,
shorter ranges, no Suspicious→Alerted gating (snap-aggro for
heat-of-sin enemies).

## Diagnostics

Two F1-toggleable overlays (in the F1 panel's Debug section, backed by
`selva::debug::flags()` — session-only, never serialized):

- **`ai_perception`** — draws vision cone on the ground + awareness label
  above each enemy. Colors: gray (Unaware), yellow (Suspicious), orange
  (Alerted), red (Combat).
- **`ai_tick_log`** — emits `[ai-tick]` to `combat-debug.log` each time an
  actor's decision tick fires. Confirms scheduling math.

Off by default at every launch.

## Cross-references

- [`bestiary.md`](bestiary.md) — figura umana rule, enemy roster.
- [`combat.md`](combat.md) — player-side combat grammar.
- [`setting.md`](setting.md) *Per-circle reactivity* — how circles
  behave pre-keeper vs. post-keeper. Once an enemy is *being
  properly punished under the restored keeper's enforcement*, its
  action JSON's weights/cooldowns get overridden by the circle's
  post-keeper modifier; the architecture is the same.
- [`DEV_PILLARS.md`](DEV_PILLARS.md) — design discipline. "Subtract
  before adding" applies hard here: every layer of the architecture
  was justified before being built.
