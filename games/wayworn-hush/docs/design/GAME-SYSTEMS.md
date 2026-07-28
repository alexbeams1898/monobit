# Wayworn Hush — Game Systems (synthesis)

> Proposed systems architecture, from research into Disco Elysium (stats),
> WoW/shiny-hunting (endless loop), Selva (materials), and Outer Wilds/Obra
> Dinn/Roadwarden (quests). **Status: proposal for slow review, one system at a
> time.** Nothing built to this yet.

## The north star

**The purpose of the journey is to find your way through the world while truly
coming to know yourself.** Everything else serves this. You walk, observe, camp,
forage, craft, fish — and every one of those acts is a way the pilgrim *learns
himself*, not a survival chore. Underneath (never stated, per
[THEME.md](THEME.md)): a soul in the afterlife finding peace by finally
reckoning with who it was.

**Finding your way = understanding.** You don't win by surviving or looting. You
win by coming to *see* the world — and yourself — deeply enough. Deep readings
of the world are **keys**: a reading you couldn't get early-game becomes
available once you've grown, and it *unlocks the way forward* — a game-long
quest, a piece of knowledge, a path, a truth about the pilgrim. The rare reading
is the **means**, not a prize. Getting it is how you find your way.

**One insight organizes the mechanics:** you're combining three proven genres —
Outer Wilds (knowledge *is* the key that opens the world), Disco Elysium
(observe → inner voices → identity), Selva (gather → craft). Closest kin is
Outer Wilds: **the loot is understanding.**

## Minimalism is the method **[PILLAR — always was, stated here]**

**Reduce every system to the least it can be while keeping its quality and its
function.** Not "small scope," and never "cheap": minimal like minimal synth —
few voices, each chosen precisely, given room. The restraint is where the power
comes from. Sparseness makes each element land because nothing crowds it.

What it means in practice:

- **Few verbs, deeply expressive.** The answer to "the game needs more depth" is
  almost never a new system. It is an existing verb reading the player in a way
  it did not before.
- **Repetition as structure, not filler.** A loop that shifts slightly is the
  form, not a shortfall — which is exactly why the woods are small and revisited
  ([OPENING.md](OPENING.md)).
- **Space is an instrument.** The walking between observations, the quiet, the
  emptiness — load-bearing, not unfinished content.
- **One change lands hard because everything else is still.** Rarity is what
  makes a shift uncanny; frequent change reads as noise.
- **Many small reactive touches, almost no branching.** Reactivity is cheap when
  it is the *same* content read differently (tiers, flags, faculty gates) and
  expensive when it forks into authored variants. Prefer a hundred things that
  notice the player over three that branch for him.

**"Tighten up" means this.** When the instruction is to tighten, simplify, or
consolidate a system, it means: make it as minimal as it can possibly be while
retaining its quality and its function — not merely tidy it.

## Two stat layers

The whole stat model is two tiers, and the second always feeds the first:

- **The self** — three "reading" faculties (Wonder · Reason · Perception, §1),
  plus **Spirit** (their derived depth-readout). *The point.* They gate how
  deeply you can read the world; growing them is finding your way.
