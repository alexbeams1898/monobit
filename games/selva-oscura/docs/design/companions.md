# Companions

> **Owns:** characters who travel with the player — currently the
> Guide. Their AI, narrative role, how they enter and leave the party.
> **Status:** Guide is fully drawn; mechanical specifics TBD.

## The Guide is the only companion

Per story.md *The Guide* and setting.md *The Guide* — the Guide is
the sole companion in the game. There are no party members, no
recruitable allies, no alternate companions per path. The Guide is
the dialogue-volume center of the game; full character drawn in
story.md.

## What the Guide does, mechanically

Locked at story.md *The Guide / Functions* and *Geography*:

- **Wood-side presence (real body).** The Guide is in the Wood
  between runs. The Vagrant interacts with him in the hub. Provides
  warmth, dialogue, and access to OFFERINGS (stat upgrades for class-
  pickers).
- **Hell-side projections.** Per-keeper interludes (between a circle's
  play segment and its boss), the Guide appears as a projection
  sustained by Beatrice's intervention. Offers heal (sangue-for-heal,
  RELIC-equivalent) and OFFERINGS access. The Guide is not aware of
  the cosmological role these functions play; he believes he is
  providing standard guide-services.
- **Degradation across cycles.** As keepers fall, the surfacing
  sangue of each broken circle routes through the channel Beatrice
  constructed and erodes him. The Hell-projection fails first (cracks
  at keepers 4-6, rare at 7-8, stops entirely after all 9). The
  Wood-body holds longer (visibly tired by 4-6, forgets the Vagrant's
  path by 7-8). **After keeper 8 (Fraud), the Guide vanishes from
  the Wood** — Beatrice routes him to Cocytus as the Treachery-tier
  successor presence (per setting.md *Per-circle reactivity /
  Treachery (Circle 9)*). The Wood hub goes empty. Full degradation
  table in story.md.
- **Climax encounter — in Cocytus.** The Vagrant encounters the
  Guide at the floor of the frozen lake during the Lucifer descent.
  Fully degraded, saturated past coherence. Class-picker path:
  combat (Guide attacks, cycling through all 9 circles' sin-
  signature attack patterns; Vagrant kills him). Unburdened path:
  handover (Guide hands over the Hand without resistance; fades; no
  fight).
- **Parallel saturation with Beatrice (unburdened only).** On the
  unburdened path, the Guide's destruction *parallels* what is
  happening to Beatrice — both are being saturated by sangue routed
  through Beatrice's infrastructure (channel for the Guide;
  reservoir for Beatrice). Two of Beatrice's instruments
  destroyed/transformed by the same mechanism; the unburdened path
  kills both as cost. See setting.md *Beatrice / Sangue saturation*.

## Beatrice is not a companion

Beatrice exists in lore and sprite art (she is the title-screen
figure). She is **not a companion** — see setting.md *Beatrice* and
story.md *Reveals / R2*.

She is offstage throughout the game (no one in the game knows about
her until R2). She appears as a *boss* on REFUSAL and PURITY (per
setting.md *Cocytus and the post-Lucifer arena* — Branches B and
C), not as an ally. She is not selectable, not partyable, not
controllable.

The existing Beatrice sprite-set (NPC dialog poses, intro cutscene
storyboard) was scaffolded before this design lock and may be partly
repurposable for the title-screen disintegration / boss-fight
animations. Specific reuse decisions deferred to art pipeline.

## AI / behavior

*Guide's per-keeper interlude appearance, his cracking-mid-sentence
behavior, his unreliable-projection late-cycle state — all TBD at
implementation level.*

Constraints:
- The Guide must be implementable as an entity in the same pool as
  player and enemies (entity symmetry rule). He is data, not type.
- His Hell-projection appearances are not full combat encounters
  (until the climax); they are interaction points (heal, OFFERINGS).
- His climax combat profile cycles through 9 broken circles' sin-
  signature patterns (wind / weight / poison / fire / hook / ice /
  etc.) — this is an authored boss, not procedural.

## Loyalty / state

The Guide's *loyalty* is irrelevant — he is not a controllable AI
ally with state. He is a hub-side / interlude-side NPC the Vagrant
interacts with. State that matters:

- Cycle stage (degradation level — table in story.md).
- Path (Guide treats class-picker and unburdened differently from
  Beat 4 onward; he calls everyone *Pilgrim*).
- Whether he has been visited at the current per-keeper interlude
  (so projections don't double-fire).

## Open questions

- Per-cycle Guide-projection mechanic (when exactly he appears, how
  the player knows he's available).
- Whether the Wood-body and Hell-projection can be visited at
  different states in the same cycle (e.g. Wood-body visibly tired
  while Hell-projection still appears coherently in the next interlude).
- Climax combat profile specifics — the sin-signature pattern cycling
  needs per-circle attack patterns designed.
- Audio cues for the Guide's degradation state across the game.

## Cross-references

- [Story](story.md) — *The Guide* (full character: Identity, Geography,
  Functions, Degradation, Climax, The Hand).
- [Setting](setting.md) — *The Guide* (cosmological role).
- [PC vs NPC](pc-vs-npc.md) — companions are NPCs sharing the Entity
  struct with the player. Mechanically symmetric.
- [Dialogue](dialogue.md) — Guide's dialogue volume is the largest
  in the game.
- [Inventory](inventory.md) — the Hand replaces the Guide's
  Wood-Guide functions after his death (path-specific).
