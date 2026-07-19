# Wayworn Hush — Design

> **Codename working title.** *Wayworn* = worn by long travel; *Hush* = the
> quiet arrived at only by wearing yourself down to it. The state the
> protagonist is looking for without knowing he's looking for it.
>
> **Status:** initial doc — concept notes, not locked design.

## What this is

A 2D 8-16 bit slice-of-life exploration game about solitary travel through
natural landscapes, dedicated to a lost friend.

Melancholic-uplifting register. Short (~10-20 hours). Minimal dialogue.
Mostly the protagonist's inner thoughts + small interactions along the way.
No moral binary, no epic stakes — just walking, camping, gathering, the
occasional weighty encounter, ambient music, and the quiet accumulation
of self-knowledge that only long walking produces.

## Core concept

**Solvitur ambulando** — "solved by walking." The philosophical premise
(attributed variously to Diogenes and St. Augustine) that some things can
only be worked through by moving through space, alone, for long enough.

The protagonist leaves home looking for something. He doesn't fully know
what. As the game progresses, what he's looking for shifts — not because
the goal changed, but because *he* changed. The ending is the return
home carrying the answer. The answer is the change itself.

The point of the game is the traveling, the landscapes, the quiet. Encounters
and skills are seasoning. The map is the meal.

## Tonal references

**Primary:**

- **Mushishi (2005-2006, anime)** — episodic wandering through misty
  landscapes, encounters with quiet supernatural creatures (mushi), the
  protagonist observes and moves on. Small stories, ambient sadness,
  gentle wisdom. This is the closest tonal reference.
- **Earthbound (1994, SNES)** — slice-of-life whimsy, small-town
  Americana turned surreal, ambient towns and calm exploration
  punctuated by strange turn-based encounters. The strategic-combat
  register (dodge windows, PSI variety, back-attacks, sound-battle music)
  is a direct mechanical reference.
- **Pokemon (Gen 1-3, especially Ruby/Sapphire/Emerald)** — the *feel*
  of routes, towns, wild encounters, weather, day/night, camping-adjacent
  moments. The overworld's role as a place you *travel through*, not
  just a menu between fights. Routes-as-experience-in-themselves.

**Secondary:**

- **Chrono Trigger** — 16-bit pastoral moments, ambient weather, the way
  the map itself carries emotion.
- **Undertale** — turn-based combat with strategic depth (dodge patterns,
  ACT commands, non-lethal solutions), minimal-dialogue characterization,
  melancholic-uplifting register.
- **Studio Ghibli films (My Neighbor Totoro, Only Yesterday, Whisper of
  the Heart)** — pastoral melancholy, small everyday moments carrying
  meaning, the quiet of nature as spiritual space.
- **A Short Hike (2019)** — small-scale wandering, no urgency, ambient
  discovery. Different medium/register but shares the philosophy.
- **Journey (2012)** — pilgrimage-as-narrative, minimal dialogue,
  landscape carrying emotion.

## Setting

- **Ambiguous historical period.** Feels older-world, pre-industrial or
  early industrial. No cars, no phones, no shopping malls. Not a
  specific historical setting — deliberately vague, like a folk-tale
  or a memory of the world before it was paved over.
- **Predominantly natural.** Forests, mountains, coastlines, meadows,
  rivers, lakes, marshes, wetlands, high plains, tundra edges,
  woodland clearings. Weather. Time-of-day. Seasonal change.
- **Sparse settlements.** Small towns and villages appear rarely, briefly.
  Not the focus. Passing through. The protagonist stops, talks to a
  couple of people, moves on. Compare Pokemon's small towns — brief,
  homely, present but not dominant.
- **Very little technology.** Whatever exists is pre-modern:
  cooking fires, small oil lanterns, handmade tools, animal-drawn
  transport (if any). Nothing electric.

## Protagonist

- Alone for the majority of the game.
- Male (per current notes; could shift).
- No specific age locked yet. Adult, but young-adult-feel — the age
  where someone leaves home to look for something.
