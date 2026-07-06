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
dev tool exposes the full parameter set (every macro + fine morph the
authoring tool provides plus per-bone scale overrides); the player
creator exposes a curated subset. Whatever the dev authors, the
player can save; the dev just has more knobs.

## What "humanoid" is parametrically

The universe of knobs the canvas covers (shipped + planned).
`Creator?` = exposed on the player character-creation screen;
`Transformable?` = some AppearanceTransform can write to it via
the resolver (see *The world shapes the body* below).

Note: macros are bake-time only; "transformable" for a macro means
"a re-bake-triggering event could change it," not runtime per-frame.
Bone-scale + fine morph axes are transformable per-frame.

| Knob | Status | Creator? | Transformable? | Notes |
| --- | --- | --- | --- | --- |
| body_scale | shipped | no | yes | Uniform model-space scale. SIZE axis -- player never picks size; emerges from gameplay (class evolution, etc.). Authoring + NPC config use it freely. |
| head_scale | shipped | no | yes | Per-bone scale on head joint. SIZE axis (not proportion). |
| arm_scale | shipped | no | yes | Both upper-arms + descendants, symmetric. SIZE axis. |
| leg_scale | shipped | no | yes | Both upper-legs + descendants, symmetric. SIZE axis. |
| torso_scale | shipped | no | yes | Spine root + descendants (includes neck/head/arms). SIZE axis. |
| color (RGB tint) | shipped | yes | yes | Per-channel skin tint, [0,1] multiplier. Sangue / decay transforms tint over time. |
| MPFB2 macro: age | incoming | yes | yes (re-bake) | 18-80 single slider. Long-residence transforms re-bake on rare trigger. |
| MPFB2 macro: body mass | incoming | yes | yes (re-bake) | underweight ↔ heavy. Same re-bake gating. |
| MPFB2 macro: body tone | incoming | yes | yes (re-bake) | soft ↔ athletic. Same re-bake gating. |
| MPFB2 macro: face structure | incoming | yes | no | Caucasian / Asian / African free blend -- starting face shape. Cosmologically not "race" in Hell; just face structure. Creator-only; world doesn't rewrite skull shape. |
| MPFB2 fine morphs (face) | incoming | yes (curated) | partial | Per-region face sliders (eyes, nose, mouth, jaw, cheeks). Most are creator-only identity; a curated subset is transform-touchable (eye bag depth, cheekbone prominence, mouth angles). |
| MPFB2 fine morphs (body) | incoming | yes (curated) | partial | Proportion sliders that don't change size (shoulder width, hip width, neck length, waist thickness). Posture transforms write to a subset (shoulder slump, spine curve). |
| Hair (style + color) | incoming | yes | partial | Style is creator-only; color may transform with age. TBD authoring surface (separate mesh, not morphs). |
| Eye color | incoming | yes | no | Iris RGB / preset. Possession events could glow-tint as a flag, not a creator-axis transform. |
| Per-bone decay morphs | incoming | no | yes | Larva-specific (gaunt limbs, slumped posture). Transform-driven, not creator. |

The shipped knobs are the foundation; everything incoming layers
ON TOP without changing the data flow. New knobs become new fields
on `Appearance` and new sliders on the same UI surfaces.

**Tool note (2026-06-27):** Evaluated MB-Lab (defunct), then CharMorph
(works, but produces a Rigify deformation rig that fights Mixamo's
animation library at every joint -- the retarget pipeline required
~700 LOC of rest-pose corrections and still had visible artifacts).
Migrated to **MPFB2** (MakeHuman Plugin for Blender 2, v2.0.16+):
same MakeHuman lineage as CharMorph for morphs/phenotypes, but ships
a first-class "mixamo" rig type that produces the canonical 52-bone
mixamorig: skeleton out of the box. Mixamo animations attach with
zero retargeting via MPFB2's `map_mixamo` operator -- the entire
retarget pipeline collapsed to a thin bake step. See
[[humanoid-pipeline-mpfb2]] in agent memory for the migration details.

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

## The world shapes the body (LOAD-BEARING)

