# UX

> **Owns:** the player-facing surface — screen flow, HUD, menus,
> control scheme, footer-hint conventions.
> **Status:** locks for footer-hints; rest TBD.

## Footer button hints (locked convention)

Every screen that shows button hints at the bottom follows one rule:
**A first, then B, then R (RIGHT) if present.**

Example shapes:

- `A:OFFER  B:BACK`
- `A:READ  B:BACK`
- `A:BEGIN  B:BACK`
- `A:AVOW` (single-button)
- `B:BACK` (single-button, when A is not meaningful)

**Formatting rules:**
- No space after `:` inside a pair (`A:READ`, not `A: READ`).
- Two spaces between pairs.
- **BACK** is the universal back verb — never `RETURN`.
- **READ** is the universal inspect verb — used for entering detail
  pages (bestiary detail, TEXT body) regardless of whether the page
  itself is text or stats.
- **R:KNOW** (the Crucible stat-spend right-arrow peek) is a
  *withheld* control: it exists but is not advertised in the
  footer. Discoverable, not signposted.

Why this matters: a 128×64 screen cannot spare inconsistency. One
rule, one voice — the player never has to re-read the footer in a
different order on a different screen.

## Screen flow

*TBD — what scenes exist, in what order, what transitions between
them. Engineering substrate already exists (TITLE / MAIN_MENU /
GATE / PLAY scene-paging banks); design question is what *content*
lives in each.*

Locked-by-implication from setting.md / story.md / fallback.md:
- Title screen with Beatrice portrait + slow-disintegration
  animation (per story.md / R2 reveal architecture).
- Death cutscene → death-card overlay (*NOT YET / THOU DOST NOT
  BELONG*) → run-stats screen (no header) → respawn-in-Wood (per
  fallback.md *Death*).
- Opening sequence Beats 1-5 (per story.md *The opening sequence*).
- Per-keeper interlude (Guide projection appears, offers heal /
  dialogue — per companions.md, story.md *The Guide / Functions*).
- Climax: Guide encounter → keeper 9 → post-Lucifer (path-dependent).

Specific screen content, transitions, and ordering: TBD at
implementation.

## HUD

*TBD — what's on screen during play. HP bar, held-substance counter
(reads from the Vagrant; per the locked cosmology he IS the
holding zone -- no separate vessel/wound feature), etc.*

Locked-by-implication:
- Three stats follow the engine triad (HP / fire_rate / damage).
- The held-substance counter (replacing the older "wallet" concept)
  reads double-vinculum Roman numerals; cap at 999,999,999 (the
  cosmological ceiling -- 9^9, per setting.md *Hell's accounting
  cap*).
- Per [[project_substance_has_no_in_game_name]]: the substance
  itself is NEVER labeled in any diegetic surface. The commit-verb
  has a name (Crucible / Censer); the substance does not.
- HUD must stay in Hell's register (Early Modern English; Italian
  loanwords only where load-bearing per
  [[project_foreign_language_load_bearing]]).

## Menus

*TBD — pause menu, inventory menu, class-picker UI, Crucible stat-
spend panel, etc.*

Locked-by-implication:
- Vestigia UI exists and shipped phase-3 (per memory + fallback.md).
- *Inscribe* / *Restore* are the working save/load verbs (per
  setting.md *Vestigia*).
- Each per-screen dirty cache cuts ungated `draw()` cost (per
  CLAUDE.md *Per-screen dirty cache*).

## Control scheme

*TBD — Arduboy has 6 buttons (Up/Down/Left/Right/A/B). What does
each do in each context? Default mapping must be Arduboy-shaped; SDL
gets the equivalent.*

Locked-by-implication:
- A = primary action / confirm (per footer-hint convention above).
- B = back / cancel.
- R (RIGHT) = inspect / peek (rare, withheld; used for the
  Crucible's stat-KNOW peek).

## Name-entry screen

The Vagrant's name is given by the player at Beat 3 of the opening
sequence (per story.md / character-creation.md). Six letters,
elicited by the Guide. UI specifics: TBD.

## Cross-references

- [Setting](setting.md) — register doctrine, *Inscribe* / *Restore*
  verbs, the vinculum-Roman accounting cap.
- [Story](story.md) — opening sequence, R5 final-scene per ending,
  death-card spec.
- [Fallback](fallback.md) — death-cutscene flow, run-stats screen,
  respawn semantics.
- [Inventory](inventory.md) — inventory screen (open).
- [Companions](companions.md) — Guide interlude UX.
- [Options](options.md) — the settings menu specifically.
- [Character creation](character-creation.md) — opening UX flow.
- [`docs/scene-paging.md`](../scene-paging.md) — engineering
  constraint on scene transitions (~400 ms swap).
- [`.claude/rules-scenes.md`](../../.claude/rules-scenes.md) — when
  a swap is allowed (player-perceptible loads only).