- Minimal spoken dialogue. Most narration is inner monologue — his
  thoughts, observations, questions to himself.
- Encounters other people rarely, briefly, without ceremony.
- Learns about himself through what he sees and how he moves through
  it, not through spoken exchanges.

## Structure — open exploration

**Potentially open-world / open-exploration** — the player can choose
which region to travel through next, in what order, without heavy
gating. Similar to Pokemon's overworld freedom in the mid-game where
routes branch and the player picks their path.

**Not endless.** The world has a defined shape and edges. It just
doesn't force a linear order through it.

**The ending is the return home.** Whatever the protagonist leaves to
find, the story closes when he returns with it. The player earns the
ending by having traveled — not by defeating a final boss, not by
reaching a particular ending point on a map. The return is the
narrative resolution.

## Systems (initial sketch)

### Traveling

- Overworld movement, foot-scale, camera follows.
- **Multiple region types**: forests, mountains, plains, coastlines,
  wetlands, rivers, hillsides, tundra, high-desert, farmland edges,
  woodland clearings.
- **Weather system** — rain, snow, mist, clear, fog. Affects
  visibility and atmosphere; possibly affects some ambient encounters.
  Chrono Trigger / Pokemon Ruby-Sapphire register.
- **Day/night cycle** — full 24hr cycle at compressed real-time. Time
  of day affects what's out (nocturnal creatures, ambient events).
- **Landmarks** — distinctive natural features (a lone tree on a hill,
  a rock formation, a bend in a river) that the player remembers
  spatially. The world rewards attention.

### Camping / rest

- Player can camp at appropriate spots (clearings, safe roadside
  places).
- Camping restores health, passes time, produces a small ambient scene
  (fire, cooking, sleep).
- Possible camping-as-narrative-moment: inner-monologue triggers
  around fires; small memory beats; occasional visitor.

### Skills (acquired, not leveled)

- **Skills are learned, not grown.** Get one, use it, keep it. No XP
  curves per skill.
- **Environment-specific application.** Different skills for different
  situations — one for cold-weather crossings, one for gathering
  specific plants, one for calming wildlife, one for reading tracks,
  one for signaling in fog, one for identifying wild food.
- **Acquisition**: learned from small encounters (a person met on the
  road teaches something; observing a specific animal reveals a
  method; finding a specific item unlocks a technique).
- Not gated behind any fight — they are learned by attention and encounter.

### Encounters — the "combat" is metaphorical **[LOCKED]**

**There are no hostile enemies and no battle system.** Nothing in this world
attacks him; there is nothing here that wants to hurt him, and the reason is
structural rather than tonal ([THEME.md](THEME.md)). Combat exists only as an
*analogy* for how an encounter is engaged: you read it rather than beat it
([GAME-SYSTEMS.md](GAME-SYSTEMS.md) — "observing is the combat").

What the strategic-encounter register keeps, expressed through observation and
deeds instead of a fight:

- **You win by reading, never by out-levelling.** Attention and the right
  approach, not numbers.
- **Rare and weighty.** A handful of encounters that matter, against many quiet
  ones — never the frequent-random-encounter rhythm.
- **Non-violent solutions were always the point.** Wait it out, offer something,
  approach differently. With no violent option at all, this stops being an
  alternative and becomes the whole verb set.
- **Pacing may tighten** — an encounter can carry tension, weight, a beat that
  is not leisurely. Achieving that feel through observation/action rather than
  threat is an open craft problem (see Open questions).
- **Music** may shift register for a weighty encounter and return to the ambient
  bed after.

Light stats and buffs still exist (food, rest, skills) — they feed reading and
doing, not survivability. There is **no health pool and no losing.**

*Origin note: the original concept ([ORIGINAL_NOTES.md](ORIGINAL_NOTES.md))
described turn-based combat in an Undertale/EarthBound register. That register
survives; the battles do not.*

### Gathering / inventory

- Pick up things along the way — small trinkets, cooking ingredients,
  plants for the game's herbalism, notable rocks / feathers /
  seedpods.
