# Wayworn Hush — Interaction Model

> How the player acts on the world. General shape; feel values live in config.

## One verb, resolved by stance

Interacting with the world is a single input. What it *does* is resolved by the
player's **movement stance** — the stance is the verb:

- **Walking → observe.** Interacting examines the thing (surfaces its reading).
  Slow down to notice.
- **Running → act.** Interacting opens what you can *do* at the thing (its deed
  menu). Move with intent to do.

The stance is not a separate mode to toggle — it *is* how you're already moving.
A persistent on-screen cue shows the current stance, and a distinct sound marks
each transition, so the verb an interact will perform is always known.

Drawn from the console-RPG lineage (one context-driven button), adapted so the
context is the player's own stance rather than a facing/target check.

## Items are immediate

Things lying in the world (pickups, gather nodes) are not observed — they are
taken directly on interact, regardless of stance. They carry their own visual
cue. Only observable *spots* are governed by the stance.

## Targeting

Each frame the system resolves the single active target — the nearest thing within
reach, or the one under the cursor (cursor overrides where you stand, both gated to
reach). Only the active target is cued and can fire. Interacting fires on the key
or a click aimed at the active target.

## Where the substance lives

The interaction layer stays generic — it resolves *what* is targeted and *which
stance*, then hands off. Observing runs the reading system; acting runs the deed
system; taking an item runs inventory. The meaning lives in those systems, not in
the routing. Adding a new kind of thing to act on is a new capability, not a new
interaction system.

## Related

- [OBSERVATION-SYSTEM.md](OBSERVATION-SYSTEM.md) — the observe verb + its cue.
- [ACTIONS.md](ACTIONS.md) — deeds (what "act" surfaces), including item-granting
  deeds (drops).
