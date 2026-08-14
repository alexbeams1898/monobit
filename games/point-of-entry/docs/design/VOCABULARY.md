# Point of Entry — the words

**One thing, one word.** This file is the source of truth for what the parts of
this game are called, in code, in config, on disk, in the map, and in the docs.
It is checked by `scripts/check_vocab.py`. Run it, and the other three checks
beside it, before pushing:

```bash
for c in areas design structure vocab; do
  python games/point-of-entry/scripts/check_$c.py || break
done
```

*(This game has no CI job yet — the checks are run by hand. Until it does,
"enforced" means someone remembering, which is exactly the thing the file
exists to stop relying on.)*

The words come from what the game says on screen. Where the trade has a real
term and it is plain enough to read without a glossary, the trade wins — *point
of entry* and *exclusion* are both real, and both were already here.

## The six

| word | is | example |
|---|---|---|
| **descent** | the whole thing under the bar | |
| **floor** | one depth of it | `B2` |
| **room** | one space on a floor: its own map, its own holes | `B2-A` |
| **chamber** | one template stamped into a room's map, ~2 per room | — |
| **hole** *(formally: point of entry)* | a way through | `B2-A-3` |
| **passage** | a hole that is spent | — |

A **pest** is what comes out of a hole. A **species** is which kind it is,
named by its file path. The **field guide** is the book he reads about them.

## Two things that are easy to confuse

**A floor is not a room.** `B2` is the floor; `B2-A`, `B2-B`, `B2-C` are rooms
on it, reached from one another sideways. The tag has always said this; the
code disagreed for a while and cost a day.

**A chamber is not a room.** A room is generated from about two chamber
templates joined by a corridor. The player walks through it as one space and
never hears the word — it exists for the generator and the config, nowhere
else. `rooms_per_floor` and `chamber_limit` count different things.

## A hole's state

Two independent facts, not one:

| its own program | what it is | on the sheet |
|---|---|---|
| never opened | **sealed** | Sealed |
| opened, still running | **open** | Working |
| finished | **spent** — it is a passage | Cleared |

Orthogonally, a passage is **in use** while something is coming through it from
the far side, and a passage in use is not a way anywhere.

## Retired

These are gone. They are permitted **only** in migration code and its test
fixtures, where naming an old file or key correctly would mean naming it wrong.

| retired | say |
|---|---|
| seep, site | hole |
| creature, vermin | pest |
| bestiary | field guide — or *formulas*, where it means the derivation table |
| floorgen | roomgen |
| floor *(meaning a room)* | room |

## Where things live

```
config/descent.json        the descent itself (act boundaries)
config/rooms/*.json        kinds of room: cellar, warren
config/chambers/**/*.chamber   the templates a room is built from
config/holes/*.json        kinds of hole, and what comes out of them
config/pests/*.json        the pests themselves
```
