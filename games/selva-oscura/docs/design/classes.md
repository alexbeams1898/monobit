# Classes

> **Owns:** the player's class system — penitent / heretic / ferine /
> unburdened, their evolutions, stat profiles, what each *feels* like
> to play.
> **Status:** structural locks; per-class mechanical detail TBD.
>
> **Naming history:** the third class was originally "Wretched"
> (retired 2026-06-11) → "Feral" (working name through
> 2026-06-14) → "**Ferine**" (locked). Older doc passages may still
> say "Feral" / "Wretched" until the rename sweep lands; the
> mechanical / cosmological content carries forward unchanged.

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

### Class is a bias of growth, not a gate (LOCKED 2026-06-12)

For class-pickers (Penitent / Heretic / Feral), **class does not
hard-lock which stats the player can install.** Every body stat and
every mind stat is installable on every class-picker path. What
class does is **bias the install cost** — installing a stat that
aligns with the class's cosmological identity is cheaper in sangue
per point; installing a stat orthogonal to the class's identity
costs more.

This is the Dantean reading. Free will is preserved: the path the
Vagrant signed at Beat 4 shapes — but does not determine — what he
can become. The cosmology punishes orthogonal growth with friction,
not with refusal. A Feral player who *really* wants high INT can
install it, slowly, expensively. He will lag behind a Heretic doing
the same thing, late game, but he is not denied the road.

Working bias shape (specific multipliers TBD at tuning):

| Class | STR | DEX | END | LCK | INT (cog) | acquired-stat |
|---|---|---|---|---|---|---|
| Penitent | cheap | medium | cheap | medium-high | medium | cheap (built-in) |
| Heretic | medium-high | cheap | medium | cheap | cheap-medium | cheap |
| Feral | cheap | medium | cheap | high | high | cheap |

The Unburdened is the structural exception — not biased, blocked.
He cannot install body stats at all (1/1/1/1 lock). This is not a
bias choice; the cosmology of the refused Signing means no
imprint-fire exists to install through. Body progression is
substituted by riversamento-gated non-stat capability (see
*Evolutions*).

Mind stats (Perception / Cognition / Intelligence) bias the same
way for class-pickers but with smaller multipliers — they grow
from engagement, so substance cost is partial, not full.

### Non-melee combat verbs: incantations vs invocations (LOCKED 2026-06-12)

Selva has two cosmologically distinct registers of non-melee
combat verb. They are **peer item categories** in the data layer
(engine `ItemCategory::Incantation` + `Invocation`); there is no
umbrella term for "both" — the cosmology resists umbrella-ing them
because they operate on structurally different mechanisms.

| Register | Cosmological source | Mechanism |
|---|---|---|
| **Incantation** | The Vagrant's own installed substance | Vagrant exerts; substance leaves his substrate as the verb |
| **Invocation** | Beatrice's reservoir, reached through the cosmological link | Beatrice acts *through* the Vagrant; he is the conduit |

**Both registers administer second death.** Per setting.md
*Forced repentance through second death*, granting second death
to a shade trapped in stagnant contrapasso is the cosmologically
correct act in a broken Hell. Whether the Vagrant grants it
through his own crushed-weight, his heretical word, his feral
howl, or Beatrice's grace channeled through him — the act IS
the cosmologically required work. **Neither register is
"evil magic"; both are how the broken protocol gets administered**
by the only being who can do it. Catholic *maleficium* (unjust
harm via cosmological force) does NOT apply, because the harm is
just within the cosmology of forced repentance.

**Class bias on incantation / invocation usage:**

Class is a bias, not a gate, here too. Any class-picker can use
either category; the scaling differs.

- **Penitent / Heretic / Feral incantations**: incantation items
  scale on stats the burdened class can install. Class-tilted
  incantations (a Penitent-coded hammer-of-weight; a Heretic-
  coded forbidden word; a Feral-coded paralyzing howl) scale
  best for their aligned class but are usable by any burdened
  class, just less effective.
- **Invocations** are structurally the Unburdened verb. A
  burdened class CAN pick up an invocation item and try to
  trigger it; they have no riversamento volume, so the channel
  cannot open — the invocation scales to nearly nothing.
  Effectively (not by hard rule), invocations are an
  Unburdened-only register.
