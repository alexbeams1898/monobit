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

Each observable authors **tiered thoughts**, gated by which *other* observations
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
- **A quiet reviewable page** — the game's "journal," but it's *what you've
  noticed*, not a quest list. Reinforces attention as the substance.
- **The progression ledger** — the record is the source of currency earned (§5).

(The review UI is a later step; the record *data* is what §5 needs.)

## 5. Progression — souls-esque currency + skill tree

**Observing grants a soul-like currency; you spend it at a skill tree.** This
replaces the earlier "XP → level" MVP and is *simpler* than Selva's three-counter
cognition model (one currency, not three stats).

- **New observations grant currency.** A first-tier notice grants a small
  amount; reaching a **deeper tier** grants more; forming a **conclusion** grants
  the most (connecting dots is the higher-order act). Amounts are authored per
  observation/conclusion (config, not hardcoded). Re-reading an already-earned
  tier grants nothing — currency tracks *new understanding*, not repetition
  (Selva's rule: "re-examining gives nothing").
- **Currency spends at a skill tree.** Skills are acquired, not leveled
  (DESIGN.md: "skills learned, not grown") — the tree is a set of unlockable
  nodes bought with observation-currency. What skills *are* (cold-crossing,
  plant-lore, wildlife-calming per DESIGN.md) is separate design; this system
  provides the **currency and the spend surface**.
- **Souls-esque, curated:** like Dark Souls souls — a single fungible currency
  earned by the core verb, spent deliberately. Unlike souls: not lost on death
  (no death register here), and earned by *noticing*, not killing.

Selva's stat-derivation math (`stat = 1 + floor(log2(growth+1))`, diminishing
returns) is a good reference if a skill-tree cost curve needs one, but the
currency itself is a plain accumulating count.

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
   observable the player faces within range; fire on keypress. (Contained change
   to one function — the earlier `observations::update(state, x, y)` auto-check
   becomes an on-press "what am I facing?" query.)
2. **Tiered thoughts** — extend the observable data from a flat line list to
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

1. **Observe** — face + Space fires the observable you're facing (in range).
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

## Cross-references

- [MAP-ARCHITECTURE.md](MAP-ARCHITECTURE.md) §6 — quests = monologue; observables
  authored in the LDtk object layer.
- [AUDIT.md](AUDIT.md) §3 — selva `lang`/`insight` lift assessment.
- [DESIGN.md](DESIGN.md) / [AESTHETIC.md](AESTHETIC.md) — register; "skills
  learned not leveled"; "thoughts get deeper"; no objective markers.
- Selva shipped implementation (what we curate *from*): `insight/Insight.{h,cpp}`,
  `lang/Language.{h,cpp}`. **Build to the code, not `cognition-system.md`** —
  the docs describe warrant/cascade machinery the code never shipped.
