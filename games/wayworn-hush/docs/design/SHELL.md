# Wayworn Hush — Shell (design direction)

> **Status:** direction, not built. The app around the game: what greets you, what
> persists, what state the program is in. Kept general on purpose — the shape, not
> the content. Specifics (copy, layout, exact fields) are decided when built.

## Why now

Everything the pilgrim becomes — what they've noticed, carried, made, understood
— currently evaporates on exit. The game boots straight into the world with no
way in or out. Every system built from here adds state that a save has to know
about, so the split gets more expensive to retrofit each time.

## Three kinds of state (the load-bearing distinction)

The game's runtime state is really three things wearing one hat. Naming them is
most of the design:

- **Authored config** — loaded from JSON every run, identical every time, the
  same for every player. Never saved.
- **Progression** — what this playthrough has become: what's been noticed,
  understood, carried, made, recorded; how deep the self has grown; where the
  pilgrim stands; what time it is. **This is the save.**
- **Ephemeral** — live-frame bookkeeping (entities, cursors, timers, edge-detect
  flags). Rebuilt on load, saved never.

Every future system must declare which of the three it is. A system that can't
answer that is a system whose data model isn't finished.

## Persistence

**Named pilgrims are the slots.** Starting anew asks who you are; the greeting
lists who you've been. Each pilgrim keeps their own continuous walk — there is no
manual save verb and no slot management: within a pilgrim, the game keeps your
progress and you return to it. It writes itself at natural moments and on
leaving.

A pilgrim's record holds **progression + where they stand**, so returning resumes
the walk in place rather than restarting it with your things.

**A pilgrim is keyed by a stable identity, never by their name.** The name is
what's *shown*; it is not what they *are*. This is deliberate and diverges from
the studio's prior games, which key by name and pay for it: two pilgrims may
share a name without colliding, deleting one cannot take the other with it, a
lookup cannot silently find the wrong one, and renaming is free rather than a
trap that must update a key in lockstep or lose the save.

**A record must distinguish "never walked" from "walked, and got nowhere."** A
fresh pilgrim has no place to resume to, which is not the same as resuming at the
world's origin.

**Versioning is a first-class concern from day one.** The file carries a schema
version; loading an older one migrates it forward, and migration runs *after*
read, never during. Reads are tolerant of missing fields (an absent key takes its
default), which makes additive change free — the common case.

## What is a pilgrim's, and what is the game's

Progression belongs to a pilgrim. Things that are true of the *installation* —
preferences, and anything the player would be annoyed to re-answer per pilgrim —
sit beside the roster, not inside each record. Every new piece of saved state
must say which it is.

## App state

A single **phase** says what the program is doing (greeting / playing), with
in-game overlays layered on top of play rather than replacing it. It is a plain
value, not a scene hierarchy: transitions are assignments, and only the phase
gates whether the world ticks.

The world is not built until the player commits to entering it — greeting a
player should not require a loaded region.

## Reuse, not reinvention

The studio has solved the file half of this twice (once per prior game), each
time by copying it. The generic parts — resolving a platform save directory,
reading/writing a JSON document, the migrate-after-load hook — are **engine
concerns** and belong there once, with each game owning only its own save *shape*
and its own migration. Menus and screens stay game-side per engine doctrine.

Wayworn's on-screen surfaces reuse the register and primitives already built for
the pause page (soft overlay, shadow-text, hover/confirm parity between mouse and
keys) — the greeting is not a new visual language.

## Open threads (decided when built)

- What "natural moments" means for autosave, and how (or whether) it's surfaced.
- Whether settings (audio, display) become real and persisted — nothing in the
  studio's prior art does this today; it would be greenfield.
- Whether the greeting carries anything beyond the minimum (title, continue,
  begin, leave).
- Where the crash reporter's output belongs once a save directory exists.
