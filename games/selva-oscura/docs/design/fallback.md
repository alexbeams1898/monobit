# Fallback

> **Owns:** what happens when things go wrong from the player's
> perspective — death, retry, save semantics, vestigia (the trace
> left behind by previous runs).
> **Status:** structural locks; per-screen UX detail TBD.

## Death

Locked at setting.md *Second death*. Summary:

- **Mechanics on Vagrant-killed:** uncommitted held sangue returns
  to Hell
  (unrecoverable); stats / lifetime sangue / riversato persist.
  Cycle structure (setting.md *Cycle structure*) governs what
  resets vs. what persists.
- **Cutscene:** Vagrant suffers visibly, sangue exits the body. Hell's
  killing-protocol fires; the body dies but the soul, having no
  imprint, is ejected to the Wood (per setting.md *The unjudged*).
- **Death-card overlay** at the cutscene's peak, *YOU DIED*-coded:
  ```
          NOT YET
  THOU DOST NOT BELONG
  ```
  *NOT YET* (top, large, modern register) — refusing finality.
  *THOU DOST NOT BELONG* (bottom, smaller, Early Modern register) —
  echoing Charon's *non sei di qua* (Inf. III.88).
- **Run-stats screen follows** with no header text — full pixel budget
  for data (sangue lost, kills, keepers felled this run, etc.). Term
  *sangue* enters vocabulary in this screen, not on the death-card.
- **RIVERSAMENTO's terminus is the only run-end without the death-card.**
  The cosmic firing replaces it. The absence is the player's
  confirmation that something fundamentally different has occurred.

## Retry

- **Same Vagrant.** No alternate-character on death; the run is the
  current Vagrant's continuing struggle. Per setting.md *The
  unjudged*, Hell cannot finish a soul it never measured — the cycle
  restarts because the protocol's completion does not land.
- **Respawn location: the selva oscura (the Wood).** Setting.md
  *The selva oscura as cosmological destination* — the Wood
  naturally receives unjudged souls. Beatrice does not pull him
  back; the cosmology does. The Vagrant wakes in the Wood after
  every death.
- **Class persists across cycles.** Class choice and evolution stage
  carry forward. Stat investments persist. Riversato lifetime total
  persists. Keepers felled persist (a felled keeper does not
  respawn). NPC encounter history and Grimoire unlocks persist.
- **Uncommitted vessel contents do not persist.** Lost on death
  (returned to Hell). The only way to obtain more is to extract from
  new shades and keepers.

## Save semantics

Save = **vestigium** (singular) / **vestigia** (plural). The save
register is the cosmological record-imprint of the Vagrant's current
state — not a physical site, just the trace of run state. UI verbs:
*Inscribe* (save) / *Restore* (load). Save/load language is in the
world's register, not labeled "save" / "load."

Symmetric across paths — both class-picker and unburdened use the same
save register and the same triggers.

- **Autosave triggers:** every commit (installation for class-picker
  or riversamento for unburdened), every item commit, voluntary
  retreat (via markers), second death,
  and other run termination.
- **Manual save (Inscribe):** Wood-side. Specifics deferred to UX.
- **Restore:** loads from autosave. Used primarily by players who
  make a Wood-side mistake (e.g. installing the wrong stat via
  commit).
- **Persistence across power loss:** vestigia must be durable,
  written to file atomically.

## Vestigia (player-experience role)

- **Primary use:** restore from a Wood-side mistake. Death does not
  cost beyond the run's uncommitted vessel contents — vestigia are
  *not* bloodstain-style recovery for in-Hell mistakes.
- **Run-end is not a vestigium-loss event.** Persistence carries
  through second death; only uncommitted vessel contents are lost.
- **Each character / run occupies a vestigium slot.** Multiple
  vestigia let the player run multiple Vagrants in parallel
  (different paths, different classes, different choices).

## Cycle structure

Per setting.md *Cycle structure*. Structural lock; narrative-
experience pacing of cycles is open (story.md TBD item 1).

**Persistent across cycles:** lifetime sangue total, stats installed
via commit, riversato lifetime total, class choice, keepers felled,
NPC encounter history, Grimoire unlocks, the active commit outcome
(installation or riversamento per path).

**Reset each cycle:** uncommitted vessel contents, current HP and
position, transient combat state.

## Open questions

- **Manual save (Inscribe) UX flow** — when can the player save?
  Wood-only or anywhere? Per-cycle limits?
- **Vestigium count / slots** — how many parallel runs can a player
  maintain? Constrained by EEPROM capacity on Arduboy.
- **What does Restore look like on the death cutscene** — does the
  player choose Restore *before* the death-card, *after* the
  run-stats screen, or both?
- **Final wording for *Inscribe* / *Restore*** — currently working
  candidates. Locked as concepts; specific verbs TBD.

## Cross-references

- [Setting](setting.md) — *The unjudged*, *Cycle structure*, *Second
  death*, *Save and inventory / Vestigia*.
- [Story](story.md) — death and retry are narrative events; the
  run-end texture (NOT YET / THOU DOST NOT BELONG, run-stats
  screen) is part of the reveal architecture.
- [Classes](classes.md) — class persists across cycles; stat
  investments persist.
- [UX](ux.md) — screen flow for death-cutscene → death-card →
  run-stats → respawn-in-Wood.
