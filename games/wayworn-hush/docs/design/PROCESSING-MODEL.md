# Wayworn Hush — Processing Model (the cognition engine)

> **Owns:** what happens *inside* the pilgrim when he observes something — the
> objective reading, the subjective **realization** it can spark, and how those
> feed back to unlock more. The game's core mechanic ("observing is the combat").
> [OBSERVATION-SYSTEM.md](OBSERVATION-SYSTEM.md) owns the *verb* + *HUD*; this doc
> owns the *cognition*.
>
> **Status:** BUILT (core + VALUE model). The two-type engine, the `unlock_when`
> primitive (`UnlockCondition.h`), the ambient trigger-index engine
> (`Observations.cpp`: `observe` / `setFlag` / `evaluateStats` / `runEngine`),
> and the four glimmer signals (`signalFor`) are implemented and tested. The
> **VALUE model** (Layer 2) is live: a thought's `value` (= authored `importance`
> + structural `opening` over the reveal graph), its `difficulty` (structural +
> quiet value nudge), and all Spirit EXP are **derived** at load; observations
> carry authored `value`; hidden observables use `visible_when`. Authored
> `difficulty`/`spirit_exp` are gone.
>
> Not yet built: the separate thought-journal surface, the `!` visual mark, the
> action menu, the thought cabinet, the world map (see §Not yet built).

## The premise

Observing models how a real person processes what they notice. There are **two
kinds of understanding**, and the second self-feeds into a loop:

1. **Observation — objective.** What is factually there, at the detail your
   acuity affords. **Deterministic** — a sharp eye always sees the moss. No roll.
2. **Realization — subjective.** A stat-weighted **thought** your mind produces
   from what you've observed. **One type**, whose character is *emergent* in how
   many observations it needs:
   - fed by **one** observation → reads as a **thought** (an in-the-moment
     association — "this moss recalls the north ridge"),
   - fed by a **series** of observations → reads as a **conclusion** (a
     cross-event synthesis — "moss + anthill + ridge-rains → this floods in
     spring") that **unlocks** content.

A realization's inputs are always **observations** (never other realizations, in
the authoring sense — though a *fired* realization becomes a held memory another
can reference). It is a **roll**, recorded once. "Conclusion" is not a separate
type — just a realization requiring many observations, Reason-gated, with
lived-experience feeders + yields.

Randomness lives only in **realizations** (the thought/synthesis roll).
Perception is deterministic; capability (actions, when built) is a threshold.

## The ouroboros

```
OBSERVE (objective, deterministic tiers)
   │  observing a spot is now a held memory
   ▼
REALIZATION (subjective roll): fires when its unlock_when becomes true —
   fed by 1 observation → a "thought"; by a series → a "conclusion"
   │  a conclusion's yields set flags / reveal observables
   ▼
… new memories + unlocks feed the next REALIZATION → observe → realize → unlock … ↺
```

Each observation becomes a **memory** future realizations can require; each
conclusion **unlocks** new content. Both pools grow every loop — the machine
feeds itself.

## The unlock primitive (`unlock_when`) — the spine

**One data-driven condition gates everything** (an objective tier, a realization,
later an action). It is an **OR of clauses** (satisfied if any clause holds); each
clause is an **AND** of its set fields. Empty = unconditional. (`UnlockCondition.h`)

| Clause field | True when |
|---|---|
| `observed: [X, Y, ...]` | you hold ALL these memories (an observed spot, or a fired realization id) |
| `stat: {faculty: N}` | each named stat is ≥ N |
| `flag: Z` | quest/event flag Z is set |

There is deliberately **no tier-depth clause**. The deepest objective tier whose
own `stat:` gate you meet is the one that surfaces — a tier never needs to say "I
require being at tier N," because meeting tier N's gate *is* being deep enough.
If a future gate genuinely needs "reached tier N at spot X," that becomes a
memory id (`X@N`) checked through `observed:` like everything else — one uniform
mechanism, no separate concept.

A realization requiring **many** observations in one clause is a synthesis (a
"conclusion"); requiring one is an association (a "thought"). "Memory is the game":
`observed:` checks your accumulated record, never a stat.

## Memory is not a stat — Memory IS the game

There is **no Memory faculty.** "The memories you hold" is not a stat — it is the
**accumulated record** (`observed_tier`, `fired` realizations, `flags`). The
faculties (**Wonder · Reason · Perception**) are your *capacity to process* that
record. The record is *what* you process; the faculties are *how well*. A
realization that "needs a memory" checks whether you've **recorded** it (via
`unlock_when: observed:`), never a "Memory level."