- Unburdened CAN pick up incantations but has no installed
  substance to release — same scale-to-nearly-nothing problem
  in the opposite direction.

**The two-categories split is intentional asymmetry.** Burdened
classes share their register and differentiate via tilt; the
Unburdened-burdened wall is the genuine cosmological boundary,
not a class-vs-class boundary.

**The full incant + invocation item systems are designed in
follow-up docs when those systems ship.** This section locks the
category structure for the items-foundation branch; the
mechanical specifics (per-incant stats, scaling formulas,
animation hooks, riversamento gates per invocation) are TBD.

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
Not part of `Stats`. The class-picker receives a starter **ability
kit** at the Signing; subsequent abilities unlock at L2 / L3
evolution. The Unburdened has **no kit at L1**; abilities arrive
only via riversamento-gated unlocks at Svuotato (L2) and Diaphanous
(L3) — specifics described in *The Unburdened does NOT use the
unlock-mask for stats*, below.

**The starter ability kit at the Signing is NOT a starter weapon.**
The Vagrant's first weapon is a world-placed object found on the
descent stairs (per inventory.md *The starting weapon is in a world
container* and crafting.md *World-placed weapons*). The Signing
unlocks the commit-verb capacity (Crucible for class-pickers,
Censer for Unburdened); it does not gift a weapon. Class-pickers
and Unburdened reach the descent-stair weapon pre-Signing or post-
Signing alike — the weapon is a world event, not a menu event.

**Class passives may include weapon-scaling buffs.** The Feral's
passive (per [[project_class_acquired_stat_doctrine]]) buffs unarmed
damage scaling substantially — his body IS his weapon. Mechanically
this is class-specific scaling on the shared `unarmed` `ItemInstance`
every character carries (per inventory.md *Unarmed is a weapon*),
not a separate "Feral fists" item. The data shape is one unarmed
weapon; class differentiation is in the scaling multipliers and the
Feral's acquired-stat tier-threshold gates on unarmed evolution
branches (per weapon-evolution.md *Unarmed evolution*).

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

## Per-class identity stats: locked tier-promotion (LOCKED 2026-06-14)

Replaces the working-name placeholders (Piety / Cunning / Feral)
from project_class_acquired_stat_doctrine. Every class -- including
Unburdened -- has ONE identity stat visible on the HUD that
tier-promotes through play via the language map, same machinery as
class names tier-promote.

The promotion arc is **felt → named → cosmological**: the Vagrant
feels a thing at tier-0 (familiar word, no cosmological vocabulary),
recognizes what he's doing at tier-1 (named position), and
understands the cosmological act at tier-2 (the anchor reveal). The
mechanical scaling never changes -- only the language wrapping it,
exactly like class names.

| Class | Tier-0 (felt) | Tier-1 (named) | Tier-2 (cosmological) |
|---|---|---|---|
| Penitent | **Vitality** | **Burden** | **Penance** |
| Heretic | **Doubt** | **Unorthodoxy** | **Anathema** |
| Ferine | **Appetite** | **Voracity** | **Predation** |
| Unburdened | **Resistance** | **Hollowing** | **Diaphany** |

Each arc is the class's whole cosmology compressed into three
words. The Vagrant who picked Penitent feels his Vitality grow,
later recognizes he's bearing Burden, and finally understands he's
performing Penance. The Heretic feels Doubt, recognizes he's
landed in Unorthodoxy, learns he is Anathema (the formal cosmological
cursed-position). The Ferine feels Appetite, recognizes Voracity,
understands Predation. The Unburdened feels Resistance, recognizes
Hollowing, sees Diaphany (the substance has been passing through
him; he IS the through-passage; his L3 form Diaphanous matches the
stat name at the same anchor).

**Unburdened gets a HUD-visible stat (overrides the older
HUD-hidden doctrine from project_class_acquired_stat_doctrine).**
The Unburdened path is no longer the refuses-to-be-labeled
exception at the stat layer. The cosmological refusal lives in the
mechanics (no body-stat installation, riversamento volume drives
form evolution) and in the L1→L2→L3 form names (Unburdened →
Svuotato → Diaphanous). The stat is visible because all four
classes deserve to see their own progression.

**Tier-2 cosmological-anchor reveals fire through the same insight
system that promotes class names**: a specific load-bearing event
unlocks the tier-2 name (specifics per-class TBD at content
authoring -- e.g. Diaphany may anchor on the same insight that
fires Diaphanous L3 form).

