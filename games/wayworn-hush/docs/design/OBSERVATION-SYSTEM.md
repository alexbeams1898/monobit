# Wayworn Hush — Observation System

> **Owns:** the inner-monologue / observation mechanic and its role as the
> game's RPG progression spine. This is the system that makes "quests = the
> protagonist's thoughts" (MAP-ARCHITECTURE §6) concrete, and it is the game's
> primary progression currency.
>
> **Status:** design proposal for review. Nothing built until locked (this is
> core architecture — design before implementing).

## tldr

Walking into the vicinity of a place surfaces a thought — the protagonist
*notices* something. First encounters auto-surface in a soft textbox (thoughts
flow one into the next; new zones entered mid-thought queue cleanly), get
**recorded** in an observation log, and grant **progression** (observations are
the game's XP). Already-seen thoughts don't auto-interrupt again, but the player
can return to a place and re-read the thought deliberately. The system is a
recut of Selva Oscura's `lang` (tiered text) + `insight` (observation graph) +
cognition-stat model, curated for this game's gentler register — no combat, no
sangue, no offerings. **Attention is the progression.** Fitting, for a game
whose whole thesis is attention as the subject.

## Why this is core, not a side feature

The protagonist's thoughts are the narrative (ORIGINAL_NOTES: "mostly inner
thoughts of the character... helps him learn more about himself"). Tying
progression to *noticing* rather than fighting makes the mechanics say the same
thing the story says. This is the "leveling = attention, not combat-XP" idea
(cf. Selva's offerings-not-XP doctrine) taken further: here the noticing itself
is the currency, not a token you spend later.

---

## 1. The observation trigger (world → thought)

An **observation zone** is authored in the world (LDtk object layer, per
MAP-ARCHITECTURE): `{ id, x, y, w, h, thought }`, where `thought` is one or more
lines of the protagonist's inner voice.

- **Fires when the player enters the zone's vicinity** (AABB overlap, or
  facing-toward within range — see open question Q1). Ambient: no button press.
- **First encounter only auto-surfaces.** On first entry: surface the thought,
  record the observation, grant progression, mark fired. Re-entering later does
  **not** auto-interrupt (see §4).
- **Fires once per observation for the auto-pop + progression.** The record and
  XP are one-time; the *text* remains replayable (§4).

This is the `insight`-observation primitive from Selva, curated: an
event-driven flag that fires from world state. Wayworn's trigger kind is
"entered zone" (Selva had `examined`/`flag_set`/`kill_count`/etc. — we need far
fewer).

## 2. The textbox + queue (thought → screen)

A **minimal auto-textbox**, Mother-3 register (AESTHETIC.md): understated frame,
soft or instant text (no per-syllable clatter), low in the frame, quiet. Built
on the engine's `UIRenderer` primitives (no widget exists; small game-side box).

- **Auto-surface, auto-pass.** The thought fades in, holds briefly, passes — no
  keypress required. The player never stops walking.
- **Multi-line flow.** A thought with several lines/descriptions plays them in
  sequence, each holding then flowing into the next.
- **Clean queue.** Entering one or more new zones while a thought is playing
  **stacks** the new thoughts in a FIFO queue; they play out in order after the
  current one. Never overlapping, never dropped. The queue is the whole
  scaling story — "should be a simple system," and it is: a `std::deque` of
  pending thought-lines drained at a fixed cadence.

**Dismiss model:** auto-fade (moving). *(Open question Q2: whether standing
still holds the thought — the "dwell" variant. Deferred; auto-fade is the v1.)*

## 3. The observation record (the "you noticed this" log)

Every fired observation is **recorded persistently** (save data) — Selva's
observation-log, curated. The record holds: which observation, its text, where
(zone id / region), when first noticed. This is:

- **A safety net** — if the player misses the auto-pop (walked through fast),
  the observation is still logged; nothing is lost.
- **Reviewable** — a quiet menu page listing what the protagonist has noticed
  (the game's closest thing to a "journal," but it's observations, not quests).
  Reinforces attention as the game's substance. *(UI is a later step; the record
  data model is what §5 needs now.)*
- **The progression ledger** — the record IS the XP source (§5).

## 4. First-encounter vs. replay (the detail you flagged)

- **First encounter:** auto-surface + record + progression. (New = the thought
  intrudes gently, once.)
- **Subsequent visits:** the observation is already recorded, so it does **not**
  auto-pop (no interruption, no double-XP). But the player **can return to the
  place and have the thought again** — deliberately, on their terms. Mechanism:
  re-entering an already-fired zone offers the thought as a *replayable* beat
  (open question Q3: does it replay on re-entry automatically-but-silently-
  logged-as-not-new, or only on an explicit "look again" input?). Either way:
  **no XP, no record-change** on replay — replay is for the player's sake, not
  progression.

