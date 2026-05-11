# Selva Oscura — Development Pillars

The non-negotiable principles that shape how the game is built. These sit
above gameplay design, above engineering style, above tooling: they
govern HOW work happens, not WHAT we work on.

If a proposed change conflicts with a pillar, the change loses. If two
pillars conflict, the earlier-listed one wins.

---

## 1. Simplest possible foundation, built up incrementally

**Every new feature starts at its simplest viable form.** A movement is
a single clip on a single button — no physics, no chaining, no edge
cases. A combat action is one attack hooked to one input. A camera
behavior is fixed-distance follow. The first version of any system
does ONE thing and is provably correct at doing that one thing.

**Then we layer.** Once the foundation is in place, in-game, and feels
right at the most minimal level, we add the next thinnest layer. Edge
cases get added one at a time, each in its own pass, each tested
before the next is layered on. A jump goes: clip fires on input → adds
ground detection → adds Y-arc → adds jump-attack chain → adds context-
sensitive variants. Never all five at once.

**Why this matters:** every system in Selva Oscura that had trouble in
development was one that grew faster than it was understood. The
animation transition stack went through Path-A → Path-B → Path-A
because layers got added without confirming each was needed. The
leg-spasm bug class persisted for months because multiple bridging
mechanisms were stacked without isolating which one was load-bearing.
Built layer-by-layer, with each layer's purpose proven before the
next, those classes of confusion don't form.

**Signs you're violating this pillar:**
- "Let me also handle X while I'm in here." (No — file X for later.)
- "This needs a system to manage Y first." (Probably not. Try without.)
- "We'll need this anyway when Z lands." (Maybe. Add it when Z lands.)
- Proposing a fix that introduces three new files. (One file. Maybe.)
- A first version that already supports configurations you haven't
  decided on yet.

**Operational rule:** when starting a feature, write down what the
single thinnest viable version is. Build only that. Ship it. Play it.
Decide what's missing. Build only the next thinnest layer. Repeat.

---

## 2. Root cause over symptom, every time

When a bug surfaces, fix the assumption that broke, not the visible
behavior. A foot teleport gets fixed at the data-flow level, not by
clamping the foot's position. A leg spasm gets fixed by structural
separation of bridging mechanisms, not by tuning timing constants.

**Bandaids are anti-patterns, not options.** If the first proposal
addresses the symptom, the proposal is wrong — re-derive it from the
underlying assumption.

When a fix lands, the bug class is gone, not just this instance.

---

## 3. Prove with data, never theorize past the evidence

When a bug's cause is unclear, add diagnostic logging first. Read what
the system actually produces, not what it should produce. Most
animation bugs in this project have been caught by reading per-frame
world positions — values sitting in the log that I read past for
sessions because I was theorizing instead of looking.

When data contradicts theory, the theory is wrong. Update the theory.

---

## 4. Subtract before adding

Every new feature carries debt. Before adding a system, check whether
an existing one already covers it (sometimes badly — that's OK, the
fix is to improve it). Before adding a tunable, check whether existing
ones can be repurposed. Before adding a track, channel, layer, or
slot: ask whether one of the existing ones isn't pulling its weight.

The animation transition stack is +28 lines net across the harmony-
rule refactor, but ~600 lines of code were both added AND removed in
the process — the architecture is structurally cleaner because every
mechanism that exists has a single clear job.

---

## 5. Tripwires for invariants

When a structural rule is established (the harmony rule, root motion
separation, etc.), add a runtime log that fires if the rule is
violated. The rule isn't just doctrine — it's enforced by
self-detection. Future regressions can't hide.

Example: `[!!! OVERLAP]` in PoseSampler logs if inertialization ever
fires during an active loco crossfade. If that line ever appears in
combat-debug.log, a code change has re-introduced the failure mode.

---

## 6. Author the simplest content first

This applies to art, music, dialog, encounter design. The first
version of any content is the rough, single-pass version. We play
through with placeholder assets, identify what's missing, and iterate.
We don't author final-quality assets until the rough version has
proven the design.

This is asset-side mirror of pillar 1.

---

## 7. Pacing: small victories

Long-horizon work is broken into small, shippable victories. Each
session ends with something playable and demonstrably better than the
previous session. We don't accumulate three weeks of work-in-progress.

See memory `feedback_3d_pacing.md` for the operational rule.

---

## Anti-pillars

Things that are NOT pillars, and that we explicitly reject:

- **Engineering elegance for its own sake.** If the codebase looks
  unprincipled in places but ships a fun game, that's the right
  tradeoff. We refactor when fragility blocks work, not preemptively.
- **Feature parity with reference games.** Selva Oscura is not
  Souls/Sekiro/ICO. It draws from them; it doesn't copy. Features
  ship because they serve this game, not because reference games have
  them.
- **Tooling perfection.** The build system, lint pipeline, and test
  harness are good enough. Time spent there beyond "good enough" is
  time not spent on the game.