**Tier-0 / tier-1 / tier-2 mind summary prose per stat is TBD.**
The locks here are the names + the tier shape; the in-fiction
prose at the Mind sub-page summary for each tier lands at content
authoring time.

## Soft-cap stat curves: per-class scaling shape (LOCKED 2026-06-14)

Replaces the install-cost bias doctrine from
project_class_bias_and_magic_split. The new rule:

**Install cost is universal.** Every class pays the same sangue
per stat point at the Crucible. What differs per class is the
**diminishing-returns curve shape** -- the Elden Ring soft-cap
model.

For each body stat, the per-stat return-on-point follows a curve
that BENDS at a soft cap. Below the cap, each point gives full
return; above the cap, each point gives diminishing return. **The
position of the bend is class-specific.** A class that is "good at"
a stat has its bend LATE (high soft cap, big returns deep into
investment). A class that is "bad at" a stat has its bend EARLY
(low soft cap, returns drop off quickly).

The player can still install any stat to any value on any class.
What changes is what their investment BUYS past the cap.

Per-class body stat shape (LCK exempt from class bias; universal
curve for all classes):

| Class | High soft cap (good at) | Low soft cap (bad at) |
|---|---|---|
| Penitent | END | DEX |
| Heretic | DEX | END |
| Ferine | STR | DEX |
| Unburdened | — (all body stats locked at 1/1/1/1) | — |

The triangle (Penitent END/DEX, Heretic DEX/END, Ferine STR/DEX)
makes each class's curve shape distinct from the other two:
Penitent and Heretic are inverses on END/DEX, Ferine breaks
sideways with STR.

**The Penitent's identity stat (Penance) and the body-stat END
soft-cap shape co-vary** -- a Penitent investing in END buys the
high-cap curve AND can apply it through Penance-scaled weapons.
This is the soft-cap doctrine doing double duty: signaling class
identity at the stat-investment moment AND driving weapon scaling
later.