**The player's appearance is not fixed by the creator. The creator
sets the BASE; the world applies transformations on top of it.**

Selva is a soulslike where the player IS a soul inside Hell's
ecology — subject to the same shaping forces that warp every other
soul in the substrate. Contrapasso, sangue saturation, time spent
in each circle, what the body consumes, how the body is used —
these aren't just narrative flavor. They write to the same
`Appearance` data that drives the rendered mesh.

A creator-shipped slim athletic Penitent who spends fifty hours
hauling sangue through gluttony-circle is transformed toward bloat
and darker tint by the time he stops. A creator-shipped wiry
Heretic who skips sleep across the run is transformed into
hollow-eyed gauntness. The face that exits the character-creation
screen IS the player's character; the face after twenty hours of
play is **also** the player's character, weathered by where the
player took them.

This is the design wedge versus typical soulslikes: the creator
isn't the last word on what the player looks like, it's the first.

### Why this is canonical, not flavor

Per [setting.md *Who can absorb released sangue*](setting.md): damned
souls are judged human souls in damned form, parametric instances of
the same substrate. The player is a not-yet-judged soul moving
through the same substrate. The Wood-side ledger and the Hell-side
substrate both write to the soul; the soul's form expresses what's
been written. **A soul that has been doing what THIS soul has been
doing should look like a soul that has been doing what this soul has
been doing.** The creator's role is to define what the soul looked
like at the moment it entered the *selva oscura* — not what it will
look like after it has lived in there.

### The AppearanceTransform primitive (LOAD-BEARING)

The mechanism by which the world shapes the body is the
**AppearanceTransform** — a generalization of the existing
`transform_target_archetype` pattern that already drives fresh→aged
larva burn-in. One primitive, one resolver, one render-path consumer.
Every gameplay event that affects appearance, on player or NPC,
flows through this primitive.

#### Data shape

Each Actor's `Appearance` carries a `transforms: vector<AppearanceTransform>`.
An entry has:

- **`id`** — stable identifier matching the snapshot filename.
- **`source_kind`** — what kind of world condition drives it
  (`flag` | `stat_threshold` | `residence_time` | `evolution_node` |
  `boss_felled` | `archetype_transform` | …).
- **`target_snapshot_path`** — `config/appearances/transforms/<id>.json`
  containing an `Appearance` snapshot with ONLY the axes this
  transform touches.
- **`strength`** — 0..1, the lerp weight from base toward target.

A snapshot file is itself an `Appearance` JSON; absent fields mean
"this transform doesn't touch that axis." The render resolver lerps
the live appearance from the base toward each active transform's
target by that transform's strength. Multiple transforms compose
additively per-axis; per-axis clamp lives in the slider registry.

#### Why one primitive

The existing `transform_target_archetype` system already does
exactly this for NPCs (one transform, lerped via
`resolveAppearance(actor, now_wc)`). Generalizing to a vector of
transforms with the same shape and the same render-path call site
gets us:

- Multiple simultaneous effects (sangue_bloat + violence_muscle +
  felled_lupa_scar all stacking on the player at once).
- One audit point — `config/appearances/transforms/` lists every
  thing in the game that can change a body. No scattered "where do
  appearance writes happen?" question.
- One render call site — `resolveLiveAppearance()` replaces
  `resolveAppearance()`; nothing else in the render path moves.
- Zero engine code per new transformation — just a new snapshot
  file plus the gameplay system that owns the trigger registers a
  source.

#### Per-frame reconciler

A new module `AppearanceTransformWriter` owns a process-wide
registry of **TransformSources**. Each registered source declares:

- The transform `id` it produces.
- An **`active(actor) -> bool`** predicate — when does it apply?
- A **`strength(actor) -> float`** function — how strongly, given
  current world state?
- The `target_snapshot_path` it points at.

Per frame, for each actor, the reconciler walks every registered
source. If a source is `active`, the corresponding transform
appears in `actor.appearance.transforms` with the source's current
`strength`. If a source goes inactive, its transform is removed.

