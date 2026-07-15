# Wayworn Hush — Actions

> **Owns:** the **action menu** — the options you're offered *after observing a
> spot*, each of which can unlock further observation/thought (and is the way back
> to a thought you missed). A dialogue tree, but with a *place* instead of a
> person.
>
> **Status:** design proposal. Nothing built yet. Design before implementing —
> actions are the second half of the core loop (observe → act → observe deeper).
> Depends on the cognition engine ([PROCESSING-MODEL.md](PROCESSING-MODEL.md))
> which is built.

## tldr

You observe a spot and read what's there. Beneath the reading, a short menu of
**actions** — *search the base, clear the moss, sit with it a while* — like a
Disco Elysium dialogue tree, but you're "conversing" with the stone. Taking an
action changes the world (sets a memory/flag), and the ambient cognition engine
re-checks: a deeper reading or a thought that was closed can now open. **This is
how a missed thought comes back** — not by grinding the spot, but by *doing
something* that changes your relationship to it.

Actions come from **defaults by kind, with per-spot overrides**: an "inanimate
object" affords a default set (Search, Touch, Move); a specific stone can add,
remove, or replace options for its bespoke moment. Concise to author (declare a
kind, inherit its actions), expressive where it matters (override for the
handwritten beat).

**No fail-state.** An action never punishes. It either opens something or simply
isn't offered. Missing is never a wall — only a *not yet*, and the action is the
way through.

## Why actions exist (the fiction)

Observing alone is passive — you read what your acuity affords. But some things
don't yield to looking: the dark hollow needs light, the thing in the water needs
reaching, the mossed stone needs clearing. **An action is an act of engagement
that changes what you can perceive there.** In this game, *doing is a form of
noticing* — you don't act to win (there's no winning), you act to *see*, and
seeing is the whole game.

This makes the core loop a cycle, not a line:

```
observe a spot ─▶ read what's there ─▶ [action menu] ─▶ take an action
      ▲                                                        │
      └──────────  the spot is now different  ◀────────────────┘
                   (a deeper reading / a closed thought opens)
```

## What an action is (the model)

An **action** is a menu option on an observable. Its shape is grounded in how
doing a thing works in real life — a deed *speaks in the moment*, *changes the
world*, and is either done-forever or something you can always do again:

