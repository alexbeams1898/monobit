# Cognition system

The mechanical model of the Vagrant's thinking. Replaces the
prior "auto-confirmation" mechanic (deprecated). Locked 2026-06-10.

This doc covers:

- The three-node model: observations, inferences, readings.
- The act of Deducing.
- The act of Reconsidering and the cascade.
- The warranted-vs-unwarranted state of an inference.
- The three Mind-section stats (Perception, Cognition, Intelligence).
- How cognitive engagement grows each stat.
- The Mind resource pool and its derivation.

Related docs:

- [classes.md *Mind: the resource pool vs the cognitive stats*](classes.md)
  -- where the stats live in the Stats struct.
- [setting.md *Mind is the parallel cognitive axis*](setting.md) --
  why this is cosmologically OK alongside the substance economy.
- [insight_revelation_system.md *Insight unlocks grow the cognitive
  stats*](insight_revelation_system.md) -- how the insight system
  events feed cognition.

## The three-node model

The Vagrant's cognitive state is a graph of three node types
(structurally; only two are stored, the third is a property of an
inference):

### Observation

A fact the Vagrant has gathered from the world. Sensory or testimonial.
Examples: examining a tree and noting it has been dead a long time;
hearing the Guide say he poisoned the carcass; counting the larvae at
the shore.

- Immutable once gathered.
- Cannot be "wrong" -- an observation is what the Vagrant noted; the
  noting itself happened.
- Does NOT decay or vanish on its own.
- Tracked in `unlocked_insights` per character.

### Inference

A reasoned node the player built from other nodes (observations
and/or other inferences). Inferences are the player's interpretation
of what their gathered facts MEAN.

An inference has:

- **A set of linked evidence** -- the specific observations (or
  inferences) the player chose at Deduce time. Stable once committed
  unless the inference is Reconsidered.
- **A current reading** -- which of the authored interpretations the
  player picked. Swappable via Reconsider.

Inferences are NOT auto-promoted by the system. The player chooses
their reading. The system never silently rewrites the player's
interpretation.

### Reading

A reading is a property of an inference, NOT a separate node type.
Each inference has 3-5 authored readings -- different ways the
inference could be interpreted, ranging from naive-wrong to most-
correct given the full cosmology.

Each reading carries:

- The prose interpretation in the Vagrant's voice.
- A `warrant_evidence` set: the specific observations that warrant
  this reading. The most-correct reading typically has the most
  demanding warrant set.

The reading the player picks becomes the inference's displayed
interpretation. The reading's warrant-evidence set is what determines
whether the inference is **warranted** (see below).

## Warranted vs unwarranted

The state of an inference is **derived**, not stored. Calculated each
time the system needs to display the inference:

- **Warranted**: the player's linked evidence set exactly equals the
  current reading's `warrant_evidence` set.
- **Unwarranted**: anything else. Either too few observations linked
  to support the reading, or wrong observations linked, or a reading
  picked that's more specific than the evidence supports.

This is observable to the player visually:

- Warranted inferences render inked, solid, weighty.
- Unwarranted inferences render softer, dimmer, provisional.

The game does not annotate "you are wrong" or "you are right." The
visual state IS the feedback. A player who notices their inference
renders dim has a signal that their reasoning isn't yet supported by
what they've gathered.

Unwarranted inferences are NOT punished. They can be:

- Held indefinitely as the player's current interpretation.
- Used as evidence for downstream inferences (the player can build
  trees from unwarranted foundations -- humans do this all the time).
- Reconsidered later when new evidence arrives or the player notices
  the disconnect.

## Deducing

The act of constructing an inference. Happens on the Mind sub-page
workbench:

1. Player selects 2 or more observations (or inferences) on the
   workbench.
2. Player clicks Deduce.
3. The system identifies which inference node these observations
   match (the inference whose `requires` is a subset of the selected
   set).
4. The Reading Picker modal opens, listing the 3-5 authored readings
   for that inference. Each reading shows its prose.
5. Player picks a reading.
6. The inference is placed on the workbench at the centroid of the
   selected observations. Its `linked_observations` = the player's
   selected set. Its `reading` = the player's pick.
7. Edges are drawn from the inference to each linked observation.
8. Cognitive stat growth fires (Cognition gain; Intelligence gain if
   warranted -- see below).

## Reconsidering

