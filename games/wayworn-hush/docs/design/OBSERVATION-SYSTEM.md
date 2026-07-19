# Wayworn Hush — Observation System

> **Owns:** the observation mechanic — the game's core interactive verb and its
> RPG progression spine. Makes "quests = the protagonist's thoughts"
> (MAP-ARCHITECTURE §6) concrete.
>
> **Status:** design proposal (revised after playtest). Nothing built to this
> spec yet — the earlier *ambient* prototype is being reworked to *interact*.
> Design before implementing (this is the core of the game).

## tldr

**Observing is the game's combat** — structurally, not tonally. You face
something noticeable and press a key to *observe* it. That deliberate act is the
core loop: it surfaces a thought, grants a soul-like currency, and is how you
progress. It stays **relaxed** — no timing, no juice, no floating numbers; the
satisfaction is quiet and carried largely by **sound**. Thoughts are **stateful**
(Dark Souls dialogue): what you notice in a thing depends on what you've *already*
noticed — re-observing after learning more yields deeper text, and connecting the
right observations forms a **conclusion** (a higher-order thought worth more).
Currency spends at a **skill tree**. This is a simplified cousin of Selva
Oscura's shipped `lang` + `insight` system.

## Why this shape (the playtest correction)

The first prototype made observations **ambient** — walk into a zone, a thought
auto-pops. Playtested wrong: it *happens to* you, passive. The fix is to make
noticing a **verb you perform** — face + press to observe. This flips it from set
dressing to a mechanic, and it fits the game's thesis exactly: for a game about
*attention*, attention should be the thing you actively do. The pilgrim doesn't
fight; he notices, and noticing is the skill that grows him.

"That's your combat" means: it's the interactive verb, it's the XP source, it
must feel good moment-to-moment — **but the register stays contemplative.** The
"combat" analogy is about the *slot in the design* (active verb → progression),
not the *feel* (which is calm).

---

## 1. The observe action (the core verb)

- **Face a noticeable thing + press the observe key (Space) → observe it.**
  Deliberate, player-initiated. Relaxed pace, not a reaction test.
- **What's observable:** authored points/objects in the world (a stone, a
  carved tree, a bend in the river, a cairn). Some may be subtle enough that
  *spotting* them is part of the skill (perception as the mechanic), but the act
  is always the deliberate press — never automatic.
- **On observe:** surface the appropriate thought (§3), grant currency (§5),
  record it, play the observe sound (§6).
- **Re-observable freely.** Unlike the ambient prototype, you can observe the
  same thing again any time — and because thoughts are stateful (§3), it may say
  something *new* now. Currency is granted only for genuinely-new content (a new
  tier reached, a new conclusion formed), never for re-reading the same tier.

**Interaction affordance:** the player must know a thing is observable. A minimal,
quiet prompt when facing an observable (a small glyph, or the object subtly
catching the light — *not* a loud "[E] EXAMINE" banner, per AESTHETIC.md's "no
objective markers"). Exact affordance is Q1.

## 2. The textbox + queue (thought → screen)

A minimal textbox, Mother-3 register (AESTHETIC.md): understated, low in the
frame, soft text (no per-syllable clatter). Built on `UIRenderer` primitives.
**This part survives from the prototype largely intact.**

- **Multi-line flow.** A thought's several lines play in sequence, each holding
  then flowing into the next.
- **Clean FIFO queue.** Observing again (or triggering multiple thoughts) stacks
  cleanly — a `std::deque` of pending lines drained at a fixed cadence. Never
  overlapping, never dropped.
- **Dismiss:** auto-fade while walking. Because observing is now deliberate, the
  player chose to summon the thought — so it can hold a touch longer than the
  ambient version did.

## 3. Stateful thoughts + conclusions (the Dark Souls model)

The heart of the design, and where it lifts from Selva. **What a thing says when
you observe it depends on what else you've observed.**

### Tiered text (Selva's `lang`, curated)

Each encounter authors **tiered thoughts**, gated by which *other* observations
you've made:

```
stone:
  tier_0: "A stone, half-sunk in the grass."
  tier_1 (after observing the river):
          "Worn smooth by water that no longer runs here."
  tier_2 (after observing the ruins):
          "Someone set it here. A marker. For what?"
```

`resolve()` walks tiers high→low and returns the deepest tier whose gate is
satisfied — **exactly Selva's `lang::resolve`** (audited: `{tier_0, tier_1,
tier_2, unlock_node_tier_1/2}`, one `isUnlocked` seam). Re-observing after
learning more surfaces the deeper tier. This IS the Dark Souls stateful-dialogue
behavior: same object, different line based on accumulated world-state.

### Conclusions (Selva's `inference`, curated)

Observing the right **combination** of things forms a **conclusion** — a new,
higher-order thought:

```
conclusion "people_lived_here":
  requires: [observed_stone, observed_river, observed_ruins]
  thought:  "People lived here once. The water drew them; the water left."
