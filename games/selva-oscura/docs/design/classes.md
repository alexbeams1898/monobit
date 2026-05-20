# Classes

> **Owns:** the player's class system — penitent / wretched / heretic,
> their evolutions, stat profiles, what each *feels* like to play.
> **Status:** structural locks; per-class mechanical detail TBD.

## Cosmological constraints (locked from setting.md / story.md)

The class system inherits structural rules from the cosmology. These
are not class-design choices; they are inputs class-design must
satisfy.

- **The Seal opens the imprint.** Using the Seal is Hell's first
  formal measurement. From that moment, the class-picker has an
  imprint Hell can grip. Contrapasso can land on him. The imprint-
  free state is lost.
- **Class evolution combines stat investment + contrapasso accretion.**
  Stat investment (sangue at OFFERINGS) is the *active* path the
  player chooses. Contrapasso accretion is the *passive* path the
  cosmology imposes — leaked contrapasso from felled keepers lands on
  the class-picker (per setting.md *Per-circle reactivity*). Both
  contribute to evolution.
- **Class-evolution is Hell loading itself into the Vagrant.** Each
  keeper kill releases contrapasso; some accretes in the class-picker
  Vagrant; by the time he reaches Lucifer, he is contrapasso-saturated
  and shaped to fit the throne. The TRANSFIGURATION arc is the entire
  game — class evolution is grooming-for-Satan-2 expressed
  mechanically.
- **The unburdened never receives contrapasso.** Substance flows
  through (he remains imprint-free). The unburdened evolution is
  *subtractive* — Unburdened → Svuotato → Diaphanous, the cumulative
  state of substance refusing to settle. See setting.md *The Vagrant /
  The unburdened path (PURITY)* for the locked evolution stages.
- **Three stats only — HP, fire_rate, damage.** Engine-level
  constraint per `.claude/CLAUDE.md`. Per-class differentiation must
  be expressed within this triad.
- **Entity symmetry is sacred.** Player, enemies, bosses, bullets are
  the same `Entity` struct. *Class* is data, not type. Per-class
  combat profile must work as a stat / sprite / AI-tag combination,
  not as call-site special-casing.

## The three classes

*Names locked: **Penitent**, **Heretic**, **Wretched**.* Per-class
fantasy, mechanical identity, and combat profile TBD. Constraints:

- Each class must differentiate within HP / fire_rate / damage. No
  fourth stat. No class-specific mechanic that isn't expressible in
  the triad.
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

- L1 = base form, immediately post-Seal.
- L2 = mid-game, partial imprint completion. Stat investment + some
  contrapasso accretion.
- L3 = late-game, full imprint. The halo arrives at L3 for Penitent /
  Heretic. Wretched at L3 has no halo (his punishment is
  non-completion).

**Unburdened evolution stages: L1 → L2 → L3.** Locked at setting.md.
- L1 = Unburdened (base).
- L2 = Svuotato (the emptied) — has poured out a meaningful amount of
  sangue.
- L3 = Diaphanous (the translucent) — has poured most of what could
  be collected. The prerequisite for PURITY.

Visual progression: class-pickers gain mass / imprint detail;
unburdened loses mass (subtractive). Per-stage sprite specifics TBD.

## Stat profiles

*TBD — must be reconciled with the engine-level "three stats only"
constraint (HP, fire_rate, damage). Per-class starting stats and
per-evolution growth rates emerge during gameplay tuning.*

The cosmological constraint above (contrapasso accretion contributes
to evolution) means stat growth is driven by *both* sangue
investment (player choice at OFFERINGS) *and* keeper-kills (passive,
imposed). The interaction of these two sources is open — possibly
keeper-kills unlock evolution *thresholds*, with sangue investment
determining where in the triad growth lands.

## Open questions

- Per-class fantasy — what does each class *feel* like?
- Per-class combat differentiation within HP / fire_rate / damage.
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
- [Character creation](character-creation.md) — Seal-use mechanism,
  class pick at the beasts.
- [Inventory](inventory.md) — Seal, Erasure (class-pickers can
  switch between classes; cannot return to unburdened).
- Existing memory: penitent vertical slice planned for next dev
  session.
