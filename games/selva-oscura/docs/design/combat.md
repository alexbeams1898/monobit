# Combat

> **Owns:** the player's combat experience — what hitting attack does, what
> tools the player has in their hands, how stance and grip interact with
> bindings.
> **Status:** drafting (melee/physical only); incants and natural abilities
> deliberately out of scope until they get their own treatment.

## Scope

This doc defines combat **for melee/physical builds only**. Any combat
that flows through "natural abilities," incants, casting, or ranged
attacks is **explicitly out of scope** in this version of the doc. When
those builds are designed, they get added here as their own sections OR
spun into a sibling file (e.g. `combat-incants.md`).

The choice is deliberate: locking down the melee shape first lets
non-melee combat be designed *against* a working physical baseline,
rather than designed alongside something still in flux.

## Foundation

Combat is **Souls-feel** — committed actions, weighty timing, stamina-
gated decisions, lock-on, equipment-driven identity. Every attack costs
something; nothing is spammable; whiffing has a recovery cost.

That foundation is the reference, not a literal copy. We expect to
creatively interpret pieces of it as iteration progresses. What is
**not** open for reinterpretation is the underlying physics-of-feel:
heavy actions, no spam, stamina pressure, deliberate spacing.

## On-hand / off-hand model

The two-button melee grammar is **semantic**, not anatomical.

- **LMB** = on-hand action
- **RMB** = off-hand action

What an action *is* depends on what's equipped in that hand:

- Weapon → swing or thrust
- Shield → block (held) or bash (tapped)
- Empty hand → punch / grapple
- (Future, out-of-scope here: focus / wand / catalyst → cast)

The button does the on-hand thing. The content of "the on-hand thing"
is a property of equipment, not of the binding.

## Stance

Stance determines **which physical hand is the on-hand**.

- **Right-foot-back** → right hand is the on-hand. LMB swings the right-
  hand item; RMB swings the left-hand item.
- **Left-foot-back** → left hand is the on-hand. LMB swings the left-
  hand item; RMB swings the right-hand item.

Stance is **changeable on the fly** during combat via a dedicated input
(see Bindings below). Switching stance is a quick body pivot; not free,
but fast — players can re-orient mid-fight to lead with whichever item
is more useful for the moment.

The intent: the player isn't hard-locked into a "sword build" or "shield
build." They equip two things, choose stance, and lead with whichever
hand the situation wants.

## Bindings

Default bindings as of v0. **These will change based on play feel.**
The point is to wire them up, play, and revise — not to lock them in
on paper.

| Key                   | Action                                           |
|-----------------------|--------------------------------------------------|
| WASD                  | Move                                             |
| Mouse                 | Camera / aim                                     |
| LMB                   | On-hand action (light)                           |
| RMB                   | Off-hand action (light)                          |
| Shift + LMB           | Heavy variant, on-hand                           |
| Shift + RMB           | Heavy variant, off-hand                          |
| Hold RMB              | Block (if off-hand has a shield)                 |
| Space (tap)           | Dodge / backstep                                 |
| Space (hold)          | Sprint                                           |
| Shift + Space         | Jump                                             |
| F                     | Flip stance                                      |
| Y / G                 | Grip toggle (one-handed ↔ two-handed)            |

### Notes on the binding choices

- **Shift + Space for jump** is chosen for ergonomics: pinky on Shift
  + thumb on Space + index/middle on WASD is the most comfortable
  multi-key chord on the keyboard. The known UX trade-off is that
  pressing Space while Shift is queued for a heavy attack will trigger
  jump instead — accepted as a minor friction point that's easy to
  re-evaluate after play.
- **F for stance flip** chosen because index reach from D is fast and
  doesn't conflict with combat keys. ER's F = jump; we override.
- **Grip toggle (Y/G)** is distinct from stance. Grip = how the weapon
  is held (one-handed vs two-handed); stance = which hand leads.
  Both axes coexist; players can be in left-foot-back with a two-
  handed weapon, or right-foot-back with two one-handed weapons.

## What's not yet designed

These are deliberately deferred until iteration teaches us what's needed:

- **Stamina specifics** — pool size, regen rate, per-action costs.
  Souls-feel implies it exists; numbers TBD.
- **Lock-on** — which key, what targeting rules, how it interacts with
  stance flip and dodge direction.
- **Heavy-charge timing** — does Shift+LMB hold-and-release charge a
  bigger attack? Or is it just a discrete heavy?
- **Combos / chains** — does light-light-light produce a combo
  sequence? Or is each press a discrete commitment with full recovery?
- **Backstep vs roll** — Space-tap-with-no-WASD is currently backstep;
  Space-tap-with-WASD is directional roll. Confirmed working; may need
  refinement once stance is in.
- **Two-hand grip behavior** — Y/G toggles grip but the visual /
  mechanical effect on the off-hand item is TBD (drop? stash? hold-but-
  unused?).
- **Movement-locking during attacks** — partly implemented via the
  one-shot body-mask system; needs revisiting once stance flips can
  happen mid-combat.
- **Animation transitions** — stance flip needs an authored or
  synthesized pivot; jump-while-rolling, jump-while-attacking, etc.
  all have edge cases that emerge from play.
- **Unarmed** — stays in the doc as a real combat path (LMB/RMB with
  empty hand → grapple/punch), but moveset content is TBD.

## Open questions

- Does stance flip cancel an in-progress action (a swing, a roll)?
  Souls-style says no; everything commits. Iteration will tell us
  whether players want a quicker out.
- Does grip toggle stance? E.g. when grip switches to two-handed, the
  body naturally reorients — is stance auto-adjusted, or does the
  player still flip stance independently?
- Does block-while-stance-flipping have a vulnerability window? (Real
  Souls doctrine: yes; everything has frames.)
- Is there a "no-stance" / squared-up neutral pose that's distinct
  from either footed stance? Probably useful for unarmed.

## Out-of-scope (re-iterating, because it matters)

- **Incants / spells / casting / natural abilities** — separate doc
  when designed.
- **Ranged attacks** — same.
- **Progression** (weapon evolution, stat growth, imprint-borne
  sangue, crafting) — touches combat but is its own design space;
  lives elsewhere when written.
- **Class-specific differentiation in combat feel** — designed when
  classes have their own combat sections.

## Cross-references

- [Classes](classes.md) — class identity, evolution paths. Eventually
  feeds combat differentiation.
- [Inventory](inventory.md) — equipment slots, weapon classes,
  carrying rules.
- (Future) `combat-incants.md` — non-physical combat path, when designed.
- (Future) `progression.md` or `mastery.md` — how stats grow,
  weapons level, imprint-borne sangue accumulates.
