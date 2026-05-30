# Animals & multi-skeleton support

> **Owns:** the architecture for non-humanoid actors in Selva. Driven
> by the canonical opening-sequence requirement — the Vagrant fights
> the three beasts of *Inferno* Canto I (*lonza*, *leone*, *lupa*)
> per [story.md](story.md) Beat 2. These are the project's first
> non-humanoid actors and force the singleton skeleton/mesh model in
> [`SkeletalAssets.cpp`](../../src/anim/SkeletalAssets.cpp) into a
> keyed registry. The lupa (she-wolf) goes first; lion + leopard
> follow when the asset trio is consolidated.
>
> **Status:** design pre-execution; awaiting review.

## Lore frame

Per [story.md](story.md) Beat 2 ("The beasts"): when the Vagrant
approaches the colle in the opening, all three beasts appear in
Dante's order — lonza, leone, lupa. The Vagrant fights all three.
Combat profile is the Unburdened class
([classes.md](classes.md)) since no measurement has occurred yet.
After they fall the Guide appears (Beat 3, paralleling Virgil's
arrival in *Inferno* I after the retreat).

**Doctrinal exemption: the beasts are NOT *figura umana*.** Per
[bestiary.md](bestiary.md), the bestiary rule is that *every damned
soul wears the human form*, deformed by contrapasso. The three beasts
of Canto I are not damned souls. They are *forces* in the canto
(allegorical readings: incontinence / violence / fraud), staged in
our world as living animals on the slope. Like Cerberus, the
Minotaur, Geryon, and Lucifer, they are the canonical exceptions to
the figura-umana rule — they get their own skeletons. The bestiary's
classical-guardian carve-out is the right precedent.

> The beasts have been here a long time. They have encountered many
> failed pilgrims. Their fight has a *patina* of repetition —
> recognized moves, instinctive responses. They are worn but no
> less terrifying.
> *— story.md Beat 2 (texture note)*

### Lore-tension to resolve

[wood.md:173-221](wood.md) currently says the beasts are *deferred*
and frames them as "forces that turn one back, not opponents." This
contradicts the canonical story.md Beat 2 (combat encounter). The
wood.md framing is stale; **part of this commit must update
wood.md** to point at story.md as the authoritative beats and remove
the deferred/skip-option framing. Beasts as combat encounters is the
locked direction.

## The three beasts

Per Dante (and our staging order, matching the canto):

1. **Lonza** — the leopard / lynx / spotted big cat. Light-footed,
   spotted, first to appear. Allegorically: incontinence / lust.
2. **Leone** — the lion. Head high, raging. Allegorically: violence
   / pride.
3. **Lupa** — the she-wolf. Gaunt, insatiable, the worst. The one
   Dante singles out at length. Allegorically: fraud / avarice /
   incontinent appetite.

Start with the **lupa** (most lore-loaded, most abundant in free 3D
asset libraries). Lion + leopard ship after the lupa pipeline is
proven; their assets likely come together from a paid source (see
*Asset acquisition* below).

## Asset acquisition

No single CC0 source ships matching wolf + lion + leopard. Honest
findings from
[the asset workflow](../../../../.claude/projects/c--Users-alexb-Projects-monobit/9e927aca-1c34-4405-b011-b5861ef20519/tasks/w5668r96r.output):

