# Character canvas

> **Owns:** the physical-parametric side of character creation — what
> a humanoid IS as data, what the canvas exposes, how dev-side
> authoring and player-side creation share one pipeline.
> **Complementary to:** [character-creation.md](character-creation.md)
> (narrative flow / Beats 1-5).

## The canvas philosophy

Every humanoid in Selva — the Vagrant, the Guide, vendors, keepers,
larvae — is a point in the same parametric space. There is no
separate "NPC modeling pipeline" vs "player character pipeline." One
canvas, one parameter set, one rig, one animation library.

This isn't only a tooling convenience. It's cosmologically
load-bearing per [setting.md *Who can absorb released sangue*](setting.md):
damned souls are judged human souls in damned form — all parametric
instances of the same substrate, varying in proportion, age,
decay, and the shape of their suffering. The canvas is the
cosmology made authorable.

**The dev tool IS the player creator.** Both surfaces consume the
same `Appearance` data, drive the same C++ deformation pipeline. The
dev tool exposes the full parameter set (every slider CharMorph provides
plus per-bone scale overrides); the player creator exposes a curated
subset. Whatever the dev authors, the player can save; the dev just
has more knobs.

## What "humanoid" is parametrically

The universe of knobs the canvas covers (shipped + planned):

| Knob | Status | Notes |
| --- | --- | --- |
| body_scale | shipped, dev-only | Uniform model-space scale. Player-facing creator excludes this -- size emerges from gameplay state (class, stats, evolution) per Souls convention. Authoring + NPC config use it freely. |
| head_scale | shipped, player + dev | Per-bone scale on head joint (post-pass). Proportion = sculpting. |
| arm_scale | shipped, player + dev | Both upper-arms + descendants, symmetric. Proportion = sculpting. |
| leg_scale | shipped, player + dev | Both upper-legs + descendants, symmetric. Proportion = sculpting. |
| torso_scale | shipped, player + dev | Spine root + descendants (includes neck/head/arms; counter-scale to isolate). Proportion = sculpting. |
| color (RGB tint) | shipped, player + dev | Per-channel skin tint, [0,1] multiplier |
| CharMorph macro: age | incoming | 18-80 single slider |
| CharMorph macro: body mass | incoming | underweight ↔ heavy |
| CharMorph macro: body tone | incoming | soft ↔ athletic |
| CharMorph phenotype | incoming | Caucasian / Asian / Afro / Anime / Elf / Dwarf |
| CharMorph fine morphs | incoming | ~470 per character; curated subset for player UI |
| Hair (style + color) | incoming | TBD authoring surface |
| Eye color | incoming | RGB / preset |
| Per-bone decay morphs | incoming | larva-specific (gaunt limbs, slumped posture) |

The shipped knobs are the foundation; everything incoming layers
ON TOP without changing the data flow. New knobs become new fields
on `Appearance` and new sliders on the same UI surfaces.

**Tool note (2026-06-23):** Started evaluating MB-Lab (defunct since
2023, incompatible with Blender 5.x). Pivoted to **CharMorph** --
MB-Lab's spiritual successor, uses the same base meshes + morphs from
the ManuelBastioniLAB / MB-Lab heritage, actively maintained, supports
current Blender, integrates Rigify for rigging. CharMorph's feature
set (morphs, phenotypes, hair, clothing, skin/eye color, FBX export)
is a superset of what we'd have needed from MB-Lab. CharMorph is the
actual tool wired into the canvas.

## What's explicitly NOT on the canvas

- **Equipment (weapons)** — `Inventory` / `Equipment` slots. Visible
  weapon meshes anchor onto the rig at runtime; not part of the
  character's identity.
- **Clothing (armor)** — same. Armor swaps the rendered mesh layer;
  the body underneath is unchanged.
- **Dialog / voice** — narrative + audio layer. Per-NPC, but lives
  in dialog config + audio assets, not in `Appearance`.
- **Class identity** — `PlayerProfile.class_id` + identity stats.
  Cosmologically distinct from body: a Penitent and a Heretic with
  identical bodies are still different classes. Class informs WHAT
  the body installs/refuses (sangue, gear) but doesn't change the
  body itself.

## Tool architecture (A2: bake offline, consume native)

CharMorph is a Blender Python addon — it cannot run inside the game
engine. Three architectural options were considered; the right
answer is to use CharMorph as an **offline morph factory** and have the
engine consume baked parametric data at runtime:

- **A1: Engine drives Blender headlessly** — every parameter change
  shells out to Blender. Players would need Blender installed. Killed.
- **A2 (chosen): Bake CharMorph outputs to engine-native data** — CharMorph
  produces N archetypes + morph deltas as engine assets. Runtime UI
  blends within that library natively. Player never sees Blender.
- **A3: Reimplement CharMorph's morph engine in C++** — most native,
  years of work. Killed.

A2 is the natural extension of what already exists. Today's
`Appearance` struct + C++ deformation pipeline is already the
runtime-consumed parametric system; CharMorph integration just adds
richer offline content flowing into the same pattern.

Detailed bake pipeline / CharMorph integration plan: TBD (separate
doc when work starts).

## Current state audit (2026-06-23)

**Shipped, player-facing (CharacterCreationScreen):**
- Name entry (6 chars)
- Color sliders (R / G / B, [0,1])
- Proportion sliders (head / torso / arms / legs)
- Atomic commit on confirm → per-character `config/appearances/<name>.json`

**Shipped, dev-only (F1 Tuning panel → Character tab):**
- All player-facing controls PLUS body_scale (the size axis)
- Live preview against orbit camera
- Save / Revert / Reset against the active character's appearance file

**Why the dev/player split:** body_scale is the literal size knob.
Player-facing creators in Souls don't expose this — character size
emerges from gameplay (class, stats, evolution). The dev tool keeps
it because NPC authoring (larva = 0.6x, keeper = 1.3x) and gameplay
transformations (fresh→aged larva burn) drive size from outside the
player's direct choice.

**Shipped, no UI surface (read by deformation pipeline):**
- All 5 scale knobs on `Appearance` struct
- `applyAppearanceDeformation` post-pass applies them to bone palette

**Incoming (subsequent iterations):**
- CharMorph macro sliders (age / mass / tone)
- Phenotype picker
- Face fine morphs (curated)
- Hair + eye color
- Decay morph axis for larva
- Save/load full preset library (for NPC authoring)

## Cross-references

- [character-creation.md](character-creation.md) — narrative flow
  (Beats 1-5), naming, class-pick framing
- [setting.md *Who can absorb released sangue*](setting.md) — why the
  canvas is cosmologically load-bearing
- [classes.md](classes.md) — the three classes (separate from canvas)
- [story.md *The opening sequence*](story.md) — Beats 1-5 narrative
- `include/gameplay/Appearance.h` — the canvas struct in code
- `src/ui/CharacterCreationScreen.cpp` — player creator
- `src/ui/TuningPanel.cpp` (Character tab) — dev tool
- `src/gameplay/AppearanceDeformation.cpp` — the runtime apply