## Layer 1 — Observation (objective, deterministic)

The plain reading. **No roll, no rarity.** Authored tiers gated by `unlock_when`
(usually stat thresholds); you get the deepest tier you meet. Earns `spirit_exp`
once per tier reached; reaching a deeper tier is a state change that can make new
realizations available.

```json
"observables": [{
  "id": "stone", "x": 976, "y": 656, "radius": 96,
  "tiers": [
    { "text": "A stone, half-sunk in the grass.", "spirit_exp": 5 },
    { "unlock_when": [{ "stat": { "perception": 3 } }],
      "text": "A cap of moss clings to its north face.", "spirit_exp": 5 }
  ]
}]
```

## Layer 2 — Thought (subjective, a roll)

The **colored, faculty + rarity-labeled box** — the pilgrim's interior. Fires
**ambiently** the moment its `unlock_when` becomes true (DE-passive; wherever the
player is), then is recorded permanently.

**"Thought" is the umbrella.** A thought is whatever crossed the mind — a felt
impression, an association, a deduction. A *realization* (deduces) and a
*conclusion* (synthesizes a series) are **kinds** of thought; the difference is
emergent (how many observations it fuses), not a separate type.

### The two numbers: VALUE and DIFFICULTY — independent

A thought carries two derived quantities that mean **different things** and must
never be collapsed onto one axis. Real life proves they're independent: the
realization that reframes everything is often the *simplest* to reach ("major
lore, but obvious once seen"), and a trivial detail can be the *hardest* to spot.

- **VALUE — worth.** Drives the **reward** (Spirit EXP now, stats later). Two
  roads to worth, summed — `value = importance_weight·importance +
  opening_weight·opening`:
  - **importance** — *how much a thought matters*, itself two parts:
    `importance = centrality_weight·centrality + emotion_weight·emotional_weight`.
    - **centrality** (*derived*) — how much of the web hinges on it: the
      cycle-free base worth **upstream** (what it took to reach — every
      observation/thought it stands on) plus **downstream** (the thoughts that
      depend on it). A deep culmination or a busy hub scores high. This is the
      *objective* importance the graph can read.
    - **emotional_weight** (*authored*, usually `0`) — the story/emotional
      significance no graph can see (a deathbed beat that connects to nothing yet
      lands). The one authored worth dial.
  - **opening** — *derived, structural*: the observable `value` this thought
    **uniquely** reveals (leave-one-out over the reveal graph). Distinct from
    centrality's downstream (which counts thoughts, so `opening` is never
    double-counted).
- **DIFFICULTY — how hard to arrive at.** Drives the **roll threshold** (and the
  rarity word + color). Mostly **structural** — how much the thought fuses
  (breadth of observations) and demands (feeders) — plus a **quiet, capped**
  value nudge, so a structurally simple thought stays easy no matter how
  consequential. `difficulty = clamp(structuralLoad + min(value_weight·value,
  value_nudge_cap), 1, max_band)`.

Nothing about VALUE or DIFFICULTY is authored directly — you author `importance`
(and per-observation `value`); the engine derives the rest.

### The roll (self-scaling — fair by construction)

```
facultyLevel(faculty) + feederBonus + rand(0..dice)  >=  threshold
feederBonus = Σ statLevel(feeder) / per
```

The **faculty is the roll weight, NOT a prereq** — anyone who's observed the
inputs attempts it; the faculty (plus feeders, plus luck) decides success.

The threshold places DIFFICULTY against the thought's **own reachable capacity**
— the best roll someone maxed on *this thought's own inputs* could make:

```
capacityMax  = stat_cap + Σ stat_cap/per + dice          // this thought's own ceiling
frac         = (difficulty - 1) / (max_band - 1)          // 0..1 across the bands
threshold    = frac * capacityMax * tightness             // tightness < 1, authored
```

Because the threshold scales to the thought's **own** faculty + feeders, a
high-difficulty thought sits near the top of *its own* plausible range (hard but
landable), a low one near the bottom (easy). A thought drawing on Reason +
survival is measured against a *larger* ceiling than one on Wonder alone — fair
*relative to what it requires*. **The engine cannot produce an unlandable roll.**
`tightness` is the one world-wide knob.

- **Miss = content + re-open.** A miss surfaces nothing (or the faint `miss_text`
  pull, later). It is **not** a sticky state — the ambient engine re-checks the
  thought whenever a relevant input (its faculty, a feeder, a memory, a flag)
  next changes (see §The ambient engine). Growth genuinely re-opens it; standing
  still does not.