This is intentionally derivation, not accumulation:
`actor.appearance.transforms` is a pure function of world state.
Save/load restores the world state (flags + stats); the reconciler
rebuilds the transforms list on next tick. No leaked state on
missed push/pop.

Sources are registered by the systems that own the triggering flag
or stat — `SangueSystem::init()` registers `sangue_bloat`,
`EvolutionSystem::init()` registers per-evolution-node transforms,
etc. Each registration is a small one-time call at boot. New
transforms = one snapshot file + one registration call.

#### Class evolution writes to size via the same primitive

The existing bone-scale axes (`head_scale`, `torso_scale`,
`arm_scale`, `leg_scale`, `body_scale`) remain on `Appearance` AND
remain creator-invisible — **the player cannot set them in the
creator**. They are the SIZE axes. Souls don't pick how big they
are; they grow under what they accumulate.

When the Vagrant evolves into a new class form, an
EvolutionSystem-registered TransformSource activates for that node
and points at `config/appearances/transforms/class_evolved_<node>.json`
— a snapshot whose only non-default fields are the size axes
(`body_scale: 3.0`, etc.). The character has the same identity
(same face structure, same proportions, same colors), but is now
bigger / smaller / differently weighted.

This is the cosmological inverse of the typical creator: in Souls,
the player picks size at character-creation and it never changes.
In Selva, the player NEVER picks size; size is what the world has
made the soul into. The creator is for *who you were*. Every size
change after that is a transformation, recorded as such.

#### Reversibility falls out of the design

There's no separate "reversibility" mechanism — it's a consequence
of how the source declares itself.

- **Reversible**: source.active(actor) toggles with world state.
  Sangue bloat stops being active when sangue load drops below the
  threshold; the transform disappears next frame.
- **Continuously-scaled reversible**: source.active is always true
  in some range; source.strength is `f(stat)`. The transform stays
  in the list with strength ramping to match world state.
- **Permanent**: source.active(actor) becomes true on a flag flip
  and never goes back to false. Class evolution flags don't
  un-flip; the transform stays in the list forever.

No per-axis decay rates, no separate reversibility config — the
declaration of the source IS the reversibility specification.

### Examples (not exhaustive — the transform library catalogs them)

Each row is a single TransformSource registration + one snapshot
file. The full library lives at `config/appearances/transforms/`.

| Trigger | source_kind | Snapshot touches | Reversible? |
| --- | --- | --- | --- |
| High sangue load | stat_threshold | torso bloat morph + bordeaux tint | yes |
| Class evolution into form X | evolution_node | bone scales | no |
| Boss felled (sangue scar) | boss_felled | per-boss scar morphs | no |
| Time in gluttony circle | residence_time | body mass macro morph + face fat | partial (slow) |
| Time in violence circle | residence_time | muscle tone morphs | partial |
| Sleepless / trauma streak | flag | eye bag depth + pallor | yes (resolves on rest) |
| Becoming Burdened | flag | posture-slump morphs | yes (resolves on un-burdening) |
| Starvation streak | flag | cheekbones + face width morphs | yes |
| Larva fresh → aged (existing NPC case) | archetype_transform | full archetype snapshot | no |

## Macro axes vs fine morphs — the bake fork (LOAD-BEARING)

MPFB2 splits its parameter space into two structurally different
layers. Recognizing this split is critical because it forces
different runtime architectures per layer.

### The split

- **Macro axes** (gender, age, mass, muscle, face structure) are
  *bake-time only* in MPFB2. They are not single morph deltas — each
  is a multi-component blend across pre-authored corner meshes (e.g.
  the weight macro samples across ~27 corner files combinatorial
  with age and muscle). MPFB2's bake collapses them into the base
  vertex positions; once baked, they are frozen geometry.
- **Fine morphs** (nose-hump-incr, cheek-bones-incr, eye-bags-incr,
  upperarm-muscle-incr, etc.) are *single-target shape keys* that
  load AFTER the macro bake and survive into the .glb as runtime-
  tweakable glTF morph targets. Each is a standalone .target.gz
  file.

### Why this matters