**Soft-cap curve numbers are tuning values, not doctrine.**
Specific bend positions ("good-at bend = stat 40, bad-at bend =
stat 15," or whatever shape feels right at tuning) live in
config/balance/formulas.json. The doctrine here locks the SHAPE
(per-class per-stat curves with bend-position bias). Numbers are
revisable without doctrine change.

## Unburdened: amended Mind doctrine (LOCKED 2026-06-14)

Amends [[project_cognition_system_v1]] from "Mind grows uniformly
across classes" to "Mind grows faster for the Unburdened than for
the burdened classes." The cognition system's behavior-driven
growth (Perception from observation, Cognition from inference,
Intelligence from warranted inference) is unchanged; only the
multiplier differs.

Cosmologically: the Unburdened refuses body-stat installation, so
the substance that would have densified his substrate instead
clears his head. The path's locked 1/1/1/1 body floor is paid for
by a Mind ceiling that climbs faster than the burdened classes can
reach. He is the "glass cannon utilizing heavy Mind" archetype --
fragile body, dominant cognition.

This restores symmetry to the four classes: each path has SOMEWHERE
it grows faster than the others. Penitent / Heretic / Ferine each
have a body stat with a high soft cap (END / DEX / STR
respectively). Unburdened has all three Mind stats with a higher
growth multiplier. **No class is universally weaker; the asymmetries
balance.**

The Mind growth multiplier is a single number in
config/balance/formulas.json (`mind_growth_multiplier_unburdened`,
placeholder; tune at balance pass). Burdened classes use 1.0;
Unburdened uses some value > 1.0 (tuning).

## Class-shaped descent-stair starter weapons (LOCKED 2026-06-14)

Replaces the inventory.md *The starting weapon is in a world
container* shared-weapon framing. New rule:

**The descent stairs spawn a class-shaped tier-0 starter weapon.**
Each class (Penitent / Heretic / Ferine / Unburdened) sees a
different humble weapon at the descent, matching their class's
preferred stat profile. Visually the moment is a Dark Souls 1
Asylum-stair ode: glow visible at distance, walk down, pick up.

**The starter weapon is mundane, not class-named.** Per the
explicit content rule: no "Penitent's Sword of Justice" naming at
this tier. Just a humble weapon -- e.g. a plain sword, knife,
bone-knuckle, vessel-incant -- whose scaling letter-grades happen
to match the aligned class's stat curves.

**Other classes can use it.** No hard class-lock. A Ferine who
finds a Penitent-shaped sword can wield it; the soft-cap doctrine
just means it scales worse for him (he's bad at END; the sword's
END scaling is wasted past his low soft cap). Pokemon-starter
analogy: yours from minute one but tradeable/swappable.

**The starter is the same item in the universal weapon tree.**
Lives in the same evolution tree all weapons live in. A Ferine
wanting the Penitent's starter can find it deeper in the tree
through normal crafting/evolution paths. The descent gift is just
"your class's starter is given to you here" -- not a class-locked
unique branch.

**Class-signature unique weapons** (deeper-tree class-specific
weapons with cosmologically loaded names and lore) are a separate
concern handled at content authoring. The descent starter is the
humble layer; signatures are the deep-cut layer.

**Pre-Beat-4 doctrine unchanged.** The Vagrant arrives empty-
handed in Beat 1, crosses Acheron and descends in Beats 2-3 with
fists only, signs (or refuses) at Beat 4. The starter weapon
appears at the descent stairs AFTER the Signing -- the cosmology
delivers what soul-shape the Vagrant just signed for.

## Identity stats are derived from action counters (LOCKED 2026-06-14)

Resolves the "specific behavioral lists per class" and "exact
derived-stat formulas per class" open questions from the prior
acquired-stats doctrine. Identity stats are **not stored as
separate fields**. They are **pure functions evaluated at read
time** from the PlayerProfile's permanent action-counter ledger.

### The mental model

The world keeps a single record of what the Vagrant has DONE: kills
tallied, examines counted, dialog choices flagged, sangue committed,
hits tanked, refusals refused. This action ledger lives on
`PlayerProfile` and grows monotonically across the run.

**Each identity stat is a lens onto that ledger from one class's
cosmological angle.** Penance reads the ledger and asks "how much
have you tanked / endured / submitted?" Anathema asks "how much
have you refused / defied?" Predation asks "how much have you
consumed / feasted / preyed?" Diaphany asks "how much substance has
flowed through you?"

The ledger is permanent. The lens is the active class.

### Implementation contract

- **No identity-stat fields stored on `PlayerProfile`.** The
  Identity stat is computed on demand. HUD reads
  `computeIdentityStat(profile, profile.player_class)` each frame
  (or caches with profile-change invalidation -- engine call).
- **Per-class functions live in
  `config/balance/formulas.json`.** Each class declares which
  ledger counters feed its identity-stat formula + how. Tunable;
  no code edit per balance pass.
- **Counters added to `PlayerProfile` incrementally.** Today the
  ledger has `kill_counts`, `examine_counts`, `flags`,
  `sangue_lifetime`, `sangue_riversato`, `felled_bosses`,
  `unlocked_insights`, `npc_state`. New behavioral counters
  (hits-tanked, refusals-uttered, corpses-feasted-on, etc.) land
  alongside the gameplay features that generate them. **No need to
  author all counters today** -- the foundation is the rule
  (identity = function of ledger), the content is the per-counter
  growth as systems ship.

### Why this shape

- **Identity is what you DID, body is what you BOUGHT.** Sangue
  commits buy body stats (Crucible verb); only actions earn identity
  stats. The two progression vectors are mechanically separate.
- **No conversion math at Erasure.** When the player switches class
  the HUD just swaps which function it evaluates. The ledger is
  unchanged. The Vagrant's history is permanent; only the lens
  through which Hell measures it changes. See *Erasure mechanics*
  below.
- **Cosmologically honest.** A Penitent who occasionally refused
  things has been doing heretical acts all along; Hell's accounting
  saw them; the cosmology recognizes them when (and only when) the
  Vagrant becomes the kind of soul those acts now count for.
- **Cross-class acquisition emerges automatically.** A Penitent's
  Anathema field is computable at any time -- it's just a function
  call. The number stays at whatever heretical-acts-the-player-has
  -done evaluates to. Late-game mechanics that let a Penitent
  unlock the Heretic lens (special quests, NPC reveals) need no
  new storage layer; the data is already there.

### Authoring rule for new behavioral counters

When a new gameplay action is added that should feed an identity
stat:

1. Add the counter to `PlayerProfile` (plain int / map / set, save
   round-tripped).
2. Increment it at the gameplay event hook (same pattern as
   `kill_counts` today).
3. Reference it in one or more per-class formulas in
   `formulas.json`.

That's the whole machinery. No new classes, no special-case logic,
no per-class growth events.

## Erasure mechanics (LOCKED 2026-06-14)

Closes the "Persistence across class changes" silence. Per-direction
rules:

### Cross-burdened Erasure (Penitent ↔ Heretic ↔ Ferine)

Functions as a **respec**, not a start-over.

- **Body stats refund** as sangue to the vessel for re-spending
  under the new class. Preserves existing canon (setting.md /
  economy.md "refunds prior commitments to the new vessel").
- **Identity stats re-evaluate automatically** under the new
  class's lens. No conversion event; the ledger is unchanged. A
  player who Erases from Penitent (Penance N) to Heretic gets
  whatever Anathema their action ledger evaluates to under the
  Heretic formula. A "pure Penitent" who never did anything
  heretical starts Anathema low; a "compromised Penitent" who has
  been making heretic-aligned choices starts higher.
- **Inventory + equipment carry forward.** Items don't unequip on
  Erasure. Equipment that no longer meets stat requirements (the
  player just refunded their body stats) goes into a "below-req"
  state per the existing stat-gated-equip doctrine -- still
  equipped, with damage penalty per soft-cap -- until the player
  re-installs the body stats their gear demands.
- **Materials gate the ritual** per existing canon (Guide-performed,
  materials-gated). Cross-burdened costs lower than crossing the
  burdened-Unburdened wall (per balance pass).

The fun-vs-realism trade lands here. A Penitent who has never
refused anything Erasing to Heretic starts Anathema near 0 -- by
strict cosmology that's correct (they did nothing heretical). The
respec feel is preserved because **body-stat refund + fast Heretic
re-investment is the lever**, not identity stat carry-over. Identity
stays slow-and-earned; body is the fast respec.

### Burdened → Unburdened Erasure

**Jarring; cosmologically real.** The Vagrant is undoing the
imprint, not swapping fighting styles.

- **Body stats wipe to 1/1/1/1** (the Unburdened lock per existing
  doctrine). All previously-installed sangue refunds to the vessel.
- **The refunded sangue must riversa** -- the Vagrant has to pour
  it back out. The Unburdened doesn't install; the refunded
  substance is too much to hold. Until riversamento brings it back
  to 0, the vessel sits full.
- **Identity stats re-evaluate** under Diaphany's lens. The
  Vagrant's previous action history mostly reads "you committed
  substance you weren't supposed to commit" through the Diaphany
  formula -- the Diaphany value starts low. Diaphany grows from
  here through riversamento (per locked canon).
- **Inventory + equipment unchanged** other than the body-stat-
  gating consequence noted above. Most worn gear becomes unequip-
  worthy because the Unburdened can't meet its requirements.

### Unburdened → Burdened Erasure

**Forbidden** per existing canon (classes.md:139-141 +
[[project_commit_verb_unified_2026_06_11]]). The Guide "cannot
perform the Signing on a refusing unburdened." Once refused, the
imprint cannot be re-opened. The Erasure ritual offers no path back
to a class-picker identity for an Unburdened-committed Vagrant.

This is locked and intentional. The Unburdened is the only
*irreversible* class commitment; the burdened classes are
mutually-fungible via Erasure but the Unburdened wall is one-way.

### Visual feedback at Erasure

The class-picker UI fires again (per existing canon). The displayed
identity stats for each option pre-evaluate under that option's
lens so the player sees what their numbers would be BEFORE
committing. ("If you Erase to Heretic, your Anathema would start
at 12; to Ferine, Predation at 4.") No surprise; full preview of
each option's mechanical landing point.

## Ferine mouth-slot doctrine (LOCKED 2026-06-14, IMPLEMENTATION DEFERRED)

The Ferine path's becoming-beast cosmology expresses mechanically at
the equipment layer, not just the visual layer. Specifically: the
Ferine gains a **mouth equipment slot** as a class-specific HUD
addition, gated on form evolution.

### The slot and what goes in it

- **Mouth slot** exists ONLY for Ferine. The equipment screen
  renders an extra slot row when `player_class == Ferine`; other
  classes do not see it.
- Items that go in the mouth slot are **fangs, teeth, jaw
  augments** — body-weapon shapes appropriate to "the soul has
  grown into something that bites." Authored as a new `MouthSlot`
  marker on selva-side ItemExtensions (engine ItemDef stays
  generic; mouth-slot is selva cosmology).

### Stance mutex (the combat doctrine)

- **Mouth and arms are mutually exclusive in combat.** A Ferine
  with an arm weapon equipped CANNOT use the mouth slot. A Ferine
  with a mouth item active CANNOT use arm weapons.
- The combat input rebinds per stance: arms-active routes attack
  input to the normal weapon-swing chain; mouth-active routes
  attack input to a per-mouth-item attack hook (bite / gnash /
  lunge — TBD per item).
- A stance toggle (button or context) flips between the two.

### Progression-gated unlock

- The mouth slot is **NOT available at Ferine L1**. The L1 Ferine
  is still mostly humanoid; their starting weapon (`fang.json`)
  sits in the arm slot, used like any other hand weapon — a
  grip-weapon shaped like teeth, not yet a mouth verb.
- The mouth slot **unlocks at Ferine L2 evolution** (per the locked
  cosmology: form shifts toward feral, body begins growing the
  jaw-augment capacity).
- At L3, the mouth slot is dominant — the body IS the mouth, arm
  weapons feel vestigial (mechanical statement: arm scaling falls
  off further, mouth scaling peaks).

### Why this composes with locked doctrine

- **Becoming-beast made mechanical**: the cosmology of "the
  Ferine's commit rewrites the substrate" lands as a literal
  equipment-layer change. The player FEELS the evolution by seeing
  a new slot appear on their HUD.
- **Class signaling through HUD presence**: choosing Ferine
  literally adds a slot to the equipment screen at L2. No other
  class gets it; the HUD itself communicates class identity.
- **Predation stat as gating + scaling**: mouth items scale on
  STR + Predation (the body-axis the Ferine already has). The L2
  Predation threshold is the same threshold that unlocks the slot.
- **Composing with unarmed-as-weapon**: the existing
  `unarmed` item per [[project_items_loot_doctrine_locked]] is the
  default arm-slot fallback. The mouth slot has its own default
  ("teeth at L2") so the Ferine is never disarmed in BOTH slots
  simultaneously.

### Implementation deferred

Building the mouth-slot equipment-screen rendering + arm-vs-mouth
combat dispatcher + stance-toggle input is a focused next slice.
For v1 (descent-stair starter weapons + world-pickup spawn) the
Ferine starter (`fang.json`) ships as an arm-slot weapon -- humble
"a fang shaped to be gripped in the hand," played like any other
weapon. The mouth-slot verb arrives with the L2 system.

This doctrine is locked so the data shape we author today doesn't
contradict it. Specifically: the Ferine starter is named "Fang"
(not "Fang Caestus" / "Beast Knuckle") so when L2 mouth verb lands
the same item could move to the mouth slot via evolution without
a renaming churn.

## Open questions

- Per-class fantasy — what does each class *feel* like?
- Per-class combat differentiation within STR / DEX / END / LCK
  scaling (before considering class-specific stats).
- Which class-L2 unlocks introduce new base fields, and which stay
  on the universal four? (Heretic = Faith locked above. Penitent
  and Ferine TBD.)
- Whether the unlock-mask is per-stat booleans or a single
  `class_evolution_level: int`. (Working answer: per-stat boolean
  — more granular, simpler engine code.)
- Whether derived-stat values are recomputed per frame, cached on
  stat-change, or only on level-up. (Working answer: cache on
  stat-change; cheap and predictable.)
- Stage-name for Ferine at L3.
- How contrapasso accretion expresses mechanically — does it auto-
  invest into specific stats? Modify class-specific behaviors? Unlock
  evolution gates? *Decision deferred to gameplay tuning.*
- Whether SURFEIT (all-stats-maxed) is reachable on all three
  classes equally, or whether one class hits the cap most easily.
- Per-counter growth formulas per class (which counters feed which
  identity stat at what weight) -- locked as a rule (formulas.json
  config) but content lives in the per-system gameplay authoring as
  features ship.

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