The act of changing an inference's reading without rebuilding the
inference. Happens on the workbench:

1. Player clicks an existing inference on the workbench.
2. Reconsider button enables.
3. Player clicks Reconsider.
4. The Reading Picker reopens with the current reading highlighted.
5. Player picks a different reading.
6. **CASCADE**: any downstream inferences that depended on this one
   are released entirely (removed from the workbench, their cognitive
   stat gains reversed).
7. A confirmation modal lists the downstream inferences that will be
   released. Player confirms or cancels.
8. On confirmation, the cascade fires, the new reading is set, and
   cognitive stat growth is recomputed (the inference itself
   re-evaluates: was the old reading warranted? Is the new one?
   Adjust Intelligence accordingly).

Reconsider is the only way the player updates an inference's reading
during play. The system never auto-promotes.

## The cascade rule

When an inference is Reconsidered (or Released entirely):

1. Find all inferences whose `linked_observations` (or
   `linked_inferences`) includes this inference.
2. Recursively for each of those, find their dependents.
3. The full transitive-closure set is the cascade set.
4. All cascaded inferences are removed from the workbench.
5. The cognitive stat gains from those inferences are reversed.

This is jarring by design. It honestly represents that changing your
mind about a foundational belief invalidates everything you built on
top of it. The player rebuilds whatever they still want, with the
new foundation, and reconsiders what new readings make sense.

Observations are NEVER cascaded. They're facts; they don't depend on
anything. Only inferences cascade.

## The Mind-section stats

Three universal-base stats live alongside the body four
(STR/DEX/END/LCK). They grow from specific cognitive acts and they
affect specific in-game systems.

### Perception

- **Grows from:** new observation firings.
- **Affects:**
  - **Combat reading**: parry / dodge timing windows widen; enemy
    telegraphs become more legible at higher Perception.
  - **Examine depth**: more environmental detail surfaces. Some
    examinables have tier-2 or tier-3 information that only appears
    at higher Perception (e.g., closer inspection reveals
    discoloration the lower-Perception observer misses).
  - **NPC subtext**: characters lying, hesitating, or implying things
    becomes visible to the player as additional dialog lines or
    interior-voice notes.

### Cognition

- **Grows from:** every inference Deduced, regardless of whether the
  reading is warranted.
- **Affects:**
  - **Mind pool MAX**: the resource pool's maximum capacity is
    derived from Cognition. Higher Cognition = larger pool.
  - **Mind pool regen rate at rest**: faster recovery at vestigia.

Cognition rewards the ACT of thinking. Even sloppy reasoners who
infer often grow Cognition. The Vagrant's mind gets bigger from use.

### Intelligence

- **Grows from:** warranted inferences (reading matches evidence).
- **Affects:**
  - **Cognitive ability potency**: scales the damage / range /
    duration of incantations, weapon arts, miracles. Multiplies with
    the class-specific scaling stat (Faith for Heretic, TBD for
    others) for the final effect.
  - **Multi-choice hints**: at higher Intelligence, the Reading
    Picker shows hints about which readings have stronger evidence
    support given the player's current observation set.
  - **Cosmological-reveal gating**: certain late-game reveals
    (Beatrice's role, the specific shape of Lucifer's act, etc.)
    surface to the player only at sufficient Intelligence -- the
    Vagrant who has reasoned correctly is ready to perceive the
    deeper layer of the cosmology.

Intelligence rewards the QUALITY of reasoning. The player who reasons
carefully grows Intelligence; the player who reasons sloppily doesn't.

### Diminishing returns

All three stats grow with diminishing returns. The first inference
gives more Cognition growth than the hundredth. The curve shape is
authored and tuned by play; the principle is that early engagement
is highly rewarded and later engagement provides smaller but ongoing
growth, preventing late-game stat ceiling-running.

### Reconsider cost

When Reconsider triggers a cascade, the cognitive-stat gains from
the cascaded inferences are reversed. The player pays the cost of
the cognitive work they're throwing away. They can immediately
re-Deduce with new readings; the rebuilt inferences contribute
gains again. Net: Reconsider is mechanically expensive precisely
when it's a major restructuring.

This makes Reconsidering a real choice. Players won't reconsider
casually; they'll do it when they really think they were wrong.

## The Mind resource pool

