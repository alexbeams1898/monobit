# Dialogue

> **Owns:** how characters speak — per-speaker register, dialogue tree
> shape, state-aware branching, choice mechanics.
> **Status:** structural locks; per-NPC trees TBD.

## Register doctrine

Two registers in tension throughout the game:

- **Modern register** — the Vagrant's dialogue trees (the player's
  voice, given to the Vagrant). Modern English. Used for things the
  Vagrant doesn't have old names for: *"some old man with a stick"*
  rather than *"Charon"*; *"this place"* rather than *"Acheron"*.
- **Old register** — Grimoire, gate signage, the death-card's lower
  line, NPC speech, Beatrice's vocabulary. Italian loanwords. Early
  Modern / Chaucer-recognizable English. Locked at
  `.claude/rules-text.md`.

The friction between the two registers is the world's friction-engine:
the player chooses modern phrasings; the world replies in old words.

## Per-speaker register table

| Speaker | Register | Notes |
|---|---|---|
| The Vagrant (dialogue trees) | Modern | Player's voice, given to him |
| The Guide | Old | Calls Vagrant **Pilgrim** (Beatrice's vocabulary fed through him) |
| NPCs (default) | Old | Each circle's NPC speaks in their sin's tonal flavor within old register |
| NPCs (lucid window) | Old, may switch to **Pilgrim** | Address-switch marks the lucidity itself |
| Grimoire | Old, doctrinal | Register-shifts across the game (early entries confident; mid-game entries admit confusion; post-R2 entries name Beatrice) |
| Gate signage / environmental text | Old, bureaucratic | Hell's accounting language |
| Death-card | Mixed | *NOT YET* (modern) / *THOU DOST NOT BELONG* (old) |
| Beatrice (in Grimoire / her own register) | Old, formal | Calls Vagrant **Pilgrim**. Never speaks in-game until her appearance at R4 Branch B/C; even then, fragmentary / rabid |
| Beatrice (in REFUSAL/PURITY combat) | Fragmentary | Rabid, no coherent sentences, no recognition of the Vagrant |

## NPC dialogue is state-aware

Locked at setting.md *NPC dialogue is state-aware*. NPCs perceive the
Vagrant's state and branch on:

- Path (class-picker vs unburdened)
- Evolution stage (Penitent L1/L2/L3, Heretic L1/L2/L3, Wretched
  L1/L2/L3, Unburdened/Svuotato/Diaphanous)
- Riversato lifetime volume
- Lifetime sangue total
- Keepers felled
- Per-NPC encounter history (first encounter, lucid window state,
  keeper-fall state — pre-keeper / post-keeper)

The world *sees* the Vagrant and *responds.* This is what makes the
unburdened path's discovery organic: an unburdened with sangue and
non-zero riversato is spoken to differently than a class-picker with
the same lifetime total.

## Tree shape

Reference target: **Disco Elysium constrained to the hardware**. Deep
trees by NPC, with state-aware branching. Per-NPC tree depth,
breadth, and content TBD.

Constraints:
- **RAM and flash budget.** Trees and string tables must fit per the
  hardware ceiling (~28 KB flash, 2.5 KB RAM). Per-NPC tree depth
  may need to be smaller than reference target.
- **PROGMEM string handling.** All NPC strings live in PROGMEM (or
  on FX flash for once-per-frame data). The PROGMEM stack-buffer
  trap (`.claude/CLAUDE.md` *Safety stubs*) must be respected.
- **State-aware branches must be cheap.** Branch evaluation is
  per-line; state checks should be data lookups, not call-site
  branches.

## Choice mechanics

Dialogue choices express the player's voice through the Vagrant.
What choices affect:

- **Branch selection** (the immediate next line).
- **Per-NPC encounter history** (which lucid-window content has been
  shown).
- **Possibly: NPC reward branch** (different rewards per dialogue
  outcome — TBD per NPC).
- **Not: stats.** Dialogue does not directly grant stat increases —
  that path goes through the Crucible (class-picker installation).
- **Not: HP.** Dialogue does not heal — healing comes from the
  Guide's projections, NPC items, or other TBD mechanisms.

Dialogue is the player's expression. The Vagrant is whatever the
player makes him.

## The Vagrant speaks; the player chooses what he says (LOCKED)

The Vagrant has a voice. When the player selects a dialogue choice,
the Vagrant actually says that line in the world — NPCs hear it; the
act of speaking happens. Diegetically: he is a person who speaks; the
player is the agency that decides what he says. Standard
player-character relationship, no special metafiction required.

Implications:

- NPCs hear what he says. They can quote him back, dispute his
  words, correct him, repeat them in different register, build on
  them. Reply patterns are not constrained to intent-reading;
  characters can respond to literal speech.
- Modern-register dialogue choices are the lines he speaks. He
  speaks in the modern register; that is his tongue.
- The register split (modern player choices / old NPC voice) stays
  as a stylistic device. It is no longer a metaphysical claim about
  "thought language" — it is a tonal choice. The Vagrant speaks
  modern; the NPCs speak old. The friction is intentional and reads
  as cultural / temporal / register distance between the Vagrant
  and the Commedia-voiced world he has arrived into.
- UI: dialogue choices are shown as the Vagrant's voice (modern,
  plain, listed). NPC replies are shown as the NPC's voice (old
  register, attributed to them). When NPC reply quotes or refers to
  what the Vagrant said, that is the standard player-character
  dialogue feedback loop.

## What no one says (concealment doctrine)

- **No NPC mentions Beatrice** until R2 (posthumous Grimoire entries
  unlock at the Guide's death). Per story.md *Reveals / R2*. The
  player has had no information about her at all — only the
  unidentified title-screen image.
- **No NPC mentions the throne** before the post-Lucifer sequence. R3
  is concealed throughout the descent.
- **No NPC mentions PURITY by name** before the unburdened path's
  discovery. Hints (the Guide's worry on Signing-refusal, the riversamento
  sites that activate state-aware NPC dialogue) seed the path
  without naming it.

## Open questions

- Per-NPC voice differentiation within the old register. Each NPC
  needs an audible / readable identity within the same baseline
  vocabulary. Defer to per-NPC writing.
- Tree depth budget per NPC (constrained by flash).
- Whether dialogue choices have a UI cost (e.g., the Vagrant takes
  damage from delay, or NPC patience runs out). Defer to gameplay
  tuning.
- The Guide's tree at the climax — fragmentary, broken-liturgy
  combat dialogue (story.md *The climax*). Specific lines TBD;
  per-author rule (design-doc authorship: scaffold and ask, do not
  generate user-facing content).

## Cross-references

- [Setting](setting.md) — *Naming convention*, *NPC dialogue is
  state-aware*, *NPCs* (blinkered to circle).
- [Story](story.md) — *Voice register* (Vagrant), *The Guide / Calls
  the Vagrant Pilgrim*, *Reveals* (what gets revealed when).
- [Companions](companions.md) — the Guide is the dialogue-volume
  center.
- [`.claude/rules-text.md`](../../.claude/rules-text.md) — register
  doctrine for any user-facing string.