- **Macros are frozen at creation time** — we cannot run-time-slide
  age from 25 → 50; that would require re-baking the entire mesh.
- **Fine morphs are runtime-free** — drive their weights every frame
  with zero bake cost. AppearanceTransforms write to these.

### Commit-on-creation, re-bake-on-rare-trigger pattern

The pattern: the creator commits ONCE — macro choices write into a
per-character baked mesh, and the parametric choices are frozen
afterward until a rare in-fiction event triggers a re-bake. This
is the standard soulslike approach. Day-to-day appearance changes
from gameplay are handled by runtime overlays (textures, decals,
secondary morphs) on top of the baked base, NOT by re-baking.

What's different here vs the genre baseline: the genre baseline
ships runtime overlays for a small curated set of conditions
(hollowing, status effects, hit decals). We ship runtime overlays
for the full ~80 transform-touchable fine-morph axes driven by
AppearanceTransforms (see *The AppearanceTransform primitive* above).
Same architecture, far wider expression — this is the wedge that
makes the body system distinct.

### Character creation flow

1. Player completes the creator (macros + initial fine-morph picks
   + name + class).
2. Atomic commit triggers a headless Blender bake: `gen_humanoid.py`
   runs with the player's macro values frozen into base vertices +
   every transform-touchable fine morph loaded as 0-weight shape
   keys. Produces a per-character .glb under
   `assets/characters/players/<name>.glb`.
3. A loading surface is shown during the bake (~30s estimated;
   first-pass profiling required). **Copy lives in the
   implementation layer, not in this doc**, and must not name the
   cosmology (see the player-facing-surfaces rule for the opening's
   non-disclosure constraints).
4. Engine loads the per-character .glb the same way it loads any
   other humanoid bundle. The shipped fine-morph weights resolve to
   the player's initial creator picks; AppearanceTransforms layer
   on top via the per-frame resolver.

### Re-bake trigger

The engine path is generic: any code path that calls a
`rebakePlayerAppearance(profile)` entry point triggers the same
gen_humanoid.py flow with whatever macros the player has at that
moment. What in-fiction event(s) invoke that path is a design
question owned by [story.md](story.md), not this doc.

### Fine morphs that we expose at creator commit time

Every fine morph that ANY TransformSource writes to must be loaded
as a 0-weight shape key during the creator bake. If we don't load
it, the runtime can't drive it. There is no cost to loading a fine
morph at 0 weight (the vertex shader's loop skips zero-weight
contributions at near-zero cost), so we err on the side of loading
all transform-touchable fine morphs even if no creator slider
currently exposes them.

The transform-touchable fine-morph list is the SUPERSET of the
player-creator slider list — i.e. some morphs the world writes to
but the player never sees in the creator. The list is derived: walk
every transform snapshot in `config/appearances/transforms/`, union
all morph keys mentioned. The bake script consumes that union.

## Tool architecture (A2: bake offline, consume native)

MPFB2 is a Blender extension — it cannot run inside the game engine.
Three architectural options were considered; the right answer is to
use MPFB2 as an **offline morph factory** and have the engine consume
baked parametric data at runtime:

- **A1: Engine drives Blender headlessly** — every parameter change
  shells out to Blender. Players would need Blender installed. Killed.
- **A2 (chosen): Bake MPFB2 outputs to engine-native data** — MPFB2
  produces N archetypes + morph deltas as engine assets. Runtime UI
  blends within that library natively. Player never sees Blender.
- **A3: Reimplement the morph engine in C++** — most native, years
  of work. Killed.

A2 is the natural extension of what already exists. Today's
`Appearance` struct + C++ deformation pipeline is already the
runtime-consumed parametric system; MPFB2 integration just adds
richer offline content flowing into the same pattern.

Detailed morph-bake pipeline plan: TBD (separate doc when work
starts). Today's pipeline (gen_humanoid_male.py + bake_clip.py) is
the foundation -- generates a static parametric snapshot per actor
variant; the morph-blend extension layers on top.

## Current state audit (2026-06-29)

