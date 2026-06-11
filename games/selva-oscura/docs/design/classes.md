# Classes

> **Owns:** the player's class system — penitent / wretched / heretic,
> their evolutions, stat profiles, what each *feels* like to play.
> **Status:** structural locks; per-class mechanical detail TBD.

## Cosmological constraints (locked from setting.md / story.md)

The class system inherits structural rules from the cosmology. These
are not class-design choices; they are inputs class-design must
satisfy.

- **The Signing opens the imprint.** Accepting the Guide's Signing
  ritual at Beat 3 is Hell's first formal measurement. From that
  moment, the class-picker has an imprint Hell can grip. Contrapasso
  (the cosmological law per setting.md *Per-circle reactivity*) can
  land on him. The imprint-free state is lost.
- **Class evolution is the cumulative substance the Vagrant has
  participated in moving.** Every act in the world is sangue
  moving (per setting.md *Sangue and the law of substance*) — a
  shade killed, an item picked up, a move learned, a stat installed
  via the Crucible, a contrapasso restoring on a circle. All are
  accounting surfaces of the same substance event. The class-
  picker's evolution L1→L2→L3 fires when the **total substance
  installed in his imprint** meets the next form's requirements.
  Requirements may be expressed as stat thresholds + items + learned
  moves + abilities — all of these are just sangue-state checked at
  the next Crucible commitment.
- **Class-evolution is Hell loading itself into the Vagrant.** As
  each circle's contrapasso restores (per setting.md), some of the
  substance catches on the class-picker's imprint. By
  TRANSFIGURATION he carries all 9 circles' contrapasso-signatures
  in his substrate — *Hell, fully installed in him*. He is fit for
  the throne because he IS the substance Hell would now have on its
  throne. The TRANSFIGURATION arc is the entire game — class
  evolution is grooming-for-Satan-2 expressed mechanically.
- **The unburdened path is structurally different.** Substance moves
  *through* the unburdened Vagrant into Beatrice's reservoir (per
  setting.md *Sangue saturation of Beatrice*). His evolution L1
  Unburdened → L2 Svuotato → L3 Diaphanous fires on cumulative
  **riversamento volume** alone — not stat investment, not item
  collection, not move learning. **Stats stay at 1/1/1/1 baseline
  forever** (no imprint → no metabolic-fire → no installation). Each
  evolution stage unlocks **non-stat
  capability** — abilities, passives, and the development of his
  unique combat technique (stillness / interruption register).
  Nothing the unburdened gains is investable or numerical. Visual
  register is **subtractive** (he loses mass / becomes translucent
  / by Diaphanous is mostly outline) because the substance that
  would have built his body has gone elsewhere. Each riversamento
  that fires an unburdened evolution is *also* a contribution to
  Beatrice's transformation — the two ends of the same substance
  pathway.
- **Universal base schema is the four-stat quad (STR / DEX / END /
  LCK).** Same on every actor, fixed at compile time. Class
  differentiation rides on top — see *Stat schema model* below for
  the three-layer mechanism. Names are internal placeholders; Dante-
  coded display labels arrive with the canon-voice UI pass.
- **Entity symmetry is sacred.** Player, enemies, bosses are the same
  `Actor` struct. *Class* is data, not type. Per-class combat profile
  must work as a stat / ability / unlock-mask combination, not as
  call-site special-casing.

## The three classes

*Names locked: **Penitent**, **Heretic**, **Wretched**.* Per-class
fantasy, mechanical identity, and combat profile TBD. Constraints:

- Each class differentiates via the layers described in *Stat schema
  model* below: derived stats, class-specific abilities, optionally
  one new base-stat field gated by unlock-mask. The universal four
  (STR / DEX / END / LCK) stay shared.
- Per-class differentiation can be expressed in *how* the class
  processes contrapasso accretion — not *whether* it accretes. Same
  cosmological input, different processing, different evolution
  shapes.