- **Lupa (immediate, CC0):**
  [Quaternius Ultimate Animated Animal Pack](https://quaternius.com/packs/ultimateanimatedanimals.html)
  — full anim set per animal (idle, walk, run, attack, hit, death,
  jump, +others), FBX + glTF + Blend, CC0, low-poly stylized. Wolf
  is included. Drop-in for the lupa.
- **Trio together (paid):**
  [Omabuarts Quirky Series Animals Ultimate Pack](https://omabuarts.itch.io/quirky-series-animals-ultimate-pack)
  ($299, one purchase). Wolf + lion + lioness + leopard in one
  consistent stylized rig, 18 shared animations, 4 LODs, commercial-
  OK. The only single-source matched trio found.
- **Realistic trio (paid per-model):**
  [vukhiemton on Sketchfab](https://sketchfab.com/vukhiemton/collections/animal-rigged-and-animated-00fee13d0b614a23a2a0c5b379d3c7b7)
  — wolf, lion, snow leopard in realistic style, ~$15–30 each.

**Style register.** Quaternius is flat-shaded stylized low-poly;
X_Bot is mid-detail semi-realistic. There's visible mismatch when
they share a shot. Per the choice during scoping: performance > style
coherence for now, so the lupa ships stylized and the lion/leopard
match style when they ship. Long-term the Selva tonal register
(solemn, period-Italian, Commedia voice) leans semi-realistic, so a
future swap to vukhiemton or Omabuarts is on the table.

**Recommended path:**
1. **Now (this commit):** Quaternius wolf, CC0, drop-in.
2. **Lion + leopard land later:** decide between Omabuarts ($299 for
   matched trio of all 3, would replace the Quaternius wolf too) and
   vukhiemton (~$60 for matched trio in semi-realistic register). The
   decision waits until lion/leopard authoring becomes urgent.

## Engine refactor map

The current pipeline assumes one skeleton (X_Bot, Mixamo conventions)
and one mesh. Per the
[skeleton-pipeline-requirements workflow](../../../../.claude/projects/c--Users-alexb-Projects-monobit/9e927aca-1c34-4405-b011-b5861ef20519/tasks/w5668r96r.output)
+ the
[archetype-actor-system workflow](../../../../.claude/projects/c--Users-alexb-Projects-monobit/9e927aca-1c34-4405-b011-b5861ef20519/tasks/w5668r96r.output),
the lupa forces the following changes:

### 1. SkeletalAssets singleton → keyed registry

- Today: `sSkeleton`, `sPlayerMesh`, `sClips`, `sSampler` are file-
  static singletons in [`SkeletalAssets.cpp:21-25`](../../src/anim/SkeletalAssets.cpp).
  Every actor's `createPoseSampler(skeleton(), playerMesh())` reuses
  them ([`Enemies.cpp:85`](../../src/gameplay/Enemies.cpp),
  [`Actor.cpp:261`](../../src/gameplay/Actor.cpp)).
- Required: promote to
  `std::unordered_map<std::string, Skeleton/SkeletalMesh/ClipRegistry>`
  keyed by skeleton id. Player gets key `"player"` (or `"x_bot"`);
  lupa gets `"wolf"`. `initSkeletalAssets` iterates a manifest of
  `{id, skeleton_path, mesh_path, clip_dir}` and loads each.

### 2. Hardcoded `mixamorig:*` joint names → per-skeleton joint map

- Today: ~10 hardcoded `mixamorig:*` strings in PoseSampler
  ([`:631`](../../src/anim/PoseSampler.cpp) Hips,
  [`:569-570`](../../src/anim/PoseSampler.cpp) leg masks,
  [`:653-658`](../../src/anim/PoseSampler.cpp) foot IK,
  [`:2184`](../../src/anim/PoseSampler.cpp) loco pose-match).
- Required: per-skeleton `SkeletonJointMap` struct loaded from JSON:
  `{ hips, leg_root_left, leg_root_right, feet[], spine_root, ... }`.
  The wolf's `Hips` may be named `Bone.001` or `Spine_0` or whatever
  Quaternius uses; the map decouples the lookup from the Mixamo
  convention.

### 3. ActorVolumes hurtboxes → data-driven per archetype

- Today: [`ActorVolumes.cpp:57-100`](../../src/combat/ActorVolumes.cpp)
  hardcodes 9 capsules between named humanoid joints (`Hips`–`Neck`,
  `LeftUpLeg`–`LeftLeg`, etc.). A wolf with no `LeftUpLeg` collapses
  to a single point.
- Required: archetype JSON gets a `hurtboxes: [...]` array. Each
  entry: `{ joint_a, joint_b, radius_scale, damage_mult }`. Capsule
  is built from those two joint world-positions. Player gets a
  `player_hurtboxes.json` reproducing today's exact 9-capsule layout
  (no behavior change for the player). Wolf authors its own (head,
  shoulders, body, hindquarters).

### 4. `playerMesh().foot_offset_y` → per-actor mesh handle

- Today: 5+ call sites read `selva::anim::playerMesh().foot_offset_y`
  ([`Actor.cpp:192`](../../src/gameplay/Actor.cpp),
  [`BehaviorTree.cpp:86`](../../src/gameplay/BehaviorTree.cpp),
  several spots in
  [`PerFrameTick.cpp`](../../src/gameplay/PerFrameTick.cpp),
  [`ActorHud.cpp:317`](../../src/ui/ActorHud.cpp)).
- Required: every actor resolves its own mesh via its skeleton id;
  `foot_offset_y` queries through that. The wolf's offset is
  different (lower — quadruped on four feet vs humanoid hip).

### 5. Per-skeleton CMake bake target

- Today: one
  [`selva-oscura-convert-attacks`](../../CMakeLists.txt) target uses
  one retarget config pointing at `x_bot/skeleton.ozz`. Funnels every
  FBX in `source/` through Mixamo-shape assumptions.
- Required: parameterize the X_Bot block into a function
  `_selva_register_skeleton(id, fbx_dir, ...)` instantiated twice —
  once for X_Bot, once for the wolf. Wolf FBXs go in
  `assets/characters/wolf/source/`, retarget against
  `assets/characters/wolf/skeleton.ozz`, output to
  `assets/characters/wolf/<clip_name>.ozz`. Both targets feed
  `selva-oscura-sync-config`.

### 6. Hardcoded clip-name constants → per-archetype clip names

- Today: [`Enemies.cpp:44-62`](../../src/gameplay/Enemies.cpp)
  hardcodes `kEnemyPeacefulIdleClipName = "standard_idle"`,
  `kEnemyWalkClipName = "walking"`, `kDeathClipName = "death"`,
  `kKnockdownClipName = "stunned"`, etc. — Mixamo-clip strings the
  wolf doesn't have.
- Required: each archetype declares its own clip names:
  `{ idle_clip: "wolf_idle", walk_clip: "wolf_walk",
     death_clip: "wolf_death", knockdown_clip: null, ... }`. Pattern
  already exists for `Actor.death_clip_name`
  ([`Actor.h:265`](../../include/gameplay/Actor.h)); extend to
  cover idle / walk / flinch / hit-react / knockdown / getting-up.
  Null fields mean "this archetype doesn't have that anim" — the
  state-machine skips it.

### 7. Collider shape: oriented capsule

- Today: `Body.collider_radius`
  ([`Actor.h:127`](../../include/gameplay/Actor.h)) is XZ capsule
  radius — a single vertical capsule. Wolf body is horizontal.
- Required: extend `Body` with
  `enum CapsuleAxis { Vertical, AlongYaw }; float collider_length;`.
  Vertical preserves humanoid behavior; AlongYaw is the wolf path.
  Actor-vs-actor push-out math
  ([`Actor.h:458`](../../include/gameplay/Actor.h)) and Jolt capsule
  sync ([`PerFrameTick.cpp:4049, 4115`](../../src/gameplay/PerFrameTick.cpp))
  both branch on the axis. Two separate capsules per quadruped
  (shoulders + hindquarters) is overkill for v1.

### What stays unchanged

- **PoseSampler** itself
  ([`createPoseSampler(skeleton, mesh)`](../../src/anim/PoseSampler.cpp)
  already takes both as parameters). Once the singletons are
  registry'd, the sampler is fully agnostic.
- **EnemyArchetype / EnemyAction schema**
  ([`EnemyArchetype.h:27-71`](../../include/gameplay/EnemyArchetype.h))
  already has `clip`, `hitbox_joint`, `hitbox_radius` as strings.
  Wolf bite action just sets `{ clip: "wolf_bite", hitbox_joint:
  "Wolf_Jaw" }` — no humanoid assumption.
- **BehaviorTree action firing**
  ([`BehaviorTree.cpp:61-93`](../../src/gameplay/BehaviorTree.cpp))
  — passes `hitbox_joint.c_str()` straight through; no body-part
  enum, no humanoid string match. Wolf's `Wolf_Jaw` flows through
  unchanged.
- **Yaw integration** — wolf rotates around vertical axis through
  hip, same as humanoid. `buildActorModelMatrix` is already
  geometric. Caveat: if Quaternius's wolf rest pose faces +X instead
  of -Z (the humanoid convention), a one-time rest-pose rotation
  baked into mesh import fixes it — no code change.
- **AI barriers, cycle-respawn, region spawn pipeline, multi-
  resident architecture** — all skeleton-agnostic. Wolves authored
  in `surface/region.json` (the colle is in surface region)
  enemy_spawns array, archetype `"wolf"`, AI never crosses any
  region barrier because spawn region stays consistent.

## Scope: one commit

This is the third doctrine-level foundation refactor in the session
(after the multi-resident region refactor in `cbdc80c` and the
Limbo-shades + AI barriers in `3294bb9`). Per
[[feedback_correctness_over_friction]] + [[feedback_no_bandaids_root_cause]]:
no half-step. The wolf-as-shim approach
(`if (is_wolf) use_other_globals`) is the textbook
[[feedback_dual_source_of_truth_is_the_bug]] anti-pattern.

The commit ships:
- Singleton → registry refactor (#1)
- Joint-map per skeleton (#2)
- ActorVolumes data-driven (#3)
- Per-actor mesh/foot_offset (#4)
- CMake parameterized bake (#5)
- Per-archetype clip names (#6)
- Oriented-capsule collider (#7)
- Quaternius wolf assets imported + baked
- `wolf` enemy archetype JSON
- Lupa placed in `surface/region.json` `enemy_spawns` on the colle
  slope (TBD exact coords — wait for the slope's geometry to be
  authored; for v1 a single test wolf at a hand-picked spot)
- `wood.md` lore update: remove the deferred-framing for the beasts,
  point at `story.md` Beat 2 as authoritative

## Opening sequence sequencing

`story.md` Beat 2 has the Vagrant fighting **all three** beasts
before Beat 3 (Guide appears). **Locked: lupa ships alone for now;
lion + leopard are TODO'd until custom assets exist.**

**Why.** No single CC0 source has wolf + lion + leopard (per asset
research). Paid packs ($60–$299) deferred per the no-purchase-now
decision. Custom assets are planned but not yet authored. Three
options were considered:

1. **Ship lupa alone, Beat 2 as stub.** Chosen.
2. **Re-tint Quaternius wolves as lion + leopard.** Rejected — reads
   as 3 wolves of different colors, worse than 1 clean wolf, AND
   guarantees re-authoring later when custom assets land.
3. **Hunt for free lion/leopard from other sources.** Rejected —
   even if found, they wouldn't match the Quaternius wolf's rig or
   style, so all 3 still need re-authoring later. Mismatched
   placeholders are worse than one clean one.

The architecture refactor (singleton → registry, archetype JSON,
etc.) proves itself end-to-end with one beast. Adding lion +
leopard later is content-only — additional registry rows, additional
archetype JSONs, no engine work. The scaling is built in.

**Stub framing for the player.** Leading with the lupa is canon-
defensible: she's the canto's most-lingered-on beast and the
climactic blocker per Dante. A player encountering "just the wolf"
reads as "stub of the full encounter," not "game missing pieces."

**Doc updates this commit:**
- `story.md` Beat 2: add a `**Status:** lupa shipped; lonza + leone
  pending custom assets` marker. Keep the full Beat 2 design text.
- `wood.md` deferred-beasts section: replace with pointer to
  story.md as authoritative + the figura-umana exemption note.
- `bestiary.md`: add "Canto I beasts (lonza, leone, lupa)" to the
  classical-guardian exemption list.

## Final decisions

All open questions resolved at root level — no bandaids:

1. **No paid packs.** Free-only for placeholders; custom assets
   (planned) ship later. Beat 2 plays as a stub (lupa alone) until
   the trio is custom-authored. Reasoning: any paid-pack purchase
   becomes throwaway when custom assets land; better to keep one
   clean stub than ship mismatched placeholders.
2. **Lupa spawn pos:** `[0, "auto_terrain", -90]`. The schema is
   extended this commit so `enemy_spawns[].pos[1]` accepts either
   a float Y (literal world Y — today's behavior, what the 16
   limbo shades use) OR the string `"auto_terrain"` sentinel
   (sample `groundHeight(x, z)` at boot). Generic, reusable for
   ANY future enemy on a slope where the terrain Y is hard to
   author by hand. Wolf is the first user; the 16 limbo shades
   keep their literal `-43.13` (no behavior change).
3. **Lupa collider:** `radius=0.25m`, `length=1.4m`,
   `axis=AlongYaw`. Standard wolf body proportions
   (~1.4m nose-to-tail, ~0.5m body diameter). Tunable post-F7
   from `config/enemies/wolf.json` if it feels wrong; not a
   bandaid because collider feel IS iterative by nature.
4. **Wolf rest-pose orientation:** if the Quaternius wolf imports
   facing +X instead of -Z (the humanoid convention), it's
   **fixed in Blender + re-exported** so the `.glb` ships with
   the correct rest orientation. NO runtime rotation field on
   the joint map. Asset prep handles it once; the engine stays
   skeleton-agnostic. (Wood-beasts framing in `bestiary.md` below
   notes the asset-prep contract for future custom animals too.)
5. **`wood.md`:** rip the deferred-beasts section
   ([wood.md:173-221](wood.md), [wood.md:865](wood.md)), replace
   with a short paragraph pointing at `story.md` Beat 2 as
   authoritative + a note about the figura-umana exemption per
   `bestiary.md`.
6. **`bestiary.md` — new dedicated category, not lumped with
   classical guardians:** the Canto I beasts (lonza, leone, lupa)
   are cosmologically distinct from classical guardians
   (Cerberus, Minotaur, Geryon, Lucifer). Classical guardians are
   *post-mortem Hell-creatures, made by Hell itself, named in the
   canto as Hell's enforcement.* Canto I beasts are *living
   animals encountered BEFORE descent, allegorical forces in the
   canto, not Hell-creatures at all.* Both groups are figura-umana
   exempt but for different reasons: classical guardians because
   Hell made them as monsters; Canto I beasts because they're not
   Hell-creatures. A separate "Wood beasts (Canto I)" section in
   `bestiary.md` preserves the cosmological distinction.

## Doctrine pointers

- [[feedback_correctness_over_friction]] — singleton-to-registry
  refactor done correctly, not shim'd
- [[feedback_dual_source_of_truth_is_the_bug]] — skeleton identity
  currently inferred from 3 places; consolidate to archetype JSON
- [[feedback_diamond_foundations_first]] — wolf is the third
  foundational rebuild this session; ship the right shape
- [[feedback_locomotion_clip_register]] — wolf clips need
  locomotion.json entries with `translation_source: "root_motion"`
  same as humanoid clips
- [[feedback_read_docs_before_lore_reasoning]] — flagged the
  wood.md vs story.md tension because of this doctrine