A per-character resource with a current/max pair. NOT a stat -- a
resource alongside HP / Stamina / Poise. Displayed on the Form
sub-page as a bar.

- **Max** = derived from Cognition stat (formula TBD; principle:
  monotone increasing).
- **Current** depletes from cognitive-act spending: incantations,
  weapon arts (for those that cost Mind), reasoning aids.
- **Current** recovers at vestigia (rest sites). Like real brain
  fatigue: you rest, you recover. The MAX itself only grows from
  Cognition growth, which comes from cognitive engagement.

The Mind pool is what the player physically SPENDS. Cognition is the
stat that determines how much they have to spend.

## What this replaces

This system replaces the prior auto-confirmation mechanic (the
`confirmed_by` flow). That mechanic is deprecated:

- Existing `confirmed_by` arrays in insight JSON can stay; they no
  longer drive behavior.
- The `tryConfirm` API in `selva::insight` is dead code; no UI
  surface calls it.
- The `certain_conclusions` field on PlayerProfile is dead data; new
  saves won't populate it; old saves can keep it for back-compat.
- New inference authoring uses readings + warrant_evidence instead.

The new model is honest to cognition: the player CHOOSES their
interpretation; the system shows whether the evidence warrants it;
the player decides whether to revise.

## Authoring discipline: reading-cap

**Each inference's readings cap at what its own evidence can warrant.**
A reading cannot use vocabulary, knowledge, or framing that the
Vagrant could not honestly possess given the evidence available at
that inference's tier.

The implied-lesson doctrine forbids the Vagrant's interior voice
using unearned vocabulary. Readings are the Vagrant's interior
voice. The same rule applies.

**Example.** `infers_meat_state` has three readings:

1. **`meat_simply_spoiled`** — needs `[lupa_felled, carcass]`. Naive
   naturalistic reading.
2. **`meat_tampered_unknown_hand`** — needs `[lupa_felled, carcass,
   carcass_tampered]`. Acknowledges a hand; refuses to name one.
3. **`meat_poisoned_by_guide`** — needs `[lupa_felled, carcass,
   carcass_tampered, guide_confessed]`. The Guide is named because
   he named himself.

There is NO fourth reading at this inference that references "another
hand behind the Guide" or anything Beatrice-tier. The Vagrant has no
evidence of Beatrice at this point, so a reading invoking her would
be the game telling the player something they haven't earned. Bad
authoring.

**Cosmological deepening happens via NEW inferences, not via reading
progression.** When the player eventually gets Beatrice-evidence,
they Deduce a NEW inference (something like
`infers_guides_will_was_arranged`) whose readings explore the
conditions-shaping mechanic. That new inference's existence then
invites the player to Reconsider `infers_meat_state` -- not because
its readings have changed, but because the player now understands
their old reading in a deeper context.

This is the elegant property: **the cognition system's
Reconsider+cascade mechanic IS the mechanism for retroactive
cosmological deepening.** It happens through player-driven revision
of inferences they built on a now-revised foundation, never through
a single inference's readings reaching impossibly far.

**Authoring rule, summarized:**

> A reading is what the Vagrant could honestly think given the
> linked evidence. Never more. The game's deeper truth surfaces
> through chained inferences and Reconsiders, not by stretching a
> single inference's readings beyond their evidence-tier.

## Mind sub-page UX doctrine

Locked 2026-06-11 after iteration through drag-drop, click-to-pick-up
with a Move button, all-context-menu, and other shapes. The shape
below is the one that landed -- it earned its keep by surviving the
friction tests, not by being elegant on paper. Re-litigation should
require new evidence (a real workflow that breaks under the current
model), not aesthetic preference.

The page is a thinking surface. Two spatial regions (library on the
left, workbench canvas on the right) plus one prose surface (info box
below them) plus a transient status strip below that. Three gestures
drive every action; each gesture does ONE thing.

### Gesture taxonomy

- **Click** (Mouse left / A / Cross) -- **move**. On library: pick
  up an observation to add to the canvas. On workbench: pick up a
  node to relocate it. While carrying: click the canvas to place,
  click the library to return (lock-respecting).
- **Shift+click** (Mouse left + Shift / LB + A / L1 + Cross) --
  **toggle in selection**. The dedicated marking gesture. Workbench
  only; library shift+click is a no-op (library items aren't
  selectable). Selection survives moves -- mark several
  observations, rearrange them spatially, then act on the marked set.