- The halo is the Hell-recognition stamp received by Penitent and
  Heretic at L3 (per setting.md *Halo*). Wretched never receives one
  (his punishment is non-completion). The halo is bureaucratic, not
  sanctified.
- Penitent vertical slice is the current dev focus (per memory).
  Heretic and Wretched are deferred.

## Evolutions

**Class-picker evolution stages: L1 → L2 → L3.** Stage names per
class TBD (Penitent / Mantle, Heretic / Tomb, Wretched-equivalent
TBD).

- L1 = base form, immediately post-Signing. Stats start at universal
  baseline (1/1/1/1).
- L2 = mid-game. Triggered when the Vagrant's cumulative substance
  arrangement meets the class-specific L2 requirements
  (stat thresholds + items + learned moves + abilities). Commits at
  the next Crucible use. The substance has installed enough to
  support the next form.
- L3 = late-game, full imprint. The halo arrives at L3 for Penitent /
  Heretic. Wretched at L3 has no halo (his punishment is
  non-completion).

**Unburdened evolution stages: L1 → L2 → L3.** Locked at setting.md.
- L1 = Unburdened (base). Stats start at universal baseline
  (1/1/1/1) and **stay there for the entire run**. The unburdened
  never raises a stat number. The HUD is frozen on the floor.
- L2 = Svuotato (the emptied) — has routed a meaningful amount of
  sangue through into Beatrice. Triggered by cumulative riversamento
  volume. Unlocks the unburdened's distinctive combat-technique
  development + non-stat passives (specifics TBD; **no new stat
  fields are added by Svuotato**).
- L3 = Diaphanous (the translucent) — has routed most of what could
  be collected. The prerequisite for PURITY. Unlocks deeper
  riversamento-themed capability (specifics TBD; **no new stat
  fields**).

Visual progression: class-pickers gain mass / imprint detail
(substance accumulating in their substrate); unburdened loses mass
(subtractive — substance has gone through). Per-stage sprite
specifics TBD.

**A "true unburdened run" — total refusal — is mechanically
possible.** A player who refused the Signing AND never uses a
riversamento site stays at L1 Unburdened for the entire run. Stats
stay 1/1/1/1, unburdened-evolution never fires, neither Svuotato
nor Diaphanous unlocks. This is the absolute refusal — the path that
refuses Hell's measurement AND Beatrice's reservoir. Hard by design;
offers no progression mechanic at all.

**The Signing happens once, at Beat 4.** The Guide performs the
ritual once; the player accepts or refuses in that moment, and the
choice is committed for the rest of the save. A refused Signing
cannot be revisited later — there is no "carry the option forward
and pick later." The late-game Erasure can re-shape a class-picker
(switch class, or un-measure to unburdened) but cannot perform the
Signing on a refusing unburdened.

## Stat schema model

**Status:** WIP. Locks down once the first class (Penitent or
Heretic) is wired and the unlock-mask mechanism has been exercised
end-to-end.

The cosmological constraint above (contrapasso accretion contributes
to evolution) means stat growth is driven by *both* sangue
investment (player choice at the Crucible) *and* keeper-kills (passive,
imposed). The interaction of these two sources is open — possibly
keeper-kills unlock evolution *thresholds*, with sangue investment
determining where in the schema growth lands.

### The universal base — what every soul carries

Every actor in the game — Unburdened player, class-picker player,
every enemy shade, every keeper, every NPC — has the same fixed
schema of base stats, organized into two groups:

**Body (universal four):**

- **STR**, **DEX**, **END**, **LCK** — the substrate axes. Grow
  through Crucible-fire (sangue installation) for class-pickers;
  stay at 1/1/1/1 forever for the Unburdened, who refuses the
  installation channel. **All actors start at 1/1/1/1** at this
  floor.

**Mind (universal three):**