```json
"observables": [
  { "id": "stone", "value": 2, "x": 976, "y": 656, "radius": 96, "tiers": [ ... ] },
  { "id": "water", "value": 3, "x": 464, "y": 400, "radius": 96, "tiers": [ ... ] },
  { "id": "ruin",  "value": 8, "visible_when": [{ "flag": "knows_settlement" }], "tiers": [ ... ] }
]

"thoughts": [
  { "id": "stone_water_worn",
    "unlock_when": [{ "observed": "stone" }],
    "faculty": "perception",
    "text": "Worn smooth. Water shaped it, once, where none runs now." },

  { "id": "people_lived_here",
    "unlock_when": [{ "observed": ["stone", "water"] }],
    "faculty": "reason",
    "feeders": { "survival": 2 },
    "text": "People lived here once. The water drew them; the water left.",
    "set_flag": "knows_settlement" }
]
```

The first is a **realization** (one observation, Perception). The second is a
**conclusion** (a series — stone AND water — Reason-gated, survival feeds it,
and it **reveals** the ruin via its flag → high opening). Same struct; the kind
is emergent. **Nothing authors `difficulty` or `spirit_exp`** — both derive.

### Deriving VALUE (the forward reveal-graph)

`opening` is the **leave-one-out marginal**: run the reveal walk with **all**
thoughts firing, then again with **this thought excluded**; the difference is the
observable `value` that *only this thought opens*. A thought that reveals a
hidden spot gets that spot's value; one that opens nothing gets `0`.

The reveal walk is a fixpoint over two forward edges: a firing thought's
`set_flag` / being-noticed can (a) make a hidden **observable** visible (its
`visible_when` holds → the player can now observe it → its value is reachable),
and (b) enable further **thoughts**, which cascade. An observable's optional
`visible_when` (same `unlock_when` primitive) is the uniform reveal mechanism —
no bespoke "unlock_observable" field. The walk terminates because inputs are
always observations or earlier thoughts (a DAG), so VALUE is computed once at
load and cached.

### Deriving centrality (how much of the web hinges on a thought)

`centrality` is the *objective* half of importance, read off the dependency
graph. Each memory has a **cycle-free base worth**: an observation's authored
`value`, a thought's `opening`. Then:

```
upstream(t)    = Σ base worth of every observation/thought t transitively requires  (what it took)
downstream(t)  = Σ opening of every THOUGHT that transitively requires t            (its hub role)
centrality(t)  = upstream(t) + downstream(t)
```

Downstream counts **thoughts only**, so `opening` (which owns downstream
*observable* value) is never double-counted. A base worth being cycle-free means
the whole thing computes in one staged pass: `opening` first, then `centrality`,
then `importance` → `value`.