- **Middle-click** (Mouse middle / Y / Triangle) -- **open context
  menu**. The page's only action surface. Menu items populate
  dynamically based on what's hovered + what's selected: Infer,
  Reconsider, Add evidence, Return to library, Deselect, Cancel
  carry.

There is no persistent action row, no Move button, no toolbar. The
context menu IS the action layer. Every cross-input mapping has a
clean controller analog -- the UI code doesn't change when the
gamepad path gets wired; only the input source does.

### The single read-surface

The info box below the panels is the page's ONLY prose surface. No
hover tooltips on either library or workbench. Hovering a node
updates a page-wide `mind_hover_id` tracker; the info box renders
that node's prose. Content resolution priority: hovered > exactly-
one-selected > carrying > placeholder.

The pinned-tooltip pattern we tried before this lost on two counts:
(1) tooltips imply ephemerality and the player can't refer back to
them while clicking; (2) two read-surfaces (tooltip + info box)
fight for the same job. One surface wins.

### Selection is explicit and stable

Selection is changed only by shift+click (add/remove) or the context
menu's Deselect. A regular click NEVER changes selection. This means
the player can rearrange marked observations without losing the
marks -- "select three observations, move one of them to group with
the other two, then middle-click Infer" is a single coherent
workflow.

Clicking empty canvas while not carrying clears the selection. That's
the only implicit deselect path; everything else is opt-in.

### Gesture-teaching does NOT live on the page

The page renders mechanic-teaching strings (Click to move, Shift+
click to select, Middle-click for actions) ONLY in a future global
help mode -- a pause-menu Help screen modeled on Elden Ring's. The
Mind page itself shows only transient state in the status strip
(`Carrying: <name>` or `N selected`); when nothing transient is
true, the strip is empty.

The reason: teaching strings on the page pollute the read. The
player goes to the Mind page to THINK; they don't want a control-
hint reminder fighting with the inference prose for attention. The
help mode covers the global teaching surface for the whole game;
it's the right home for this content.

### Context menu spatial behavior

The menu opens leftward from the cursor (anchor pivot `(1.0, 0.0)`)
so its body sits to the left of where you clicked. This leaves the
info box's space (below the canvas, full width) and the canvas
itself uncluttered. ImGui auto-clamps to the screen if the cursor
is near the left edge.

### What this replaces

- **Drag-drop.** Lost because the source node didn't visibly "travel"
  with the cursor -- a custom circle-preview helped but the gesture
  was still ambiguous with click-to-select.
- **Click-to-pick-up + Move button.** Lost because the Move button
  was a third interaction the player had to learn just to disambiguate
  workbench-click from selection-click.
- **All-context-menu** (Add to selection / Remove from selection as
  menu items). Lost because adding to selection became a 2-step
  ritual (right-click, pick menu item) for what should be a single
  gesture.
- **Right-click for context menu.** Lost because right-click is the
  global "close pause menu" gesture and would eat-and-close the
  popup on the same frame.

The shift+click + middle-click model is the synthesis: separate
gestures for separate jobs, controller-friendly, no modes to
forget, no buttons to teach.

## Open questions

- **Diminishing returns curve shape.** Linear-then-asymptotic?
  Logarithmic? Tuned by play.
- **Intelligence ability scaling formula.** Multiplicative with the
  class scaling stat? Additive? TBD when first abilities ship.
- **Perception combat formula.** Window widening per point of
  Perception? TBD when combat-Perception integration ships.
- **NPC subtext authoring overhead.** Each NPC needs subtext lines
  that gate on Perception. Real authoring commitment; not yet
  scoped.
- **Cosmological-reveal gating thresholds.** Which Intelligence
  values gate which reveals? Tuned with story progression.

## Cross-references

- [classes.md](classes.md) *Stat schema model* + *Mind: the resource
  pool vs the cognitive stats*.
- [setting.md](setting.md) *Mind is the parallel cognitive axis*.
- [insight_revelation_system.md](insight_revelation_system.md)
  *Insight unlocks grow the cognitive stats*.
- Memory: [[project_mind_resource_doctrine]] -- earlier lock,
  superseded in detail by this system but the cosmological frame
  carries forward.
- Memory: [[project_cognition_system_v1]] -- the current lock.
