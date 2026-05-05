# PC vs NPC

> **Owns:** the design-level expression of the entity-symmetry rule.
> What the player does that NPCs don't, what NPCs do that the player
> doesn't, and how the symmetry shows up in design.
> **Status:** structural locks; per-system implementation detail TBD.

## The constraint

The engine treats every active actor — player, shades, NPCs, keepers,
Lucifer, Beatrice, bullets — as the same `Entity` struct, in one
pool, processed by one update/draw loop. Differences are *data*
(stats, sprite_id, AI tag), not call-site branches. There is no
`Player` class; there is no `if (is_player)`. The Vagrant is an
entity with the input-controller AI tag; a shade is an entity with
a wander-and-attack AI tag; the player and the shade share their
collision math, damage system, death animation, sangue payout
mechanic.

This is not just an engineering rule — it is a **design rule**. The
Vagrant is *not special* in the world. He has the same stats as the
beings around him. A boss is an enemy with bigger numbers. The
player's class is a stat profile, not a privileged code path.

## What only the PC does

These are functions that, by their nature, only one entity can
perform. They are still expressed as data on that entity, not as
type:

- **Input control.** The PC's AI tag reads input state instead of
  running scripted behavior. Other entities have AI tags that read
  the world (player position, distance, line-of-sight, pattern
  state). Same pool, different tag.
- **Camera anchor.** The camera follows the entity with the
  player-controlled tag. If the player-controlled tag were on a
  different entity, the camera would follow that one. (This is
  expressible by data; no special PC code path.)
- **Save / vestigium targeting.** Vestigia preserve the player-
  controlled entity's persistent state (class, stats, lifetime
  sangue, etc.). Other entities have their own state (encounter-
  history, lucid-window-state) which is also persisted, but the
  *vestigium* is anchored to the player-controlled entity.
- **Dialogue trigger.** Conversation initiation routes through the
  player-controlled entity (proximity check + button press). NPCs
  respond; they don't initiate.
- **Death = run-end.** The player-controlled entity's death triggers
  the run-end cutscene + death-card + run-stats screen. Other
  entities' deaths trigger sangue payout and per-circle reactivity
  (NPC turning hostile, contrapasso leak, etc.).

## What only NPCs do

- **Wander / pursue / attack patterns.** AI tags for combat
  behavior. The player-controlled entity reads input instead.
- **Lucid windows.** NPCs (one per circle, per setting.md *NPCs*)
  have a lucid window during their pre-keeper presence. The Vagrant
  has no lucid window — he is hollow throughout.
- **State-aware dialogue branching.** NPCs perceive the Vagrant's
  state (path, evolution, riversato, lifetime sangue, keepers
  felled) and branch on it. The Vagrant himself does not branch —
  he has no internal state reading the world.
- **Carry contrapasso leak.** When a circle's keeper falls,
  contrapasso routes into the surviving NPC and shades, plus the
  class-picker Vagrant, plus the Guide. The *Vagrant* receives leak
  too (under class-picker), but only as a passive accretion to his
  imprint — he doesn't *carry* it the way an NPC does.

## What both do identically

- **Combat.** Damage, fire_rate, HP. No PC-only stat, no NPC-only
  stat. The triad (HP / fire_rate / damage) governs both.
- **Sangue payout on death.** The Vagrant's wallet sangue returns to
  Hell on his death (per *Senza Forma* — Hell reclaims its substance).
  Shades and keepers, when killed, deposit sangue into the Vagrant's
  wallet. Same mechanic, different direction.
- **Sprite rendering.** Same draw loop. Same sprite system. Same
  bitmap-based animation.
- **Position / movement.** Same world-coordinate system, same
  collision detection.
- **Second death (cosmologically).** Per *Senza Forma*: the Vagrant
  cannot *receive* second death (no imprint). Shades and keepers can
  and do receive it (every kill the Vagrant grants is the soul's
  resolution). The mechanic of *being killed* is the same; the
  cosmological outcome differs because of imprint state.

## The Guide as companion edge case

The Guide is an NPC that:
- Appears in the Wood (the hub) consistently across runs.
- Appears in Hell as a projection at per-keeper interludes.
- Provides services (heal, OFFERINGS).
- Eventually fights the Vagrant (class-picker climax) or hands over
  the Hand (unburdened climax).

He is *not a party member*. He does not follow the Vagrant in combat.
He is a stationary or interlude NPC, mechanically. The "companion"
framing is narrative, not mechanical: he is the *dialogue-volume
center* of the game, not a controllable ally.

His Hell-projection appearances are entities in the same pool,
spawning at interlude triggers. His Wood-side body is an entity in
the Wood scene. Both share the Entity struct; they differ in AI tag,
state, and per-cycle degradation flag.

## Beatrice as boss edge case

Beatrice, when she appears at R4 Branches B and C (PURITY and
REFUSAL), is a boss entity. Same Entity struct as a keeper, with
combat profile data appropriate to her (rabid, light-based,
disintegrating in real-time).

She has no dialogue tree the player navigates — her "speech" during
combat is fragmentary, single-line emissions tied to attack patterns
(deferred to per-encounter design). She does not have a lucid window.
She is not state-aware in the NPC sense.

## Open questions

- **Targeting precedence.** When the Vagrant is hostile to multiple
  entities (post-keeper NPCs, shades, keepers), how does targeting
  prioritize? Defer to gameplay design.
- **Whether the Vagrant can target NPCs in their lucid window.** Per
  setting.md, NPCs become hostile only post-keeper (lucid window
  closes). Pre-keeper, can the player attack them anyway? Defer.
- **Bullet symmetry.** All bullets are entities. Some bullets belong
  to the player; some to enemies. Friendly-fire rules: defer.
- **Whether the Hand's effects (heal, OFFERINGS-anywhere,
  riversamento-anywhere) need a special trigger or use the same
  entity-interaction pattern as other in-world objects.** Defer to UX.

## Cross-references

- [Classes](classes.md) — class is a stat profile; same triad applies
  to enemies and bosses.
- [Companions](companions.md) — the Guide as edge case.
- [Setting](setting.md) — *Senza Forma* (asymmetric second-death
  receivability), *NPCs* (lucid window mechanic), *Per-circle
  reactivity* (contrapasso leak distribution).
- [Fallback](fallback.md) — death = run-end (player-controlled
  entity only).