- Some for practical use (cooking, healing, crafting simple items).
- Some for collection / attention-reward (things you notice because
  you're paying attention).
- No weight system torture. Inventory management is light.

### Inner-monologue system

- Contextual triggers throughout the world — reaching a viewpoint,
  entering a specific biome for the first time, camping at a specific
  place, encountering weather change.
- The protagonist thinks. The player reads. Not dialogue choices —
  the protagonist's own inner voice.
- The tone SHIFTS across the game as the protagonist's understanding
  of himself shifts. Early monologues are surface-level ("this is
  cold" / "I miss home"). Late-game monologues become deeper. The
  arc of self-knowledge is legible through the monologue register.
- Sparse. Not every screen. The silence between monologues is as
  important as the monologue itself.

### Music / sound

- Ambient, layered, region-specific.
- The composer is you + your friend.
- Ambient beds shift by region, weather, and time of day.
- A weighty encounter may break the ambient bed briefly, then return to it.
- Long stretches of very-little-music are OK. Silence is used.

## Art direction

- **8-16 bit pixel art.** Not lo-fi retro — precise, painterly-in-
  limited-palette. Reference Chrono Trigger's world sprites, Mother 3's
  town textures, Studio Ghibli's landscape sensibility rendered in
  pixels.
- **Muted color palettes.** Ambient greens, blues, greys, warm
  fire-oranges for camps. Not vibrant Pokemon Ruby saturation;
  softer, more Mother 3 / EarthBound Halloween Hack register.
- **Landscape as protagonist.** Vistas that the player stops to look
  at. Views. Reflections. Weather shading. Time-of-day lighting.

## Ending

- **The protagonist returns home carrying what he found.**
- Whatever he found is understanding-shaped, not object-shaped. He
  brings home himself, changed.
- **Melancholic-uplifting.** He's older, more himself, quieter.
  Something was left behind on the road; something was gained.
- Simple, small-scale ending. He walks through the door. His family
  is there. The game ends.

## Not this

- Not: epic fantasy, hero's-journey-defeat-the-dark-lord, chosen-one
  narratives
- Not: cities, malls, technology, cars, modern trappings
- Not: heavy dialogue trees, faction politics, choice-consequence
  moral systems
- Not: leveling grinds, gear treadmills, deep RPG number-crunching
- Not: horror, grimdark, cynical detachment
- Not: infinite / procgen open world — the world is finite and
  authored

## Settled since this list was written

- **Regions** — a shore (the hub he begins on) and a small woods that keeps
  opening. Deep rather than wide. See [OPENING.md](OPENING.md).
- **Home / family** — never seen, only ever described. The shore is the *home
  away from the home he references*; the story decides when they surface.
- **Combat** — cut as a system; kept as metaphor. See Encounters above.
- **Dungeons** — possible, but they cannot lean on combat. The open craft
  problem is what resists the player instead (navigation, item-gating,
  comprehension as the lock).

## Open questions

- **What draws him back into the woods.** Recognition needs a reason to return.
- **How an encounter earns tension** without threat — the "combat pacing" feel,
  not yet achieved in the build.
- **Which skills specifically exist** — needs a skill list.
- **Camping mechanic depth** — pass-time-only or full narrative vignettes.
- **Inner-monologue authoring workload** — how many triggers, how deep per
  trigger. Sized by the both-registers rule in [VOICE.md](VOICE.md).
- **Save system** — camping-as-save-point vs anywhere-save. Note the pillar that
  saving is a personality surface, not plumbing ([VOICE.md](VOICE.md) §3b).
- **Palette** — muted vs. brighter Emerald/EarthBound warmth. See
  [AESTHETIC.md](AESTHETIC.md).
- **Whether the composer authors regions before art or vice versa.**
- **Whether the shore and woods carry the whole walk** or are the first two.

## Cross-references

- [ENGINE.md](ENGINE.md) — engineering / architectural notes,
  reuse from prison-escape and engine core
- [AESTHETIC.md](AESTHETIC.md) — pixel art references, palette
  direction, environmental storytelling notes