This is what fixes the **terminal payoff**. `who_left_here` opens nothing
(`opening 0`), but it sits atop the whole stone→water→settlement→ruin pyramid, so
its **upstream** centrality is large → it reads as important and rewards well,
even though it's a dead end. A hard conclusion that everything led to is
valuable *because* everything led to it. (Authored `emotional_weight` layers on
top for the beat the graph still can't feel.)

### The shared currency (systems flow together)

Just **three authored numbers** — an observation's `value`, a thought's
`emotional_weight`, and the world tuning weights — feed **every** downstream
quantity, so the systems move as one instead of each carrying its own magic
constant:

| Fed by | Downstream |
|---|---|
| observation `value` (authored) | that observation's tier Spirit EXP; its base worth in centrality |
| observation `value` opened | a thought's **opening** |
| graph shape (up/downstream worth) | a thought's **centrality** (derived) |
| thought `emotional_weight` (authored) | the emotional half of its **importance** |
| centrality + emotional_weight | **importance** |
| importance + opening | **value** |
| value | Spirit EXP **reward** |
| thought structure (+ quiet value nudge) | **difficulty** → rarity word, color, roll threshold |
| observation `value` *(later)* | glimmer pull weight |

Change one authored number and the whole web re-balances at load. Nothing is
authored twice; importance, value, difficulty, rarity, and reward are all
**outputs** of two honest judgments (what's worth noticing, what lands
emotionally) plus the graph's own shape.

## The ambient engine (event-driven, indexed)

Realizations **float free** — not owned by any observable. They fire whenever
their `unlock_when` becomes true, through observing, an event, a flag, a stat
gain — anywhere. To stay performant, an **inverted trigger index** built at load
maps each *changed key* → the realizations that reference it, so a state change
only re-checks the realizations that care — **O(affected), not O(all).**

```
key forms:  obs:<id>   flag:<name>   stat:<name>
load(): for each realization, register it under every key its unlock_when names.

runEngine(changedKeys):            // the single shared core
  while changedKeys not empty:
     key = pop
     for each realization in index[key]:
        if unfired and satisfied(unlock_when):
           roll  → hit:  fire, queue line, earn EXP (from VALUE), apply yields
                          (yields push NEW changed keys → cascade), record memory
                   miss: nothing recorded — re-checked when a relevant key next changes
```

A miss is **not** a sticky state: the index only re-visits a realization when a
key it references changes (its inputs, its faculty, its feeders — all indexed),
so a missed roll is automatically re-attempted the next time any of them moves.
Anti-abuse is **structural** — standing still changes no key, so no re-roll.

Three entry points all funnel into `runEngine`:
- **`observe(pos, dir)`** — reveal the deepest objective tier (deterministic, EXP
  once), then run the engine over `obs:<spot>` (now a held memory).
- **`setFlag(flag)`** — an external event sets a flag → run over `flag:<name>`.
- **`evaluateStats()`** — a faculty/feeder leveled → run over every `stat:` key
  (re-opens prior misses whose roll inputs rose).

Yields cascade: a fired realization is itself a held memory (`obs:<id>`), and its
`set_flag` produces a `flag:<name>` key — both fed back into the loop; fire-once
bounds it. Revealing new content rides the same rails: gate that content's
`unlock_when` on the flag. This is the ouroboros, automatic.

## The signals (real mental states → glimmer)

`signalFor(spot)` derives one of four states — **what your current self can do at
this spot right now.** About the subjective/reachable layer, never the objective
reading (always just there at your acuity).

| Signal | Real condition | Glimmer |
|---|---|---|
| **Unobserved** | never observed | warm glow, only when faced |
| **InsightAvailable** — *"I could get this now"* | a deeper tier OR a realization referencing this spot is **reachable + unfired** | the **`!`** (currently the strongest glow; the mark/pop lands with the `!` visual slice) |
| **Intuition** — *"something here, I can't place it"* | such a thing exists but is **out of reach** (stat/memory too low) | **faint pull** |
| **Processed** — *"reckoned with, for who I am now"* | nothing reachable-unfired here now | **soft persistent glow** |

The world never changes — **your capacity to read it does.** A spot moves
Intuition → InsightAvailable because *you* changed (a stat rose, or you observed a
required memory). Derived every query from the record + self; never a stored flag.
This is what fixed the old glimmer bug (which used "deepest *eligible* tier" as a
false "done" test).

## UI honesty (load-bearing principle)

**The UI shows the diegetic/felt truth; it hides all the math.** Playable
fun/high-level by a casual player, optimizable by a deep one, without *requiring*
anyone to engage the mechanics.

- **Shown (honest, real state):** *that* you realized something (the box); *how
  rare* (rarity word + color = the difficulty you beat); *that there's more here*
  (the `!`/glow = a reachable-unfired thing); *that you grew* (a stat leveled).
  Stats are player-facing — the one dial the player steers.
- **Hidden (the math):** the roll, thresholds, `facultyLevel + feeders + rand`,
  the trigger index, the `Knowledge` snapshot, `missed_at_level`, the `unlock_when`
  conditions. Never a number-vs-number check, never a die.

Rule: **stats visible, everything downstream of stats invisible.** (`Knowledge` is
backend plumbing; it surfaces nowhere.)

## Surfaces (separate records)

- **Observations** — the objective record (per spot). Eventually pinned on a
  **world map** with `!` markers.
- **Thought journal** — the subjective record (fired realizations). **Separate**
  from observations. *(Currently both share the pause page's Noticed tab; the
  split is a pending slice.)*

## Not yet built (implementation order from here)

1. **Separate thought-journal surface** — split fired realizations out of the
   Noticed tab into their own view; style objective (plain) vs realization
   (colored) distinctly in the box via `PendingLine.kind`.
2. **The `!` visual** — the exclamation mark + pop-in + soft pulse for
   `InsightAvailable` (the signal + engine already exist; this is the render).
3. **Action menu** — the capability-gated deed tree on an observable (open on
   observe; options gated by the same `unlock_when`; acting sets a flag/memory the
   ambient engine re-checks, which is how a **missed thought comes back**). Full
   design in [ACTIONS.md](ACTIONS.md).
4. **(Deferred)** thought cabinet (slot/internalize notable thoughts into
   permanent shifts); world map (pin observations + `!`).

## Cross-references

[OBSERVATION-SYSTEM.md](OBSERVATION-SYSTEM.md) · [GAME-SYSTEMS.md](GAME-SYSTEMS.md)
· [THEME.md](THEME.md)