**Shipped, player-facing (CharacterCreationScreen):**
- Name entry (6 chars)
- Body Type discrete pick (humanoid_male / humanoid_female)
- Color sliders (R / G / B, [0,1])
- Bone-scale sliders (head / torso / arms / legs) -- **scheduled to
  be REMOVED from creator** per the new doctrine (bone scale = SIZE
  axis, transform-driven). They stay on `Appearance` + in dev tools.
- One proof-of-concept morph slider (Nose Hump) demonstrating the
  end-to-end glTF morph-target pipeline
- Atomic commit on confirm → per-character `config/appearances/<name>.json`

**Shipped, dev-only (F1 Tuning panel → Character tab):**
- All player-facing controls PLUS body_scale (the size axis)
- Live preview against orbit camera
- Save / Revert / Reset against the active character's appearance file

**Shipped, engine plumbing:**
- All 5 bone-scale knobs on `Appearance` struct
- `applyAppearanceDeformation` post-pass applies bone scales to bone palette
- `morph_weights` map on `Appearance`; vertex shader applies them
  pre-skinning per the glTF morph-target spec
- `transform_target_archetype` field on `EnemyArchetype` + the
  `resolveAppearance(actor, now_wc)` lerp that drives the
  fresh→aged larva burn. **This is the prior art that the
  AppearanceTransform primitive generalizes** -- one-target case
  becomes a one-entry transforms list.

**Incoming (subsequent iterations):**
- Migrate creator UI to the categorized navigator (Identity / Face
  Structure / Eyes / Nose / Mouth / Ears / Proportions / Skin / Hair
  / Eye Color sections, Souls-style zoom-into-section)
- Remove bone-scale sliders from player creator (move to dev only)
- MPFB2 macro sliders (age / mass / tone / face structure A/B/C)
- Curated MPFB2 face fine morphs per category above
- Curated MPFB2 body proportion morphs (don't affect size)
- Hair + eye color
- Per-bone decay morphs for larva (transform-driven, not creator-visible)
- Save/load full preset library (for NPC authoring)
- **`Appearance.transforms` vector + AppearanceTransform struct** --
  generalize `transform_target_archetype` into a vector of
  named transforms with per-transform strength.
- **`resolveLiveAppearance()`** replaces today's
  `resolveAppearance()`; consumes the transforms list.
- **AppearanceTransformWriter subsystem** -- per-frame reconciler
  that walks registered TransformSources, evaluates their `active`
  predicate + `strength` function against the actor, and updates
  `actor.appearance.transforms` accordingly.
- **Per-system source registration** -- each gameplay system that
  owns a triggering flag/stat registers its TransformSource at boot
  (SangueSystem, EvolutionSystem, ConsumptionSystem, etc.).
- **`config/appearances/transforms/<id>.json`** -- per-transform
  snapshot files; each contains only the Appearance axes that
  transform touches.

## Cross-references

- [character-creation.md](character-creation.md) — narrative flow
  (Beats 1-5), naming, class-pick framing
- [setting.md *Who can absorb released sangue*](setting.md) — why the
  canvas is cosmologically load-bearing
- [classes.md](classes.md) — the three classes (separate from canvas)
- [story.md *The opening sequence*](story.md) — Beats 1-5 narrative
- `config/appearances/transforms/` — the on-disk library of every
  AppearanceTransform in the game (each file is the snapshot for
  one transform; `ls`-ing this directory IS the audit of "every
  thing in the game that can change a body")
- `include/gameplay/Appearance.h` — the canvas struct in code
  (carries `transforms: vector<AppearanceTransform>`)
- `include/gameplay/AppearanceTransformWriter.h` — per-frame
  reconciler + TransformSource registry
- `src/ui/CharacterCreationScreen.cpp` — player creator
- `src/ui/TuningPanel.cpp` (Character tab) — dev tool
- `src/gameplay/AppearanceDeformation.cpp` — the runtime apply
- `src/gameplay/PerFrameTick.cpp` — render call site
  (`resolveLiveAppearance()`, generalization of today's
  `resolveAppearance()`)