- **The doing layer** — **Body** and the survival stats (camping, fishing,
  crafting). They exist **solely to feed the main loop**, and every gain routes
  up into the self *logically*: fishing patiently → Perception; enduring the long
  road → Body → Perception/Spirit feed; a felt/embodied reading → Perception. You
  never grind the doing layer for its own sake — you grow it, and the *self*
  grows with it. (Body sits at the top of this layer, not among the faculties:
  it's physical capacity, kin to the survival stats, not a way of *reading*.)

The doing layer gives the player concrete *reasons to act* (a quest needs a rod,
a pass needs gear); the payoff is always a deeper self.

## The core loop (bread-and-butter)

The one repeated verb-to-reward beat that fills minute-to-minute:

```
a curiosity / quest asks a question of the world
  → you act on it (observe, or craft a "key": rod, lantern, gear — then observe)
  → your faculties have grown, so you read the world DEEPER than you could before
  → the deeper reading is a KEY: it unlocks the way forward
    (a quest thread, knowledge, a path, a truth about yourself)
  → following it grows you further → you can now read deeper still → …
```

**Why crafting matters with no fail-state:** crafting is **the verb that opens
the world.** A key unlocks *access* (reach the water, the pass) and a new
*observation surface* (fish, night, vistas) — but the reward is always a deeper
reading, which is always a step in finding your way. Craft = key, observe =
door, understanding = the way through. Fishing and gear are sought because a
curiosity points at them, never as survival busywork.

**Observing is the "combat"** — DE's dialogue-check, but you read the *world*
(people included — see §8), not adversarial dialogue. The check is gated by your faculties: the same rock gives a flat,
objective line at low faculty and a rare, revealing reading once you've grown —
and the rare reading is what unlocks the next thread.

Your **existing observation system IS the minified Disco Elysium** — extend it,
don't bolt on.

## The arc: soft ending → post-game → true ending

The finite story and the endless loop are the same structure, resolved cleanly:

- **Soft ending (~10hr):** the pilgrim finds his way *enough* — the return-home
  beat lands, the arc closes. A **complete game**; a player can stop here
  satisfied.
- **Post-game (the loop):** the world stays open. The pull is now *fullness* —
  the quests and rare readings you haven't reached yet. Gentle and achievable by
  attentive play (no grind walls, no luck-farming), **but every fill rewards**:
  a new depth of monologue, the knowledge-map filling, a faculty deepening, the
  pilgrim visibly changing. Rewards land across the whole curve, not only at
  100%, so the player is always pulled gently forward — never grinding, never
  aimless.
- **True ending:** gated on *fullness of understanding* — the remaining quests
  complete and the rare readings obtained. Not a harder fight; a **deeper
  peace**. The soul that came to know everything, and itself, fully. (Buried:
  the reconciliation the soft ending only gestured at.)

The soft ending is the **arc** (spent once); the post-game is the **loop**
(runs forever, but finite here); the true ending is the loop *resolving into a
final arc beat.* Completion **is** the theme — "understand everything → find
true peace" is thematically exact, not a bolted-on collectathon.

*Nothing in this doc is LOCKED — it's the corrected spine for continued slow
review. Open threads below: boil the voices to ~4 (§1), define Spirit-as-EXP
precisely, keep working consumables/fishing (§4).*

---

## 1. The growth economy — three self-faculties, Spirit EXP, buffs

The machine, top to bottom. Deliberately minimal (a quiet game, not an RPG with
a skill web). Four parts, each with one job.

**What the numbers mean [FRAMING — load-bearing]:** stats and Spirit measure the
**spiritual and emotional journey**, never competence. The faculties are the
capacities of a soul's attention — a grown man can be fully capable and
spiritually asleep, which is exactly where the game opens: **he starts the game
in low spirits** (the pun is the thesis). Starting values need no other alibi;
the walk is him coming back to his senses. The test for any future system:
anything that grants Spirit must be defensible as soul-tending, and anything
that raises a faculty must be defensible as waking up.

### The three self-faculties — the "reading" voices

Three faculties (Will loved philosophy — the RPG's soul is philosophical). Each
is *an inner voice* with its own agenda; they can conflict. They are all ways of
**knowing/reading the world** — that coherence is why Body is *not* one of them
(Body is physical capacity, and lives in the survival layer, §4). Names
refinable.

- **Wonder** — awe, the strange, the beautiful, the magical-real; *begins
  inquiry.* The open/associative/what-if reading. *Moment:* a fog valley at dawn
  reframed as sublime; a phenomenon only a Wonder-heavy self ever stops for.
- **Reason** — logic, connecting observations; the analytical/scholar reading.
  Drives deduction — scattered observations into a conclusion. *Moment:* dry
  riverbed + foundations + fruit tree → "people farmed here once."
- **Perception** — the senses across time (folds in recollection: recognizing
  what you've seen before is Perception over time). The noticing/detail reading;
  makes more observables glimmer and unlocks recognition-gated depth. *Moment:*
  you see "someone slept under this tree" where others see a tree.

Voices can war: Reason vs Wonder (explain vs marvel) — the DE tension. Which
voice *speaks* at a place is a data-driven gate on the observation system
already built (glimmer/chime/tiered-text exist), not new machinery: an event is
`{voice, place/trigger, condition, difficulty, text/effect, once?}` — one config
entry each. The DE electrochemistry-at-a-spot pattern: latent moments that fire
only for who you've become, where, when.

### Spirit EXP → buffs → Spirit (the whole growth loop)

One currency, one curated set of buffs arranged as the soul-tree below, one
derived readout. (An earlier draft said "no tree, no prerequisites" — superseded
by *The tree IS the soul*: structure is the identity mechanism, kept shallow.)

- **Spirit EXP** (working name; rename TBD) — the currency. **Earned by major
  events, achievements, rare observations/unlocks** — not by grinding. You spend
  it to buy and upgrade buffs.
- **Buffs** — each self-faculty has a small set of **named, upgradeable buffs**
  (buy Lv1, upgrade toward Lv3). Not abstract stat-nodes — concrete, legible
  effects. Two properties, both by design:
  - **Hidden / discovered** — buffs *reveal as you go*, not shopped from a menu.
    The self surprises you; fits the quiet register.
  - **Effects are content-driven — TBD.** Buff effects are **not** invented in
    the abstract. They're dictated by the actual quests/progression and must be
    *genuinely wanted* against real content (a buff earns its place because a
    specific quest/lock/depth makes you want it). Likely "rarer/deeper readings
    of the world," but this stays an **open slot** until the story/progression is
    built out. Do not over-specify effects before the content they reward exists.
- **Spirit** — **derived from the buffs: the sum of all buff levels owned.**
  Buy/upgrade a buff → Spirit rises; monotonic by construction. Spirit is never
  a voice and is never earned directly — it is the *readout of your invested
  self*. Other systems read it:
  - rare-reading depth at a place — read the relevant *faculty/buff* level for
    flavor, and total **Spirit** for overall depth (the rock's generic→rare
    axis; the north-star mechanic),
  - the true-ending gate (fullness of understanding),
  - ambient formulas — the world grade warms, thoughts deepen as it rises.

  Spirit is the currency fed into the soul — earned by understanding, spent to
  take shape as the tree below. It IS displayed (you cannot choose what to become
  without knowing what you have to spend). What stays buried is what it *means*:
  the soul's accumulating peace, never named as such and never explained.

### The tree IS the soul **[LOCKED]**

**Spirit is potential; the tree is what he has become of it.** Nothing is
consumed by spending — the Spirit leaves the pool and takes shape as him. An
empty tree is a person who has not become anything yet; a filled one is a
specific person. A node is therefore not a perk but **a part of him**: something
he can now see, do, or bear that he could not before. The tree can never be
filled, so every soul is partial and particular.

**Where becoming happens: the lean-to.** Spirit accrues in the world (gathering,
observing, understanding) but is spent only at a lean-to — the safe, still place
where the soul is tended ([STORY-FRAME.md](STORY-FRAME.md) §4b). This gives the
game its rhythm — wander and accrue, arrive and become — and makes the reshaping
fictionally earned: you do not become someone else mid-walk; you do it stopped and
safe. **world = gather, lean-to = spend/become.**

**Your "class" is solely what you spent yourself on** — a Wonder-heavy self *is*
the dreamer; a Reason-heavy self *is* the scholar. **No class selection, no class
screen**, never named in UI, and never called a skill tree in game — it is not a
menu of abilities.

**Forks are the identity mechanism.** Beyond branch leanings, a small number of
**mutually exclusive sibling nodes** — lighting one shades the other for the
walk — are what make the tree a portrait rather than a checklist (the lesson of
KCD2's either/or perks: identity lives in the handful of exclusive choices, not
the volume of nodes). How many forks and where is content authoring, later; the
DATA MODEL carries prerequisites and exclusion groups from day one so none of it
is retrofitted.

**The pre-lit core [WIP — not locked].** The tree at walk-start is untended, not
dead: a few nodes already alive — what survived the deadening. The authored
default is part of who Will is (the writer's call). Possibly personalized by a
skippable, EarthBound-style questionnaire that asks the PLAYER about themselves
— never about Will's biography, which stays authored — with skipping yielding
the default, exactly like declining the rename yields "Will." The questions'
voice and the ember set are story material.

**The faculties are NOT decoupled.** A soul has no crafting compartment: growth
anywhere changes him everywhere. Cross-links and shared prerequisites are
correct, and a part grown for one reason should change something apparently
unrelated (what you gained by being patient changes what you can bear, not only
what you can see). Cross-faculty gating is *also* authored on the content
(`requires Wonder>=2 AND Perception>=2`) — both, not either: the world holds
combinatorics and so does he.

This is the game's central ambition, and it is meant literally rather than as
decoration: **the systems are structured the way the thing they model actually
works.** Everything the pilgrim does converts into one place; everything he can
see, do and endure reads back out of it. The ending reads the tree because there
is nothing else to read.

**Replay engine:** a second run spends Spirit EXP on different buffs → a
different class → different keys → a genuinely different reading of the same
world.

### No fail-state

Failure branches (a different voice/thought), never game-over. You can even beat
the game leaning heavily on the survival layer — viable, just lower-priority;
the faculties + Spirit are where the game lives.

## 2. Thoughts / Cabinet — the growth spine

- Sit with an idea/obsession (a **Thought**); it imposes a *temporary cost*
  while "internalizing," then permanently changes you.
- This IS "the soul internalizing what it learns" — and it's **how quests work**
  (a quest = a Thought maturing).
- Slots are scarce; a few at a time.
- **Reuse:** Selva's `insight` graph (the audit lift) is the backend.

## 3. Quests — curiosity, not checklists (Outer Wilds model)

- No quest log/markers. A quest is **a question the pilgrim wants answered.**
- **A knowledge-map** (Outer Wilds Ship Log): observations auto-log as facts;
  leads are *dangling connections* to unfound things; a self-clearing "more
  here" hint — never a nag.
- **Conclusions form in batches** (Obra Dinn): gate a conclusion behind enough
  correct observations, confirmed as a set — makes noticing feel *earned*.
- Clues for one "curiosity" **spread across scenes** → the world pulls you on.
- **Reuse:** the observation record (OBSERVATION-SYSTEM §4) becomes the map.

## 4. Survival — ritual, no fail-state, feeds the faculties

- Camp, forage, fish, tend fire, craft. **You can't lose.** No starvation/death.
- This is the **doing layer**, and **Body sits at the top of it** — physical
  capacity (weather-sense, forage yield, endurance of harsh crossings), kin to
  the survival stats, *not* a self-faculty (it's not a way of reading; it's a way
  of enduring/doing). Body is deepened by surviving and by eating (below).
- The layer exists to feed the main loop. Every act routes up into the self
  *logically* — fishing patiently → Perception; a felt reading (sensing the
  storm, the cold) → Perception; enduring a harsh crossing → Body, and Body's
  exertions in turn feed Perception/Spirit. You never grind the doing layer for
  its own sake; the self grows with it. There is **no Body route/class** — the
  physical is support, never a build.
- Stakes come from **item-gating**, not threat: certain crafted items are
  *required* to progress (lantern for the dark pass, cloak for the cold) —
  Metroidvania gating dressed as survival prep. Others are **QOL/comfort**
  (cozier camp → deeper monologues).
- "Am I equipped for what's ahead?" not "am I about to die?"

**Eating (the food-not-hunger answer):** no hunger meter, no nag. Eating does
two things at once:
- a **small permanent Body gain** — the flesh is nourished; the embodied self
  deepens (survival-layer fuel routing into a faculty, per the rule above),
- a **temporary buff** — a state/window (warmth, steadiness, a voice sharpened
  for a while).

So food *matters* without a survival meter: a real reason to forage/fish/cook is
that you're slowly building Body and opening timed windows — not staving off
death. Consumables are keys to *states/moments*, plus a trickle of permanent
growth.

## 5. Materials + crafting — Selva's gather system (lift, cut combat)

- **Gather, don't kill.** Walk to a node → observe/gather → material.
- **Two axes (Selva's cleanest idea):** *Rarity* = per-type label (how rare a
  find); *Quality* = per-copy roll (how good this one is).
- **Two lotteries:** `WeightedPool` picks *which* material; a `quality_lottery`
  picks *how good* (Masterwork ≈ 1-in-50).
- **Crafting = inputs → output** (Little-Alchemy: the ingredients are the only
  requirement, no unlock gate). Output quality scales on inputs + a doing-layer
  stat + a roll. Making something new **records** it (learning is a book-keeping
  reward, not a prerequisite); events can hand recipes over early. See
  [CRAFTING.md](CRAFTING.md).
- **Anti-cheese:** the roll commits at spawn (can't reload to reroll); cap +
  respawn trickle (can't farm one spot forever).
- **The rarest pulls are pieces of the self.** Most drops are crafting fuel; the
  rarest are **permanent boons** — an item that grants Spirit, nudges a faculty,
  or opens a route node early. This is what ties the material/rarity system to
  the *main* loop instead of a side collectathon: the rarest material isn't a
  better tool, it's a deeper self. (And it links the endless catalogue (§6) to
  the true ending — chasing rare items *is* chasing fullness.)
- **Reuse:** near-direct lift of Selva `gather/` + engine `WeightedPool`/
  `LootOps`/`CraftingOps`. **Note:** lives on the polygon-terrain branch (newer
  engine); scaffold branch must port `WeightedPool`/`LootOps` forward.

## 6. The endless loop — rare things to notice (shiny-hunt for the soul)

- **The chase = FINDING, not defeating.** The endless post-game loop is the same
  gather/observe verb + a rarity roll: rare creatures, rare weather/phenomena,
  rare variants ("shiny"), rare *behaviors* of common things.
- **The catalogue is the meta** (Zeigarnik: an 11-of-12 nags). An unfinishable
  "things noticed / wonders seen" log.
- **Author once, RNG + completionism = infinite.** Multiply a *small* authored
  set across axes (creature × variant × behavior × weather) → thousands of
  sightings; rarest combos = 100-hr chases. Cheap to build.
- Common finds feed the story economy (§5); rare finds feed the endless
  catalogue. **One economy, two jobs.**
- Rare-find feedback: a distinct chime (you have the observe-chime; rares get a
  bigger one) + the catalogue slot filling. Templates: **Alba**, Animal
  Crossing, shiny-hunting.

## 7. The arc→loop seam (soft ending → post-game → true ending)

The full structure is defined up top ("The arc" section); this is the
mechanical seam.

- The **soft ending** is the arc (spent once) — ship a real closure beat when
  the story ends; a complete game a player can stop at.
- The **post-game** is the loop (forever, finite here): the ambient,
  penalty-free gather/observe/quest-finishing loop. Gentle and achievable by
  attentive play — **but every fill rewards** (a deeper monologue, the
  knowledge-map filling, a faculty deepening), so the player is pulled gently
  forward across the whole curve, never grinding, never aimless.
- The **true ending** resolves the loop into a final arc beat — gated on
  *fullness of understanding* (remaining quests + rare readings/items). Not a
  harder fight; a deeper peace. Completion **is** the theme.
- **10hr feels complete via density, not length:** satisfying walk first;
  essential-vs-stretch content tiers; a deliberate emotional low before the
  payoff; optional unreachable depth.

---

## 8. People — nodes in the same loop **[LOCKED shape]**

NPCs are not a dialogue mode. A person is an encounter with a body, and the
observation/action/flag puzzle IS the conversation system:

- **A person = three authored pieces**, same split as everything: WHO (a
  character config: art, name, footprint), WHERE (a map placement: stand-spot,
  facing, talk box), WHAT (observation content marked with a speaker).
- **Observe a person = perceive them.** The player's own reading, in his voice —
  tiers gated by the same knowledge and faculties as any observable. People are
  part of the world being read.
- **Act on a person = engage them.** The deed menu is the conversation surface;
  a reply is a deed's result line, in the person's voice; the menu re-opening
  after a reply is the back-and-forth. Verbal and physical moves are both just
  deeds.
- **Noticing unlocks engaging**, through the one knowledge system: an observed
  tier or a flag makes a new move appear in that person's menu, and taking it
  can open trees that never open otherwise. No dialogue scripts — depth is
  attention. Skipping straight to acting is allowed and honest: moves gated on
  words never heard are simply not offered.
- **Proximity content is the person speaking up** (an Enter trigger in their
  voice) — unprompted but not controlling.
- **Scripted scenes** (a person approaching you, forced multi-beat exchanges)
  are a separate, future system — deliberately not this one. The doctrine it
  must obey when built:
  - There is a **consent gradient**, and it only changes who initiates:
    observe = fully chosen, Enter-trigger = half-chosen (you walked there),
    scripted = the world walks to you. A pressed reading is an **impression** —
    the senses take it unbidden (a crash downstairs, an alarm, a voice) — as
    distinct from an *observation*, which is chosen looking. Same content, same
    engine, different initiator; the psyche engine marks which is which, and the
    game may treat them differently (softer arrival, its own teaching).
  - A scene is **choreography that plays the graph**: it moves bodies, holds
    input, fires existing encounter content in order, releases. Never a second
    dialogue format; the writer authors one kind of content forever.
  - **The world can force what he hears, never what he concludes.** Thoughts
    still roll through his faculties inside a scene; the mind stays sovereign in
    cutscenes by construction.
  - Scenes must appear as **nodes in the authoring tree tool** (start conditions
    in, content-played edges out) — anything that participates in causality gets
    a node, or the visualization lies by omission.
- Scale honestly: a handful of characters, standing where authored. No
  schedules or pathfinding until the game asks for them.

## 9. Teaching — first-time cards, loop-first pacing

The opening teaches the loop immediately (the reward layer speaks from the
first beat — no meta-silence period), and the teacher is the **first-time
card**: the moment a mechanic first happens, the world stops, the screen dims
except the thing being taught, and one short card names it. Space resumes.

- **Once per pilgrim, ever** — the seen-set is part of the walk's record. A
  second walk re-teaches from zero, which is what a fresh pilgrim means.
- **Fired off what is on screen**, not off internal events — the card lands
  while the thing it names is visible and lit, never before or after.
- **Cards are authored content** (config), against a small vocabulary of
  first-time events the engine emits. Adding a card is authoring, not code.
- The first beats of the game double as the mechanics showcase: they should
  fire every core surface once, naturally, in playing order.

## What's already built vs. to build

- **Built:** observation/interact, tiered thoughts, glimmer, `Sprite.alpha`,
  audio, movement, the muted grade. (The stat spine's foundation.)
- **To build (roughly, order TBD):** the skill/voice model, the Thought backend
  (lift `insight`), the knowledge-map, the gather/material/craft system (lift
  Selva), the catalogue, item-gating.

## Open decisions (for slow review — one at a time)

1. **The skills** — how many, which, their voices?
2. **Skills vs. one awareness stat** — DE-lite multi-skill, or simpler?
3. **What's rare to notice** — creatures? weather? variants? all?
4. **Camping depth** — pure ritual, or the "internalize a thought" moment?
5. **Branch/engine** — build on scaffold (port gather forward) or rebase on
   polygon-terrain?

## Cross-references
[THEME.md](THEME.md) · [PSYCHE.md](PSYCHE.md) ·
[DESIGN.md](DESIGN.md) · [AUDIT.md](AUDIT.md) (selva lift) ·
[MAP-ARCHITECTURE.md](MAP-ARCHITECTURE.md)
