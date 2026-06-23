# Animals & multi-skeleton support

> **Owns:** the architecture for non-humanoid actors in Selva.
>
> Originally driven by the opening-sequence beast encounter. Per the
> **2026-05-31 wood-lore lock** ([[selva-wood-lore-locked-2026-05-31]]),
> the opening encounter is now **Lupa alone** — Lonza and Leone died
> during Hell's stagnation before the Vagrant arrived. The
> multi-skeleton architecture still ships (it's needed for Lupa, for
> classical guardians, and for the descendant-form Selva-organism
> ecology that emerges as the Wood heals — see
> [creatures.md](creatures.md)).
>
> Status (2026-05-31): Lupa engine integration COMPLETE — joint map,
> hurtboxes, archetype, bake all wired. The singleton skeleton/mesh
> model in [`SkeletalAssets.cpp`](../../src/anim/SkeletalAssets.cpp)
> has been promoted to a keyed registry per
> [commit f9466af](#). CMake refactor for the bake pipeline deferred
> to issue #131 (currently shipping via manually-baked committed .ozz
> files). Combat tuning + spawn-placement + smoke-test verification:
> WIP.

## Lore frame

Per [story.md](story.md) Beat 2 and [bestiary.md](bestiary.md)
*The three legends of the Wood*: the pre-failure *selva oscura* had
three named apex-legends on the colle's south slope — **Lonza**,
**Leone**, **Lupa**. Per the wood-lore lock, **Lonza and Leone died
during Hell's stagnation, before the Vagrant arrived. Only Lupa
survives at game-start.** The opening encounter is the Vagrant
fighting Lupa alone (Beat 2); the Guide arrives in Beat 3 after she
falls. Combat profile is the Unburdened class
([classes.md](classes.md)) since no measurement has occurred yet.

**Doctrinal exemption: the legends are NOT *figura umana*.** Per
[bestiary.md](bestiary.md), the bestiary rule is that *every damned
soul wears the human form*, deformed by contrapasso. Lonza, Leone,
Lupa are not damned souls — they are pre-failure apex-fauna of the
threshold-Wood, *real biological animals + allegorical forces given
physical form*. Like Cerberus, the Minotaur, Geryon, and Lucifer,
they are exempt from the figura-umana rule and get their own
skeletons. The bestiary's three-exemption-classes framework (legends
/ Selva-organisms / classical guardians) is the right precedent.

**Proper-name doctrine (per
[[selva-epistemic-doctrine-2026-05-31]]):** Lonza, Leone, Lupa are
proper names of specific individual creatures, NOT species labels.
Used capitalized, no article ("Lupa stalks the slope," NOT "the lupa
stalks the slope"). There is exactly one Lonza, one Leone, one Lupa.
Italian is reserved for legends; descendant Selva-organism species
get English-coined fantasy names.

> Lupa has been here a long time — long enough to have outlasted
> her sister-legends, long enough to have hunted in a Wood
> progressively emptying around her. Her fight has a *patina* of
> endurance — wary moves, instinct sharpened by lonely centuries.
> She is worn but no less terrifying.
> *— story.md Beat 2 (texture note, refined 2026-05-31)*

## The three legends

Per Dante's Canto I source text (and the staging in the pre-failure
Wood, before two of them died):