- **Perception**, **Cognition**, **Intelligence** — the cognitive
  axes. Universal across all classes including the Unburdened,
  because they grow from engagement (observing, inferring,
  reasoning correctly) -- NOT from substance installation. The
  Unburdened-stays-1/1/1/1 lock applies only to body stats; mind
  stats grow normally on all paths. **All actors start at 1/1/1/1**
  at this floor as well.

For the class-picker, body-stat growth from the floor is the
cosmological event of substance accumulating in the substrate (Hell
installing, via the Crucible's fire). For the Unburdened, **the
body floor is the ceiling.** Body numbers stay at 1/1/1/1 for the
entire run; the unburdened path has no metabolic-fire (no imprint)
to drive installation. Unburdened progression on the body side is
non-stat (riversamento → ability / passive unlocks; see
*Evolutions*, below).

Mind growth is independent of class for all paths -- see
[cognition-system.md](cognition-system.md) for what each Mind stat
does and how it grows. Briefly: Perception grows from observation
and affects combat reading / examine depth / NPC subtext;
Cognition grows from the act of inferring and drives the Mind
resource pool max; Intelligence grows from warranted inferences
(reading-matches-evidence) and scales cognitive ability potency.

Internal names for the body four match the engine's Souls-derived
quad for now; Dante-coded renames (Forza / Destrezza / Costanza /
Fortuna or thematic equivalents) land when the canon-voice pass on
UI is done. Mind stat names stay in English until the same pass.

### Class differentiation rides on top, three layers deep

A class never *replaces* the universal base. It *adds* layers above
it, each cheaper than the one before:

**1. Derived stats (the most common layer).** A class-specific value
computed from the universal base + class context. The Penitent's
**piety** might be `f(STR, END, contrapasso_accreted)`. The
Heretic's **cunning** might be `f(DEX, LCK, contrapasso_resisted)`.
These are display + scaling values, not new fields the player invests
in. They are how a class FEELS different on the same four numbers.

**2. Class-specific abilities / passives (own system).** Spells,
incantations, miracles, weapon arts, contrapasso-resistance passives.
Loaded from per-class JSON (sister to the enemy archetypes system).
Not part of `Stats`. The class-picker receives a starter ability
kit at the Signing; subsequent abilities unlock at L2 / L3 evolution.
The Unburdened has **no kit at L1**; abilities arrive only via
riversamento-gated unlocks at Svuotato (L2) and Diaphanous (L3) —
specifics described in *The Unburdened does NOT use the unlock-mask
for stats*, below.

**3. New BASE-stat fields, added rarely.** When a class genuinely
needs a base value that can't be derived from the universal four —
the canonical case being a magic-school-equivalent stat — that field
is added to the engine's `Stats` struct itself. It is present on
every actor from boot. But it is **gated by an unlock-mask**
(`stats_unlocked.faith = true`) and only appears in the HUD + at
the Crucible's stat-spend panel when the unlock fires.

### Concrete example: Heretic L2 unlocks Faith

The Heretic class-picker, at L2 evolution, gains the ability to cast
**heretical incantations**. Incantations scale with a new base stat:
**Faith**. (Names placeholder — Dante-coded version comes later.)

- **Pre-L2 Heretic, and every other actor in the game:**
  `stats_unlocked.faith = false`. The Faith row does not appear in
  the level-up panel. Incantations cannot be cast. The field exists
  on `Stats` but is invisible and unused.
- **At Heretic L2:** the bit flips to `true`. The Faith row appears
  in the Crucible's stat-spend panel. The player can invest sangue
  to raise it. Heretical incantations become available; their
  damage / range / cost scale with Faith.
- **Unburdened, Penitent, Wretched, every enemy:** never see Faith.
  The bit stays false for their entire run.

### The Unburdened does NOT use the unlock-mask for stats

The Unburdened evolution stages do NOT add new stat fields. There
is no Svuotato-stat, no Diaphanous-stat. This is the path that
*refuses* the cosmology's instruments for substance installation,
and a new base-stat field is exactly such an instrument
(numerical, investable, written-on-the-substrate). The cosmology
has no hook into the Unburdened; it can't add a stat to him.

What Svuotato and Diaphanous DO unlock:

- **Combat-technique development.** The Unburdened's distinctive
  stillness / interruption register, named in setting.md, emerges
  and refines through these gates. Mechanically: passives,
  ability modifiers, specific frames-of-immunity tied to the
  technique. Specifics TBD.
- **Path-specific passives.** Late-stage manifestations of
  substance-having-passed-through (e.g. intangibility frames as
  Diaphanous approaches; contrapasso-deflection because there's
  insufficient body to grip). These are passive states, not
  numerical stats — toggled by evolution stage, not invested in.

Per [Setting](setting.md) *Sangue saturation of Beatrice*, each
riversamento that fires an Unburdened evolution is also a
contribution to Beatrice's transformation. The Unburdened's stage-
unlocks and Beatrice's corruption are the **same cosmological event
expressed at two ends of the substance pathway.** The Unburdened
gets capability; Beatrice gets corrupted; substance flows; nothing
numerical happens on his stat sheet.

### Cadence rule for adding new base fields

The engine schema grows by **one field per class-picker evolution
unlock that genuinely needs a new base stat.** Concretely:

- Heretic L2 = +1 stat slot (Faith) for heretical incantations
- Penitent L2 = +1 more (whatever fits Penitent's L2 fantasy; TBD)
- Wretched L2 / L3 may unlock no new field at all if his ability set
  scales off the universal four (his punishment is non-completion —
  a class-fantasy reason to keep him on the base schema)
- **Unburdened stages never add stat fields.** Path doctrine
  refuses installation; new stat fields ARE installation. Svuotato
  and Diaphanous unlock capability (passives, ability modifiers,
  combat-technique development) without ever growing the numerical
  schema.

This keeps schema growth slow, deliberate, audit-able. New stat
fields are a per-class-evolution engineering act, not a runtime
accident. The engine `Stats` struct grows by ~5-6 total fields over
the game's lifetime (4 universal + 1-2 per class-picker class-L2 that
genuinely needs one). Every field is present on every actor from
boot, gated by per-stat unlock-mask bits.

### Why not a dynamic stat map

The structurally simpler alternative — `std::unordered_map<StatId,
int>` so any class can introduce any stat at any time — was
considered and rejected:

- Save/load gets messier; compile-time-fixed schema serializes
  trivially.
- Per-frame stat lookups have a hash-map cost; field accesses do
  not.
- Stat additions become per-class JSON edits anyone can do without
  review, which is exactly the wrong cadence — base-stat growth
  should be a deliberate engineering act, not data-config drift.
- The shape that gives the player "this class FEELS different" is
  mostly derived stats + abilities, NOT new base fields. Reserve
  base-field growth for the genuine cosmological-mechanical needs
  (magic-school-equivalents) only.

The fixed compile-time schema with unlock-mask is the right shape.

### Mind: the resource pool vs the cognitive stats

There are TWO distinct things in the Mind section of the design,
both cosmologically outside the substance economy:

1. **The Mind resource pool** -- a stat-shaped resource with a
   current/max pair, displayed as a bar on the Form sub-page next to
   HP / Stamina / Poise. Depleted by cognitive-act spending
   (incantations, weapon arts, cognitive reasoning aids); current
   recovers at rest sites / vestigia; MAX is **derived from the
   Cognition stat** (see [cognition-system.md](cognition-system.md)).
   The pool is NOT in the engine's `Stats` struct; it's a
   per-character resource alongside HP/Stamina.

2. **The Mind-section stats** -- Perception, Cognition, Intelligence
   -- which ARE in the universal-base `Stats` struct alongside the
   body four. They grow from specific cognitive acts (observing,
   inferring, reasoning correctly). They scale abilities the same
   way body stats do, and gate content reveals. See
   [cognition-system.md](cognition-system.md) for the full
   specification.

**Universal across all four classes, including the Unburdened.**
Both the resource pool AND the Mind-section stats grow on every
path. The Unburdened-stays-at-1/1/1/1 lock applies only to body
stats; cognitive growth has no Crucible dependency. Unburdened on
PURITY route especially benefits because their non-stat-based
capabilities (passives, ability modifiers, frames-of-immunity per
*The Unburdened does NOT use the unlock-mask for stats*, above) are
Mind-pool-spent and Intelligence-scaled.

**Mind pool powers cognitive abilities; class-specific scaling stats
multiply them.** Concretely for the Heretic at L2: Faith
(class-specific unlock-mask stat, raised at Crucible) scales
incantation damage / range / cost. Intelligence (universal Mind
stat) multiplies that further. Mind (this resource pool) is the
capacity spent per cast. Three-way scaling per ability:
*Faith × Intelligence* answers "how powerful is this cast";
*Mind pool* answers "do I have capacity to cast right now."

**UI placement:** Mind resource pool displays on the Form sub-page
as a bar. Perception / Cognition / Intelligence display on the Form
sub-page as integer stats alongside STR / DEX / END / LCK. The Mind
sub-page (insight graph + workbench) is where the cognitive
engagement HAPPENS; engagement there grows the Mind-section stats
that display on Form.

## Class identity: two layers, separated

Locked 2026-06-11. Answers the long-standing tension between "the
Vagrant arrives knowing nothing" and "the player needs to make an
informed mechanical choice at the Crucible." Resolves the question
by SEPARATING the layers cleanly.

### The two layers

**Mechanical identity is LEGIBLE.** When the player stands at the
Crucible at Beat 4, the picker shows what each option DOES in plain
terms: which stats it favors, what playstyle it enables, what the
power curve looks like. The player is not gambling on an inscrutable
label. They know they're picking the strength/endurance build vs
the cognitive build vs the precision build, the same way Souls
players know "Knight" is the heavy-armor melee start.

**Cosmological identity is VEILED.** The Vagrant does not know what
the Crucible IS. He does not know what an imprint does. He does not
know what "Sumerian", "Roman", "Greek" mean cosmologically. The
class names, the path names, the framing prose all arrive in their
tier-0 voice — the Vagrant's voice at this state, per the implied-
lesson doctrine. He sees a fire-vessel he's been told to commit
into. He understands the stakes operationally, not metaphysically.

These two layers COEXIST on the same screen. The Crucible picker
has both registers stacked:

- **Mechanical layer (player-space):** "+STR, +END" / "heavy melee,
  slow stamina, high poise" / "no stat boost, no penalty, slower
  power curve" — in plain English, the player's vocabulary.
- **In-fiction layer (Vagrant-space):** tier-0 prose the Vagrant
  could honestly think — "the path of weight and struggle" / "the
  path of seeing" / "to remain as you came." No "Roman", no
  "Sumerian", no "Greek".

The player reads both and reconciles. The Vagrant only inhabits the
second.

### Why this is OK (and not a violation of the implied-lesson doctrine)

The Crucible picker is a META-FRAME surface — it's the PLAYER
choosing, not the Vagrant. Same way the HUD sangue count and HP bar
don't pretend to be in-fiction. The mechanical labels live in the
player's space; the in-fiction prose lives in the Vagrant's space.
The implied-lesson doctrine constrains what arrives IN the
Vagrant's voice; it does not constrain UI chrome that explicitly
serves the player.

This is the same separation already at work elsewhere: the player
knows the HP bar shows damage capacity, the Vagrant knows only that
he feels weaker. The Crucible picker just makes the separation
visible at a decision point.

### Promotion: the class name itself grows through play

Class names live in the language map and tier-promote like every
other player-visible string. Authoring shape:

- **Tier 0** — the Vagrant's voice at Beat 4. Operational, no
  cosmological vocabulary. "The path of weight and struggle."
- **Tier 1** — after the Vagrant has accumulated relevant
  observations / inferences. Softer understanding, still no
  cosmological anchor. "The soldier's discipline."
- **Tier 2** — after the load-bearing cosmological insight fires
  (meeting an NPC who recognizes the path, finding an item that
  names it, completing a defining deed). The cosmological anchor
  arrives. "Roman martial discipline."

The SAME class identity gets RENAMED in the player's perception as
the Vagrant comes to understand what he picked. The mechanics never
change — only the language wrapping them. This is the insight
revelation system doing its normal job at the class-name surface.

Authoring rule: tier-2 must NEVER appear before the cosmological-
anchor insight has fired for THIS run. Tier-0 / tier-1 names are
written with the implied-lesson discipline; the player who never
unlocks the anchor never sees the Italian/historical name.

### Implication for Unburdened

The Unburdened path is the "did not commit" option. Mechanically:
no stat boost, no penalty, slower power curve, no commitment to a
path. That mechanical identity is legible from the start — the
picker presents it honestly.

Cosmologically the Unburdened's identity (relative to Beatrice, the
riversamento, the cycle) unfolds through play exactly like the
other paths. The tier-0 / tier-1 / tier-2 promotion applies; the
fact that there is no committed path doesn't exempt the Unburdened
identity from the same language-map treatment.

