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
schema of four base stats: **STR**, **DEX**, **END**, **LCK**. This
is the universal substrate. **All actors start at 1/1/1/1** as the
baseline mortal floor.

For the class-picker, growth from this floor is the cosmological
event of substance accumulating in the substrate (Hell installing,
via the Crucible's fire). For the Unburdened, **the floor is the
ceiling.** Stat numbers stay at 1/1/1/1 for the entire run; the
unburdened path has no metabolic-fire (no imprint) to drive
installation.
Unburdened progression is non-stat (riversamento → ability /
passive unlocks; see *Evolutions*, below).

Internal names match the engine's Souls-derived quad for now;
Dante-coded renames (Forza / Destrezza /
Costanza / Fortuna or thematic equivalents) land when the canon-voice
pass on UI is done.

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