```

A conclusion is a distinct, weightier reward (more currency, §5; a resonant
sound, §6). Selva's model: an inference node with a `requires` list, formed by an
explicit deduce action, **subset-matched** (having *more* than the required
observations still forms it). Wayworn's cut: **conclusions may form
automatically** the moment their required observations are all made (simpler than
Selva's manual Mind-page deduce), *or* via a light "reflect" action — Q2.

**What we take from Selva (audited against the shipped code, not its docs):**
- `lang` tier struct + high→low resolve + single `isUnlocked` seam. ✅
- Observation nodes fired from flags; the fired-set on the profile. ✅
- Conclusions = node with `requires` (subset match). ✅
- **Cut** (Selva's docs describe these but its *code never shipped them*):
  per-reading `warrant_evidence`, reconsider-cascades, the certainty mechanic,
  the Mind-pool resource, the three-stat trio. Don't port aspirational machinery.

## 4. The observation record

Every observation + conclusion made is **recorded persistently** (save data):
which, its text, where, when first made. Serves as:
- **A quiet reviewable page** — the game's "notebook," but it's *what you've
  noticed*, not a quest list. Reinforces attention as the substance.
- **The progression ledger** — the record is the source of currency earned (§5).

(The review UI is a later step; the record *data* is what §5 needs.)

## 5. Progression — two axes: stats you GROW, a tree you CHOOSE

**Stats rise by use; a Spirit tree is spent by choice. They never touch the same
numbers.** One axis is *who you're becoming* (habit); the other is *who you decide
to be* (intent). A "build" is the intersection.

**Stats (passive, use-based — Oblivion).** A stat rises from being *exercised*. WHICH
stat a thing exercises is a property of the *content*, not the verb: a thought names
its stat (its `faculty`), a deed names its stat — and either can be ANY stat, mental
or physical. A tracking observation may grow *survival*; a contemplative deed may
grow *wonder*. Verb and stat correlate (most observing is mental, most acting
physical) but it is never enforced — the author decides per thing, the system stays
neutral. No hierarchy: mind and body stats are peers, same curve; the "lore-accurate"
mind-heavy pilgrim is a *characterization the framing suggests*, not a mechanical
privilege (Souls-style — play the canon build or your own). Silent, automatic, no
menu — how you play *is* who you become. Stats are read everywhere (glow, roll
weighting, tier gates, craft quality); the tree never raises them.

**Spirit (currency, earned by noticing).** New observations grant it — deeper tier
> first tier, a conclusion most; re-reading an earned tier grants nothing (tracks
*new* understanding). A single fungible count, spent deliberately. Souls-esque, but
not lost on death and earned by attention, not killing.

**The tree (acquired, not leveled).** Nodes are unlocked, not raised (DESIGN.md:
"skills learned, not grown"). A node costs **Spirit + stat thresholds** — so your
grown stats *gate* which paths you can take. Observed all walk → observer paths
open; a low-perception pilgrim can't buy them however much Spirit he holds. **Habit
gates build.** This is where habit becomes identity (cf. the Mind-arc endings, and
KCD2's path commitments).

A node's **effect** is one of two general kinds (capabilities — bespoke new verbs —
are added ad hoc later, not part of the foundation):
- **modifier** — tweaks a number the game already reads (glow range, roll odds,
  tier-unlock threshold).
- **flag** — sets a world-state flag things react to (reuses the unlock/visibility
  flag system).

`BuffDef` (include/Growth.h) is the stub this grows from. Selva's
`stat = 1 + floor(log2(growth+1))` is a reference if a cost or use→stat curve needs
diminishing returns.

## 6. Sound design — the primary feedback channel

**With visual juice ruled out, sound carries the satisfaction.** This is a
first-class design element, not polish:

- **The observe moment** — a soft tone when a thought surfaces. A gentle bell /
  held note / breath, not a "ding." The sound *is* the reward.
- **Depth cues** — a deeper tier or a conclusion gets a distinct, richer sound
  than a first glance. Audio signals "this mattered more" without a number.
- **Conclusion unlock** — the "you connected the dots" moment gets the most
  resonant sound — the game's hushed equivalent of a level-up chime.
- **Currency gain** — a subtle audible accrual, felt not counted.

**Source intent: the Kira Stream OST.** Pull the observation tones from the
albums (a plucked note, a held tone, a soft swell) so the feedback is *of the
same musical world* as the ambient beds — the sound of noticing and the score
are the same voice. **Placeholder now** (a soft synthesized chime) so the loop
is feel-testable when built; replaced with Kira-sourced audio in an audio pass.

## 7. Build plan (reworking the prototype)

The prototype (ambient AABB-trigger + textbox + queue + fired-set + counter) is
**mostly reusable** — the textbox, queue, record, and currency counter all stay.
The rework is contained:

1. **Trigger: replace auto-AABB-enter with face-+-Space interact.** Find the
   encounter the player faces within range; fire on keypress. (Contained change
   to one function — the earlier `observations::update(state, x, y)` auto-check
   becomes an on-press "what am I facing?" query.)
2. **Tiered thoughts** — extend the encounter data from a flat line list to
   `{tier_0/1/2, unlock gates}` (lift `lang`'s Entry + resolve).
3. **Conclusions** — add `requires`-list nodes that form when their observations
   are all made; grant more currency + a distinct sound.
4. **Currency** — the counter becomes the spendable currency (skill-tree spend
   surface is a later step).
5. **Placeholder observe sound** wired to the interact.
6. **Interaction affordance** — a minimal facing-an-observable prompt.

Keep: textbox, queue, record/fired-set, persistence hooks, the Catch2 tests
(reworked for interact-trigger + tier resolve + conclusion formation).

## MVP scope — [LOCKED]

Guiding rule: **minimalist in spirit, a clean functioning machine in its parts.**
The MVP is three testable parts and nothing more:

1. **Observe** — face + Space fires the encounter you're facing (in range).
2. **Resolve** — tiered thought chosen by what you've already observed;
   conclusions auto-form when their required observations are all made; currency
   granted for *new* content only.
3. **Show** — textbox + queue + a placeholder observe sound.

The earlier open questions, resolved for MVP:

- **Affordance:** none. Face + press; observable things respond. No glyph/glint
  in MVP (add later only if playtest demands it).
- **Conclusions:** form **automatically** the moment their required observations
  are all made. No explicit "reflect"/deduce action, no Mind-page.
- **Skill tree:** **out of MVP.** Currency accumulates; *spending* it (the tree)
  is a separate later machine. MVP proves earning, not spending.
- **Perception-as-skill / hidden observables:** out of MVP.

Each part is independently testable (interact-trigger; tier resolve + conclusion
formation + currency grant as pure logic with Catch2; textbox/queue/sound by
running). That separability is the "clean machine in its parts" requirement.

## 8. The observation HUD (interact signal + the page) — the RPG's face

Guiding tension: **minimal as possible, affective as possible.** Strip
everything; what remains must land emotionally. AESTHETIC.md is the constraint —
"no exposed HUD stats during exploration, no objective markers, minimal." So the
observation UI splits into two very different things:

### 8a. The interact signal (always-on, whisper-quiet)

The one thing on the exploration screen. It tells you *there is something here to
notice* — because without it the mechanic is invisible (the playtest gap: "there's
nothing interactable"). But it is **not a blinking gamer-LED.** It's a **mood**:

- Soft, brief, easy to miss if you aren't paying attention — which is
  thematically perfect (the game rewards attention; the signal itself asks for
  it).
- A gentle **glimmer** on the thing when it's the active target (you're near it) —
  a soft world-space shimmer, not persistent chrome; it appears when relevant and
  fades otherwise.
- Its **brightness scales with the relevant faculty** — a stat renders the world
  through the character's eyes: a quiet world when the faculty is low, the world's
  subtler offerings surfacing more strongly as it grows. The same for every spot;
  the stat is the dial. So the signal is itself a reward for growing attention.
- Items are not glimmered this way — a thing lying in the world carries its own
  cue (see [INTERACTION-MODEL.md](INTERACTION-MODEL.md)).

Minimal, but affective: the world quietly *offering* something, not a UI element
demanding a button.

### 8b. The observation page (on-demand — the RPG core)

Where the RPG becomes legible. **Opened on demand** (a menu key), never on the
exploration screen — so the walk stays clean. This is the surface worth
investing in; it IS the RPG's face:

- **What you've noticed** — the record: observations made, their thoughts,
  where. A quiet ledger of understanding, not a quest list. (This is the game's
  "notebook," but it's *attention*, not tasks.)
- **Conclusions** — the higher-order thoughts you've formed by connecting
  observations. The most affective entries — the moments the pilgrim *understood*
  something.
- **Currency + (eventually) the skill tree** — the spend surface. Post-MVP for
  the tree; the page shows the currency now.
- **Register:** calm, spare, readable — a page you *sit with*, matching the
  contemplative tone. Not a stat screen; a record of a person coming to
  understand things.

### Build order (minimal-first)

1. **The interact signal first** — it's what makes the mechanic *usable* (the
   current gap). Minimal affective form: a soft signal when facing a new
   observable. This unblocks feel-testing the whole loop.
2. **The observation page next** — the on-demand record (noticed-log +
   conclusions + Spirit EXP). The RPG's face; where depth lives.

Both build on data that already exists (`State.observed`, `.formed`, the
observables/conclusions; Spirit EXP is owned by `GrowthState`, see
[GAME-SYSTEMS.md](GAME-SYSTEMS.md) §1) + the `facingObservable()` query already
written.

## Cross-references

- [MAP-ARCHITECTURE.md](MAP-ARCHITECTURE.md) §6 — quests = monologue; observables
  authored in the LDtk object layer.
- [AUDIT.md](AUDIT.md) §3 — selva `lang`/`insight` lift assessment.
- [DESIGN.md](DESIGN.md) / [AESTHETIC.md](AESTHETIC.md) — register; "skills
  learned not leveled"; "thoughts get deeper"; no objective markers.
- Selva shipped implementation (what we curate *from*): `insight/Insight.{h,cpp}`,
  `lang/Language.{h,cpp}`. **Build to the code, not `cognition-system.md`** —
  the docs describe warrant/cascade machinery the code never shipped.