### Implication for the open questions below

Several of the open questions listed in the next section narrow
significantly under this doctrine:

- "How is class identity revealed?" — through tier-promotion of
  class names in the language map; same machinery as every other
  insight unlock.
- "Does the player know what they're picking?" — yes,
  mechanically. The Crucible picker shows mechanical identity in
  player-space.
- "How do we balance mystery vs confusion?" — mystery lives in the
  in-fiction layer (cosmological identity, what the path MEANS);
  confusion is avoided in the mechanical layer (the player always
  knows what their stats do).

What remains open is the AUTHORING work: writing each class's
tier-0 / tier-1 / tier-2 names, deciding which insights gate which
promotions, and what the load-bearing cosmological-anchor event is
per class.

## The trifecta and Unburdened: posture toward Hell

Locked 2026-06-11. The three class-picker paths are three distinct
cosmological postures toward Hell's measurement; Unburdened is the
fourth, asymmetric path (refuses the measurement entirely).

- **Penitent** — submits to Hell. Accepts the measurement; bears the
  contrapasso. Form WRAPS around the submission. L3 = Mantle.
- **Heretic** — refuses Hell. Won't be measured Hell's way; insists
  on his own shape. Form SEALS against the refusal. L3 = Tomb.
- **Feral** — consumes Hell's machinery. The act of committing
  rewrites the substrate; the Vagrant becomes less human, more
  beast. Each evolution stage is visibly more deformed. L3 form
  name TBD.