This mirrors Selva's locked rule exactly: *"Re-examining a tree you already
noted gives nothing. Firing a new observation gives [growth]."* Wayworn keeps
that rule and adds the replay-in-place affordance.

## 5. Observations as progression (the RPG spine)

**Observations are the primary progression currency.** This adapts Selva's
cognition-system-v1 (Perception/Cognition/Intelligence stats grown by cognitive
acts) — but cut down for this game. Selva's full three-node cognition model
(observation → inference → reading, warrant-evidence, reconsider-cascades) is
**richer than Wayworn needs**; Wayworn is gentler and has no combat-parry or
cosmological-reveal gating to hang three stats on.

**MVP model — [LOCKED]: observations grant XP; XP accumulates; level grows.**

A concrete, legible loop:

- Each **new** observation grants XP (amount authorable per-observation; a
  sensible default for the common case, larger for weightier ones — config,
  not hardcoded).
- XP accumulates toward a level threshold; crossing it **levels up** the
  protagonist's awareness/level.
- Re-entering a fired observation grants **no XP** (fire-once, per §4).

This is deliberately the simplest working RPG loop — a real number that goes
up — chosen to prove the progression before adding nuance. What a level *does*
(gates deeper monologue tiers, skill acquisition, world-reading) is **deferred**:
the MVP just needs the observe → XP → level loop turning. The richer
possibilities (register-deepening via `lang` tiers, per DESIGN.md's arc of
self-knowledge; skill gates per "skills learned not leveled") layer on top of
this loop later without reworking it — XP/level is the substrate, and those
become things a level threshold unlocks.

**Explicitly NOT in the MVP** (avoid importing Selva's weight): no
Perception/Cognition/Intelligence trio, no inference/reading machinery, no
Mind-pool resource. One XP counter, one level. Curate up from there only when
the game asks for it.

## 6. Reuse from Selva — what lifts, what's cut

Per AUDIT.md §3:

- **`lang` (tiered string map) — lift, near-free.** Perfect for the
  register-deepening model (§5c): a thought has tier_0/1/2 text, deeper tiers
  unlock as awareness grows. Rename namespace, rewire one predicate.
- **`insight` (observation graph) — lift the spine, cut hard.** Wayworn needs:
  the observation-node concept, fire-once, the persistent fired-set, the
  "new firing grants growth" hook. Wayworn does NOT need: inferences, readings,
  warrant-evidence, reconsider-cascades, the `sangue`/`kill_count` triggers,
  the Mind-pool-as-brain-fatigue resource. Cut all of that. What remains is a
  small "observations fired + a growth counter" — much simpler than Selva's.
- **cognition-system-v1's full model — do NOT import wholesale.** Take the
  *principle* (cognitive acts drive growth; re-examining gives nothing) and the
  *stat-hook idea*; leave the three-node inference machinery in Selva.

## 7. Minimal build (what step 6 becomes, once this doc is locked)

The smallest thing that proves the feel, structured so §3/§5 slot in:

1. Observation zones (authored data; for the first test, anchor one to a rock —
   a trigger volume *beside* the rock, since the rock is collision not a
   standable tile).
2. Enter-vicinity detection → fire-once.
3. Auto-textbox + multi-line flow + clean FIFO queue.
4. The fired-set (in-memory first; persist to save next).

Progression (§5) and the reviewable record UI (§3) come after, as their own
steps — but the fire-once + fired-set built here is exactly the hook they need.

## Open questions (resolve before/while building)

- **Q1 — trigger shape:** MVP = **AABB-enter** (simplest). Facing-aware
  ("noticing what you look at") is a later refinement.
- **Q2 — dwell:** deferred; **auto-fade** v1.
- **Q3 — replay trigger:** MVP = re-entry **auto-replays silently** (thought
  pops again, no XP, no record change). Explicit "look again" input is a later
  refinement if replay-on-every-pass feels naggy.
- **Q4 — progression axis:** **RESOLVED** — observations → XP → level (MVP).
  See §5. What a level unlocks is the deferred richness.
- **Q5 — skills:** DESIGN.md's "skills acquired not leveled" — likely a level
  threshold unlocks them, but that's post-MVP. Not in scope now.

## Cross-references

- [MAP-ARCHITECTURE.md](MAP-ARCHITECTURE.md) §6 — quests = monologue; observation
  zones authored in the LDtk object layer.
- [AUDIT.md](AUDIT.md) §3 — selva `lang`/`insight` lift assessment.
- [DESIGN.md](DESIGN.md) / [AESTHETIC.md](AESTHETIC.md) — the register + the
  "thoughts get deeper" arc this system serves.
- Selva canon (for what we're adapting *from*, not copying):
  `games/selva-oscura/docs/design/cognition-system.md`.