1. **Lonza** — the leopard / lynx / spotted big cat. Light-footed,
   spotted. Allegorically: incontinence / lust. **DEAD** (died during
   Hell's stagnation; never encountered by the Vagrant).
2. **Leone** — the lion. Head high, raging. Allegorically: violence
   / pride. **DEAD** (same).
3. **Lupa** — the she-wolf. Gaunt, insatiable, the worst. The one
   Dante singles out at length. Allegorically: fraud / avarice /
   incontinent appetite. **SOLE SURVIVOR**, Beat 2 opening boss,
   `permanent_on_death: true`.

Lupa is the project's first and currently only non-humanoid actor.
Future non-humanoid actors include classical guardians (Cerberus
etc.) and the descendant-form Selva-organisms — both leverage the
same multi-skeleton architecture.

## Asset acquisition

No single CC0 source ships matching wolf + lion + leopard. Honest
findings from
[the asset workflow](../../../../.claude/projects/c--Users-alexb-Projects-monobit/9e927aca-1c34-4405-b011-b5861ef20519/tasks/w5668r96r.output):

- **Lupa (immediate, CC0):**
  [Quaternius Ultimate Animated Animal Pack](https://quaternius.com/packs/ultimateanimatedanimals.html)
  — full anim set per animal (idle, walk, run, attack, hit, death,
  jump, +others), FBX + glTF + Blend, CC0, low-poly stylized. Wolf
  is included. Drop-in for Lupa.
- **Trio together (paid):**
  [Omabuarts Quirky Series Animals Ultimate Pack](https://omabuarts.itch.io/quirky-series-animals-ultimate-pack)
  ($299, one purchase). Wolf + lion + lioness + leopard in one
  consistent stylized rig, 18 shared animations, 4 LODs, commercial-
  OK. The only single-source matched trio found.
- **Realistic trio (paid per-model):**
  [vukhiemton on Sketchfab](https://sketchfab.com/vukhiemton/collections/animal-rigged-and-animated-00fee13d0b614a23a2a0c5b379d3c7b7)
  — wolf, lion, snow leopard in realistic style, ~$15–30 each.

**Style register.** Quaternius is flat-shaded stylized low-poly;
the humanoid rig is mid-detail semi-realistic. There's visible mismatch when
they share a shot. Per the choice during scoping: performance > style
coherence for now, so Lupa ships stylized. Long-term the Selva tonal
register (solemn, period-Italian, Commedia voice) leans
semi-realistic, so a future swap to vukhiemton or a similar realistic
wolf rig is on the table — that would be a Lupa visual upgrade, not
a content addition.

**Note (2026-05-31):** earlier versions of this doc assumed Lupa was
the first of three (Lonza + Leone + Lupa) and discussed paid packs
for shipping the full trio. Per the wood-lore lock, Lonza and Leone
are DEAD in canon and have no shipping plan — they exist only as
biological remnants in the soil that feed the descendant Selva-
organism ecology. The original asset-acquisition recommendation
(Quaternius wolf for Lupa) is the final answer; the paid-trio
options are no longer relevant.

## Engine refactor map

The current pipeline assumes one skeleton (humanoid, Mixamo conventions)
and one mesh. Per the
[skeleton-pipeline-requirements workflow](../../../../.claude/projects/c--Users-alexb-Projects-monobit/9e927aca-1c34-4405-b011-b5861ef20519/tasks/w5668r96r.output)
+ the
[archetype-actor-system workflow](../../../../.claude/projects/c--Users-alexb-Projects-monobit/9e927aca-1c34-4405-b011-b5861ef20519/tasks/w5668r96r.output),
Lupa forces the following changes:

### 1. SkeletalAssets singleton → keyed registry

- Today: `sSkeleton`, `sPlayerMesh`, `sClips`, `sSampler` are file-
  static singletons in [`SkeletalAssets.cpp:21-25`](../../src/anim/SkeletalAssets.cpp).
  Every actor's `createPoseSampler(skeleton(), playerMesh())` reuses
  them ([`Enemies.cpp:85`](../../src/gameplay/Enemies.cpp),
  [`Actor.cpp:261`](../../src/gameplay/Actor.cpp)).
- Required: promote to
  `std::unordered_map<std::string, Skeleton/SkeletalMesh/ClipRegistry>`
  keyed by skeleton id. Player gets key `"player"` (or `"humanoid"`);
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
  one retarget config pointing at `humanoid/skeleton.ozz`. Funnels every
  FBX in `source/` through Mixamo-shape assumptions.
- Required: parameterize the humanoid block into a function
  `_selva_register_skeleton(id, fbx_dir, ...)` instantiated twice —
  once for the humanoid, once for the wolf. Wolf FBXs go in
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

**Per the 2026-05-31 wood-lore lock**
([[selva-wood-lore-locked-2026-05-31]]): Beat 2 is the Vagrant
fighting **Lupa alone**. Beat 3 is the Guide arriving after she
falls. The earlier "all three beasts" framing is superseded — Lonza
and Leone died during Hell's stagnation, before the Vagrant ever
arrived. They are not a stub or a future-shipping item; they are
DEAD in canon.

This collapses the previous "stub framing" question. The architecture
work (singleton → registry, archetype JSON, per-skeleton joint maps,
per-skeleton hurtbox layouts) was justified in its own right — Lupa
is non-humanoid, classical guardians are non-humanoid, and the
descendant-form Selva-organisms (per [creatures.md](creatures.md))
will each need their own skeletons too. The multi-skeleton
architecture proves itself end-to-end with Lupa and scales naturally
to every future non-humanoid.

**Lupa's silhouette returns in the descendant ecology.** The Wood
healing as the Vagrant restores keepers brings new life that echoes
the three legends — Lupa-shape descendants, Leone-shape descendants,
Lonza-shape descendants — but sangue-infused and English-named.
The legends themselves don't return; their silhouettes do, transformed
by the catch (sangue tagging along with the healing). See
[creatures.md](creatures.md) for the descendant-ecology framework.

**Doc updates this commit (HISTORICAL — done in the 2026-05-31 doc
sweep, see [[selva-wood-lore-locked-2026-05-31]]):**
- `story.md` Beat 2 — rewritten to "Lupa, the last legend"
- `wood.md` — Lore section + three-legends section rewritten;
  healing-with-catch framing locked
- `bestiary.md` — three-exemption-classes framework (legends /
  Selva-organisms / classical guardians) installed
- `setting.md` — Wood-is-sangue section updated with healing-with-
  catch framing; topology line corrected to Lupa-alone
- `creatures.md` — cosmological framing updated with per-keeper
  healing causes the leak as catch; descendant-form ecology framed
  as sangue-infused echoes of the three legends

## Final decisions

All open questions resolved at root level — no bandaids:

1. **No paid packs for Lupa.** Quaternius CC0 wolf is the shipping
   asset. Per the 2026-05-31 wood-lore lock, Lonza and Leone are
   dead in canon — there is no "trio" to ship. Lupa-alone IS the
   canonical Beat 2 encounter, not a stub. The earlier framing of
   Beat 2 as "lupa-alone stub awaiting custom trio assets" is
   superseded.
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
5. **`wood.md`:** DONE in the 2026-05-31 doc sweep. Lore-failure
   section + three-legends section rewritten with the
   healing-with-catch framing and the Lupa-only Beat 2 framing.
6. **`bestiary.md` — three exemption categories** (per the 2026-05-31
   wood-lore lock): **the three legends** (Lonza, Leone, Lupa — only
   Lupa alive), **Selva-organisms** (the descendant-form ecology
   that emerges as the Wood heals), and **classical guardians**
   (Cerberus, Minotaur, Geryon, Lucifer). Each is cosmologically
   distinct and figura-umana exempt for its own reason. DONE in
   the 2026-05-31 doc sweep.

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