- **Unburdened** — refuses to engage entirely. Doesn't submit,
  doesn't refuse, doesn't consume. Routes sangue outward through
  himself toward the unknown destination (Beatrice, cosmologically;
  player learns this only via tier-promotion). Form thins:
  L1 (Unburdened) -> L2 (Svuotato) -> L3 (Diaphanous).

**Wretched / non-completion is retired.** The prior "third class as
failure-mode" framing was a thin third leg; Feral replaces it as a
real third cosmological posture (engagement-by-consumption). All
references to Wretched in older sections of this doc are stale and
need a scrub pass.

### Feral specifics

Per the [[contrapasso-shaped-soul-doctrine]]: the Feral's commit
rewrites his form because what he eats reshapes him. Other classes'
contrapasso accretion produces measurable inscription within a
human silhouette; the Feral's produces TRANSFORMATION. He becomes
the thing that ate the souls.

Mechanical implications:
- Body stats raise as normal via sangue commit (universal four).
- The Feral additionally accumulates the Feral acquired-stat (see
  next section) from animal-shaped behavior.
- L1 form: nearly human with subtle signs (sharper teeth, eyes that
  read wrong).
- L2 form: clearly inhuman (visible deformation, posture changes,
  hands becoming claws).
- L3 form: full-beast (TBD specific name; possibly Cerberus-coded
  per Dante's gluttony circle).
- Capability tradeoffs at higher tiers: gains beast-form combat
  capabilities (natural weapons, sprint, leap, sangue-direct-from-
  flesh on kill); loses access to certain human gear / dialog
  options as the form deforms. Trading options, not losing them.

### No halo for the Feral

Like Wretched (now retired), the Feral does not receive a halo at
L3. The cosmological reason differs: the Feral's L3 form is so
deformed that Hell's bureaucratic stamp has no surface to land on.
Hell cannot measure what won't hold still in the shape it was
measured into. The halo doctrine (Penitent and Heretic receive it
at L3; Unburdened never does; Feral never does) carries forward;
only the cosmological justification for the Feral case is new.

## Acquired stats: the cognitive hook into your own path

Locked 2026-06-11. Closes the loop between the class system, the
[cognition system](cognition-system.md), and the two-layer class
identity doctrine.

### Each path has ONE acquired axis

Independent of and orthogonal to the universal stats:

- **Body four (STR / DEX / END / LCK)** — raised by sangue commit
  (class-pickers) / locked at 1 (Unburdened).
- **Mind three (PER / COG / INT)** — raised by cognitive engagement.
  Universal across all paths.
- **Acquired axis** — raised by class-shaped behavior. ONE per
  path. Differs in name, in the behaviors that raise it, and in
  what it mechanically scales.

Two progression vectors are required for full progression: universal
stats + acquired axis. Anti-grinding-via-one-axis. A class-picker
who grinds sangue without playing in character has stat numbers up
and the acquired stat lagging. A class-picker who plays in character
without committing sangue has the acquired stat moving and the
universal stats flat. Both are required.

### Per-path acquired axes (working names)

| Path | Acquired axis | Raised by | Gates form evolution |
|---|---|---|---|
| Penitent | Piety *(working name)* | Tanking, enduring, ritual participation, submission-shaped behavior | L1 -> L2 -> L3 (Mantle) |
| Heretic | Cunning *(working name)* | Parrying, refusing offered terms, exploiting Hell's machinery, refusal-shaped behavior | L1 -> L2 -> L3 (Tomb) |
| Feral | Feral | Unarmed kills, deliberate-feast on corpses, predatory movement, animal-shaped behavior | L1 -> L2 -> L3 (final-form name TBD) |
| Unburdened | *(internal: riversamento volume)* | Routing sangue outward via commit | L1 (Unburdened) -> L2 (Svuotato) -> L3 (Diaphanous) |

Working names are tier-2 canonical names per *Class identity: two
layers, separated* above. Tier-0 / tier-1 versions need authoring.

### The acquired stat IS the cognitive hook (class-pickers)

When a class-picker commits at Beat 4, an unfamiliar HUD label
appears -- the acquired axis name at tier-0 (Vagrant-voice, no
cosmological vocabulary). The player didn't ASK for Piety. They
asked for "the endurance build." Now there's a word on their HUD
they don't recognize.

That mystery IS the natural curiosity that drives cosmological
discovery. The player goes looking -- NPCs, item descriptions,
Grimoire entries, examine prose. They form inferences on the Mind
sub-page about what the label might mean. Relevant insights fire.
The label tier-promotes through the language map. By the time it
reads at tier-2 (the canonical name), the player has genuinely
earned the cosmological vocabulary.

The acquired stat is its own hook into the insight system. It IS a
cognitive-system act, same as inferring about NPCs or the world --
but the subject is YOURSELF.

### The Unburdened asymmetry

**Unburdened has no HUD-visible acquired stat.** The riversamento-
volume counter exists internally and gates Svuotato / Diaphanous
evolutions, but is NOT exposed to the player as a labeled HUD axis.

This is cosmologically honest. The Unburdened path is the one that
REFUSES TO BE LABELED. Other paths have words for what they are
becoming; Unburdened doesn't. The mechanical absence of a HUD stat
mirrors the cosmological refusal-to-acquire.

The Unburdened's progression is read only through **form evolution**
-- the visible thinning of the body across Unburdened -> Svuotato
-> Diaphanous. The body IS the readout. No word for what is
happening; only the visible state.

This asymmetry is load-bearing for the trifecta-vs-unburdened
distinction: three paths name what they are becoming; one refuses
to.

### Authoring discipline

- The gating insights must be reachable through normal play. A
  Penitent player who tanks 20 hits should be able to find someone
  in the world (NPC line, item description, Grimoire entry, examine
  prose) that provides the observation that lets them infer "ah,
  this is Piety, this is what carrying-the-blow means
  cosmologically." Not hidden behind exotic events.
- The label staying at tier-0 is diegetically honest if the player
  doesn't engage. Form still evolves at thresholds; mechanics still
  work. The cosmological LEARNING is gated on cognitive engagement,
  not the mechanical progression itself.
- Behavior that doesn't match the class doesn't penalize. Wearing
  armor as a Feral, talking peacefully as a Heretic — these are
  NEUTRAL; they just don't contribute to the axis. Subtraction
  would feel punitive; absence-of-addition is honest.

### What's open

- Acquired-stat tier-0 / tier-1 / tier-2 prose for each class (9
  strings + their gating insights).
- Final names for Penitent's / Heretic's acquired stats (Piety /
  Cunning are working names; may revise).
- Final name for Feral's L3 form.
- Specific behavioral lists per class (what counts as in-character
  behavior, with thresholds).
- Specific mechanical hooks (what each acquired stat scales --
  damage reduction formulas, parry windows, unarmed damage, etc.).
- Whether the acquired stat's HUD-VISIBILITY itself tier-promotes
  (tier-0 = no number visible, just "(?)"; tier-1 = number visible
  without label; tier-2 = number + label). Or whether the number is
  always visible and only the LABEL tier-promotes.

## Open questions

- Per-class fantasy — what does each class *feel* like?
- Per-class combat differentiation within STR / DEX / END / LCK
  scaling (before considering class-specific stats).
- Which class-L2 unlocks introduce new base fields, and which stay
  on the universal four? (Heretic = Faith locked above. Penitent
  and Wretched TBD.)
- Exact derived-stat formulas per class.
- Whether the unlock-mask is per-stat booleans or a single
  `class_evolution_level: int`. (Working answer: per-stat boolean
  — more granular, simpler engine code.)
- Whether derived-stat values are recomputed per frame, cached on
  stat-change, or only on level-up. (Working answer: cache on
  stat-change; cheap and predictable.)
- Stage-name for Wretched at L3.
- How contrapasso accretion expresses mechanically — does it auto-
  invest into specific stats? Modify class-specific behaviors? Unlock
  evolution gates? *Decision deferred to gameplay tuning.*
- Whether SURFEIT (all-stats-maxed) is reachable on all three
  classes equally, or whether one class hits the cap most easily.

## Cross-references

- [Setting](setting.md) — *The unjudged*, *The Vagrant*, *Per-circle
  reactivity* (contrapasso doctrine), *Endings* (TRANSFIGURATION /
  SURFEIT / REFUSAL trigger conditions).
- [Story](story.md) — narrative arc, R2 (the reveal that class
  evolution has been Hell loading itself in).
- [PC vs NPC](pc-vs-npc.md) — symmetry rule, *class* is data not
  type.
- [Character creation](character-creation.md) — Signing mechanism,
  class pick at the beasts.
- [Inventory](inventory.md) — the Signing, the Erasure (class-pickers
  can switch between classes; cannot return to unburdened).
- Existing memory: penitent vertical slice planned for next dev
  session.