- **label** — the deed as the player sees it (*"Clear the moss," "Taste the
  water," "Sit with it a while"*).
- **`unlock_when`** — the same primitive as everything else (see
  PROCESSING-MODEL). The option is *shown* only when its condition holds — so
  capability, memory, and prior actions gate which deeds are available. A deed you
  can't yet do simply isn't offered (never a greyed-out tease).
- **`result_text`** — what *doing it feels like*, surfaced immediately (*"The moss
  comes away in a wet sheet."*). Real acts have their own voice even when nothing
  important was under them; the deed is content in itself, separate from anything
  it unlocks.
- **`set_flag`** — the **world-state** the deed changes (`moss_cleared`,
  `tasted_spring`). That state flows into the ambient engine exactly like
  observing a spot or a thought firing: the trigger index re-checks everything
  keyed on it. No bespoke "this action unlocks that thought" wiring — the deed
  sets state, and `unlock_when` everywhere else does the rest. One uniform
  mechanism (this is what recovers a missed thought).
- **`one_shot`** — real deeds split two ways: *irreversible* (clear the moss once
  — it leaves the menu, done) vs *repeatable* (sit with a place, wait, return —
  always offered, may surface its `result_text` again). One boolean.

Richer effects (grant/consume an inventory item — the canon's "craft a key" beat;
advance time for "return at night") are deferred until the gather/craft and time
systems exist; an action's effect is a flag today.

Actions are **not** owned globally; they're presented *on a spot*, sourced as:

### Defaults by kind + per-spot overrides

Every observable has a **kind** (inanimate object, water, plant, structure,
remains, …). Each kind declares a **default action set** — the deeds that make
sense for that sort of thing:

| kind (example) | default actions (example) |
|---|---|
| inanimate object | Search · Touch · Move |
| water | Taste · Reach in · Follow |
| plant | Touch · Gather · Smell |
| remains | Sit with · Search · Bury |

A specific observable then **inherits** its kind's defaults, and may **override**:
add a bespoke action, remove one that doesn't fit, or replace an option's
`unlock_when`/effect. Authoring stays concise (most spots just name a kind);
bespoke DE moments are a small override, not a from-scratch tree.

*(The exact default sets per kind are content, authored in config alongside the
observables — not fixed in code. Kinds and their defaults are data.)*

## Actions and the missed thought (fail = a "not yet")

A thought is a roll (PROCESSING-MODEL §Layer 2). A miss is **not** a punishment
and **not** a dead end — the miss surfaces its `miss_text` (the "something you
can't place" almost), costs nothing, and the thought stays reachable. The ambient
engine already re-checks a missed thought whenever a relevant input changes.

**Actions are the deliberate way to change that input.** A thought you couldn't
land is waiting on something — and the action menu is where you *do* that
something: taste the spring, come back having cleared the moss, sit long enough.
The miss told you *there's more here*; the action is *how you reach it*. Failing
is generative — it points you toward a deed, and landing the thought *after* the
deed is the uplift the miss set up.

This keeps the tone on-brand (melancholic-uplifting, patient) — the world is
never closed to you, only waiting for you to engage differently.

## Actions and the glimmer (faculty-hued signal)

*(Cross-cuts the glimmer redesign — see PROCESSING-MODEL §signals. Summarized
here because actions drive it.)*

When a spot has a **reachable thought**, the glimmer should read in the **hue of
the faculty** that thought rolls on — so the world is legible through your
faculties (a perception-hued spot invites your Perception; a reason-hued spot,
your Reason). Multiple reachable thoughts of different faculties → the hues
**blend/cycle**. This replaces the current single-brightness signal, which
collapses distinct states (available-now vs out-of-reach) onto one pulse.

An action becoming available (or a thought re-opening after a deed) is what turns
a spot's glimmer to its faculty hue — so the signal and the menu are one system:
*color says which part of you could engage; the menu is how you do.*

## Resolved (the data model)

- **Effect** = `set_flag` + optional `result_text` (the deed's own voice), no
  richer effects yet.
- **Repeat** = per-action `one_shot` boolean (irreversible vs always-offered).
- **Source** = defaults by `kind` + per-spot `add`/`remove`/`replace` overrides;
  kinds + their default action sets are data (config), not hardcoded.
- **Config home** = `config/actions.json` holds `action_kinds` (each kind → its
  default action list). Observables carry a `kind` name (+ optional overrides) in
  `observations.json`. Separate authored surface from spots/thoughts.
- **Resolution** = merged **once at load** into a `std::vector<Action>` stored on
  the `Observable` (kind defaults + `add`/`remove`/`replace`), the same
  compute-once pattern as tiers. The menu just reads the resolved list.
- **Taken-state** = which one-shot actions have fired lives in the record
  (`State`), like `fired` thoughts and `flags` — the taken set survives save. It
  is keyed **per spot** (a spot-scoped key), so a deed id shared across spots via
  kind defaults is tracked independently — taking it at one spot never marks it
  taken at another.

### Config shape

```json
// config/actions.json
"action_kinds": {
  "inanimate": { "actions": [
    { "id": "search", "label": "Search around it",
      "result_text": "Nothing but grit and old roots.", "one_shot": true },
    { "id": "touch",  "label": "Rest a hand on it",
      "result_text": "Cold. It has been cold a long time.", "one_shot": false }
  ]},
  "water":   { "actions": [ /* taste, follow, ... */ ] },
  "remains": { "actions": [ /* sit_with, search, ... */ ] }
}
```
```json
// config/observations.json -- an observable references a kind + optional overrides
{ "id": "stone", "kind": "inanimate", "value": 2, "tiers": [ ... ],
  "actions": {
    "add":     [ { "id": "clear_moss", "label": "Clear the moss",
                   "unlock_when": [{ "stat": { "perception": 3 } }],
                   "result_text": "The moss comes away in a wet sheet.",
                   "set_flag": "stone_moss_cleared", "one_shot": true } ],
    "remove":  [ "touch" ],
    "replace": [ { "id": "search", "result_text": "..." } ]
  }
}
```

### The engine path

`takeAction(state, growth, spotId, actionId, rng)`:
1. surface the action's `result_text` as a plain `PendingLine` (the deed's voice);
2. if `one_shot`, record it (per-spot) in the taken set (so it drops off the menu);
3. `set_flag` (if any) into `state.flags`;
4. run the **existing** ambient engine over that flag key → tiers/thoughts
   re-check → a **missed thought can fire now**. Reuses `runEngine` wholesale.

A deed may also declare **item effects** — granting an item or rolling a gather
result into the satchel, and optionally consuming its spot (removing it from the
world). The observation layer stays inventory-ignorant: it reports these as ids
for the game layer to enact. This is what makes a "cache" or a "yield" just a deed
on an observation spot — the drop and the deed are one system.

An action's `unlock_when` decides whether it's *offered*; a resolved action list
filters to offered-and-not-taken(one_shot) when the menu opens.

## Open questions (to resolve before / during build)

- **The kinds taxonomy** — the actual list of observable kinds and each one's
  default action set. Content; needs a first pass (start small: `inanimate`,
  `water`, `plant`, `remains`).
- **Menu UX** — how the action menu presents (list under the reading? a radial?),
  input, and how it reads in the melancholy register (unhurried, quiet).
- **Actions that need an item** — the canon's "craft a key (lantern, rod) → observe"
  beat (GAME-SYSTEMS §2). Deferred until the gather/craft system exists.
- **Faculty-hue blend/cycle** — exact behavior when several faculties are in play
  at one spot (part of the glimmer redesign).

## Cross-references

[PROCESSING-MODEL.md](PROCESSING-MODEL.md) (cognition engine, thoughts, signals) ·
[OBSERVATION-SYSTEM.md](OBSERVATION-SYSTEM.md) (the observe verb) ·
[GAME-SYSTEMS.md](GAME-SYSTEMS.md) (gather/craft, the wider loop) ·
[THEME.md](THEME.md) (register)
