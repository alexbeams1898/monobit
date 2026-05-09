# Selva Oscura — Runtime Architecture

This is the system reference for what's currently built in
`games/selva-oscura/`. It describes systems that exist today, not the
forward-looking plan (that lives in
`engines/engine/docs/3D-EXTENSION.md`). When something changes
structurally, update this doc — file paths and line numbers go stale,
but the system shapes here should remain accurate.

The voice register / story / class design canon lives under
`docs/design/` and `docs/reference/`. This doc is purely the runtime
machinery.

---

## 1. The 30-second tour

Selva Oscura is a 3D souls-like running on the engine in
`engines/engine/`. The game's job, every frame:

1. Read input (keyboard, mouse).
2. Decide what the player is doing — locomotion intent, combat input,
   dodge, block.
3. Pick a locomotion clip (idle / walk / run / combat-idle) and tell
   the animation sampler to play it on the **locomotion track**.
4. If a discrete action fires (attack / dodge / block), tell the
   sampler to play it on the **one-shot track**, layered over locomotion.
5. Read back per-frame hip translation from the clip and apply it (in
   world space) to the player's position.
6. Render: floor, grid, scene cube, skinned player mesh.

Two tracks blended together is the central design. The locomotion
track is always running underneath; the one-shot track wins when
active and fades out cleanly. The state machinery decides what plays
on which track and when.

---

## 2. Source map

```
games/selva-oscura/
├── src/main.cpp                  orchestration: per-frame tick, render callbacks, main()
├── src/WallClock.cpp             selva::wallClock() / advanceWallClock() - global frame clock
├── src/anim/
│   ├── PoseSampler.cpp           dual-track blend, inertialization, phase-match, hip motion
│   ├── ClipRegistry.cpp          name→AnimationClip directory loader
│   ├── Skeleton.cpp              ozz::animation::Skeleton wrapper
│   ├── SkeletalMesh.cpp          GPU buffers + bone palette
│   ├── SkeletalRenderer.cpp      drawSkeletalMesh
│   ├── SkeletalAssets.cpp        skeleton/mesh/clips/sampler/locomotionConfig singletons
│   ├── LocomotionConfig.cpp      per-clip blend-in overrides from JSON
│   └── AnimationClip.cpp         ozz::animation::Animation wrapper
├── src/combat/
│   ├── AttackChain.cpp           AttackChainState/BufferedPress/PendingFirstAction + tickChainExpiry
│   ├── AttackResolution.cpp      one-time startup pass (cancel-open, chain-link-start)
│   ├── AttackResolver.cpp        ResolvedAttack/TechniqueDispatch dispatchers
│   ├── CombatData.cpp            registries (weapon classes/weapons/equipment) + load + helpers
│   ├── CombatLog.cpp             combatLog() + log file open/close
│   ├── SpliceDiag.cpp            5-joint diag capture/log + poseMatchStartFromLoco
│   ├── TransitionProfile.cpp     profile struct + 6 named profiles + fireOneShotWithProfile
│   └── WeaponData.cpp            JSON loaders for weapon class / weapon / equipment
├── src/gameplay/
│   └── PlayerState.cpp           sPlayer + wrapAngleSigned + yawFromGroundDir
├── src/render/
│   ├── Camera.cpp                cameraYaw/Pitch + window size + onWindowResize
│   ├── SceneGeometry.cpp         cube/floor/grid/axes VAOs + draw helpers
│   ├── SceneShaders.cpp          scene shader program + uniform setters
│   └── WorldRenderer.cpp         buildViewProj + renderEnvironment
├── src/ui/
│   └── ComboHud.cpp              renderComboHud + nextExpectedButtonLabel
├── include/                      matching .h files for everything above
├── config/
│   ├── tunables.json             runtime feel parameters
│   ├── locomotion.json           per-clip blend overrides
│   ├── loadout.json              player's equipped weapons
│   ├── weapon_classes/*.json     animation chains per weapon type
│   └── weapons/*.json            per-weapon stat sheets
├── assets/characters/x_bot/
│   ├── source/                   Mixamo FBX (gitignored, locally authored)
│   │   ├── rig/                  X_Bot.fbx (skeleton + mesh)
│   │   ├── loco/                 idle, walking, running, run_to_stop
│   │   ├── combat/sword/         attacks, blocks, idle, reposed
│   │   ├── combat/unarmed/       jab, hook, combo, etc.
│   │   └── dodge/                falling_to_roll, standing_dodge_backward, stand_to_roll
│   ├── baked-archive/            kept-but-FBX-less .ozz files (tracked)
│   └── (build output: skeleton.ozz, X_Bot.glb, *.ozz clips)
├── docs/
│   ├── ARCHITECTURE.md           this doc
│   ├── BACKLOG.md                deferred items
│   ├── design/                   game-design canon (story, classes, etc.)
│   └── reference/                Dante source texts
├── tests/                        Catch2 unit tests
└── CMakeLists.txt                build pipeline + asset bake
```

`main.cpp` is monolithic by design — most state lives at file scope as
`static` (e.g. `sPlayer`, `sLocomotionSM`, `sChainRight`,
`sLocoLockoutUntil`). When a system grows enough to be tested in
isolation, it gets pulled into `src/anim/` or `include/combat/`.

---

## 3. The frame tick

Entry: `int main(int argc, char* argv[])` near the bottom of `main.cpp`
(~line 4262). It bootstraps the engine, registers two callbacks, and
hands control off:

- **Per-frame update**: `selvaPerFrame(Engine&, EntityManager&, double dt)`
  (~line 1896). Reads input, runs all gameplay, ticks the animation
  sampler.
- **Render**: `selvaRenderWorld(Engine&, EntityManager&, ...)` (~line 3602).
  Sets up the camera, draws floor/grid/cube, draws the skinned player.

Per-frame ordering inside `selvaPerFrame`:

1. Scale `dt` by `tunables.time_scale` (debug knob — slows the whole
   simulation for animation inspection).
2. Update the wall-clock counter `sWallClockSeconds`.
3. Mouse-look → camera yaw/pitch.
4. Read keyboard, derive intent.
5. Run combat input handlers — attack chain, block, dodge tap/hold.
6. Decide locomotion clip via the locomotion SM.
7. `sSampler.update(*clip, dt, blend_seconds, loops)` — advances both
   tracks, runs blend math.
8. Read back hip translation, apply to `sPlayer.pos` (rotated by yaw).
9. Optional debug recording (CSV/PNG capture).

The SM and the sampler are decoupled: the SM picks a clip name and a
blend duration; the sampler doesn't care why. The sampler's job is to
get from whatever it was playing to whatever was just requested,
smoothly.

---

## 4. Animation runtime

### 4.1 Core types

All in `games/selva-oscura/include/anim/` and the matching `src/anim/`:

- **`Skeleton`** — wraps `ozz::animation::Skeleton`. One per character
  rig. Loaded from `skeleton.ozz`, which is baked from `X_Bot.fbx` via
  `gltf2ozz`.
- **`AnimationClip`** — wraps a single `ozz::animation::Animation`.
  Holds duration + track count. Loaded from `*.ozz` files baked from
  per-clip FBXs.
- **`SkeletalMesh`** — GPU buffers + inverse bind matrices + an
  asset-root transform (Mixamo's "Character" node has a 0.01 scale
  baked in, applied at load time so the runtime sees a 1:1 mesh) +
  `foot_offset_y` (lowest vertex Y in bind pose, used to plant feet
  at world Y=0).
- **`ClipRegistry`** — a name → `AnimationClip` map. `loadDirectory()`
  scans a folder for `.ozz` files and loads each. The runtime calls
  this once at startup against `assets/characters/x_bot/`.
- **`PoseSampler`** — the heart of the system. Section 4.3.
- **`SkeletalRenderer`** (`drawSkeletalMesh`) — issues a skinned draw
  call given a mesh, a model matrix, a view-projection, and a bone
  palette (one matrix per bone, ozz joint order, model-space).

### 4.2 The bake pipeline (CMake)

Mixamo ships FBX. Ozz wants `.ozz`. The CMake build chain owns the
conversion:

1. **Rig**: `source/rig/X_Bot.fbx` → `X_Bot.glb` (FBX2glTF) →
   `skeleton.ozz` (gltf2ozz with `import: enable=true`, no animations).
   This produces the canonical skeleton every clip retargets against.
   Target: `selva-oscura-convert-xbot`.
2. **Per-clip animations**: each `*.fbx` under
   `source/{loco,combat,dodge}/` gets:
   - `FBX2glTF` → per-clip `.glb` in a temp dir
   - `gltf2ozz` with a retarget config that says "use X_Bot's
     skeleton, don't import a new one, optimize, bake out root motion"
   - rename `mixamo.com.ozz` → `<sanitized_name>.ozz`
   Target: `selva-oscura-convert-attacks`. CMake uses `GLOB_RECURSE`
   over `source/*.fbx` minus the rig, so adding a clip is just
   dropping its FBX into the right purpose dir.
3. **Filename sanitization**: `_selva_sanitize_clip_name()` lowercases
   and replaces non-alphanumeric runs with underscores. `"sword and
   shield slash (2).fbx"` → `sword_and_shield_slash_2.ozz`.
4. **Archived clips**: `assets/characters/x_bot/baked-archive/`
   contains pre-baked `.ozz` files whose source FBX is no longer in
   the tree (kept for future use). `selva-oscura-stage-archive` copies
   them into the runtime output. The directory has a local
   `.gitignore` exception so the `*.ozz` rule in the repo-root
   `.gitignore` doesn't ignore them.

The runtime sees a flat `build/bin/assets/characters/x_bot/` of
`.ozz` files plus `skeleton.ozz` and `X_Bot.glb`. It doesn't know
about the source layout.

### 4.3 PoseSampler: the dual-track blend

Entry point per frame:

```cpp
sSampler.update(*clip, dt, blend_seconds, loops);
```

The sampler maintains three tracks:

- **`loco_current`** — the locomotion clip the SM most recently asked
  for. Loops.
- **`loco_previous`** — the *previous* locomotion clip, fading out.
  Cleared once the crossfade completes.
- **`one_shot`** — the active discrete action (attack, dodge, block
  raise). Does not loop. When weight > 0, dominates locomotion; when
  weight = 0 and finished, locomotion alone is visible.

The blend math each frame (rough):

```
loco_blended = lerp(loco_previous, loco_current, loco_blend_weight)
final_pose   = lerp(loco_blended, one_shot, mask × one_shot_weight)
```

The mask is a per-joint SimdFloat4 vector of 0/1 weights. `BodyMask::Full`
is all 1s; `BodyMask::UpperBody` is 0s for hips and legs, 1s for spine
and arms (so the legs keep walking under an attack swing). All
attacks fire `Full` today — `UpperBody` was tried but produced
visible leg-spasm where the walking hip cycle and the punch's hip
drive fought each other.

#### Phase matching (gait→gait crossfade)

When `loco_current` changes between two **gait clips** (walk → run, or
the reverse), starting the new clip at t=0 produces a foot-snap (you
land on a frame where the wrong leg is forward). The sampler fixes
this by computing each clip's hip path length once at first use,
caching it on the track, and — if both source and destination clear a
0.5m threshold — starting the new clip at the same *gait phase
fraction* as the source. Walk at 60% of its cycle (left foot down,
right swinging) hands off to run at 60% of its cycle (left foot
down). Idle has near-zero hip path so it's never phase-matched into
or out of.

#### Inertialization (per-joint pose decay)

When a transition jumps between poses that aren't pose-matched (e.g.
first attack from a locomotion stance, attack-link from a recovery
pose), the visible result without smoothing is a snap — joints
interpolate linearly, but the interpolation runs OVER the new clip's
authored motion, which already moves those joints. The result reads
as a brief "wrong direction" stutter.

Inertialization fixes this by capturing the per-joint delta between
the source pose and the new clip's t=0, and decaying that delta to
zero over a short window. Two flavors:

- `requestInertialization(duration)` — capture happens at the next
  `update()` call against the live previous-frame pose. Used for
  first-strike and chain-link splices where the source is "whatever's
  on screen right now."
- `requestInertializationFromPose(duration, baseline_pose)` — use a
  supplied baseline pose as the source. Used when first-strike fires
  from a peaceful stance and you want the inertialization to bridge
  *to* the canonical combat-stance baseline rather than from the
  literal idle frame.

Decay duration is **per-joint**: `base + scale × |offset_radians|`,
clamped to `max`. Joints with small offsets (hands, mostly aligned
already) finish their decay quickly; joints with big offsets (hip
flip, full stance change) get longer windows so the motion reads as
smooth rather than as a fast snap. The three numbers
(`inertialize_decay_base_seconds`, `_scale_per_radian`,
`_max_seconds`) live in `tunables.json`.

**Important constraint**: don't bake an inertialization request into
helper functions. `playOneShot()` does NOT enroll inertialization
itself — the caller decides whether to call `requestInertialization`
first. This is enforced as a `TransitionProfile` field
(`enroll_inertialization`) and a previous bug — `playOneShot` silently
setting `pending_capture = true` — burned three debug cycles before
the fix landed in commit 757bf41. See
`memory/feedback_read_function_bodies.md` if you're touching this.

#### Hip root motion

The sampler internally freezes the hip's XZ to the rest-pose origin
each frame (so the visible bones don't drift). It exposes the
*consumed* delta via `consumedHipDelta()` — the translation the clip
authored that frame, which gameplay code reads, rotates by player
yaw, and applies to `sPlayer.pos`.

This is the standard root-motion pattern: clip author the hip travel,
the engine consumes it. Gameplay doesn't double-translate the
character; the clip's authored motion *is* the character's motion
during attacks/dodges.

The dodge clip (`falling_to_roll`) authors ~3.0m of forward hip
travel, which becomes the entire dodge displacement. Walking and
running have hip translation too, but their gameplay paths use plain
WASD + `tunables.move_speed` for predictable feel; the sampler still
consumes the hip delta (so the bones stay on-model) but gameplay
ignores it.

Discontinuities (clip change, one-shot fire, time wrap) emit a zero
delta — without that, the first frame after a switch sees the new
clip's t=0 hip pos minus the old clip's last sampled hip pos and
translates the player by the difference, producing a visible snap.

#### Clip motion analysis (combat tuning)

A handful of helpers scan a clip and return semantic times:

- `clipJointMotionStart(clip, joints, quiet_fraction)` — when the
  watched joints first move significantly. Used to skip past a clip's
  windup when entering it mid-chain (the new attack starts at the
  point its motion actually begins, not the canonical t=0 windup
  pose).
- `clipJointMotionPeak(clip, joints)` — the moment of peak velocity.
  Defines the "contact" of an attack and gates the cancel-window
  search.
- `clipJointMotionEnd(clip, joints, quiet_fraction)` — when the
  watched joints settle below a fraction of peak. Defines when the
  cancel window opens — i.e. when the player should be allowed to
  press the next attack.
- `clipPoseMatchTime(prev_clip, prev_t, new_clip, joints, lo, hi)` —
  scans the new clip in `[lo, hi]` for the time whose joint world
  positions are closest to the source pose. Used at chain-link
  splices to find an entry point that matches the outgoing one-shot's
  current pose, minimizing the inertialization offset.
- `clipVelocityMatchTime` — same idea but matches velocity instead of
  position. Currently unused but available.

These run at chain-resolution time (startup) and are cached in the
attack record's `resolved_*` fields, so the runtime fire path doesn't
do scan work.

#### `playOneShot` interface

```cpp
playOneShot(clip, blend_in, blend_out, mask, start_seconds, rate, freeze_last)
```

- `start_seconds` — clip-time to begin at. 0 = canonical start. Used
  to skip a clip's leading-idle frames (e.g. running attack begins
  mid-stride; sprint-finisher start trim crops the windup).
- `rate` — playback rate multiplier. >1 snaps the clip; <1 slows it.
- `freeze_last` — when the one-shot finishes (or is released), hold
  the last frame instead of fading to locomotion. Used for the
  unarmed block (which has no separate block-idle clip).

Calling `playOneShot` again while one is active cancels the current
and starts the new one (with a fresh blend-in). The `OneShotPhase`
state machine (`Inactive → BlendIn → Hold → BlendOut → Inactive`)
governs the weight curve and auto-advances to `BlendOut` when the
clip's playhead reaches `duration - blend_out_seconds × rate` (unless
`freeze_last` is set, in which case it stays in `Hold` until
`releaseOneShot()` is called).

`isOneShotActive()` returns true the moment `playOneShot` is called
through the final tick of `BlendOut`. Gameplay reads this to gate
locomotion (see section 7).

---

## 5. The locomotion state machine

`LocomotionStateMachine sLocomotionSM` lives at file scope. The state
graph is small:

- `Idle` — standing.
- `Walk` — moving at default speed.
- `Run` — sprinting.
- `Transitioning` — playing an explicit transition clip (currently
  unused; `selectTransitionClip` returns nullptr always, falling back
  to phase-matched crossfade).

Plus an orthogonal axis:

- `CombatStance` — `Peaceful` or `CombatReady`.
- `CombatStanceFoot` — `Default` or `Mirror` (for a future
  opposite-foot moveset; currently visual selection is identical).

The clip pick is a 2D table:

|                  | Peaceful           | CombatReady (armed)    | CombatReady (unarmed) |
|------------------|--------------------|------------------------|------------------------|
| Idle             | `standard_idle`    | `sword_and_shield_idle_4` | `unarmed_combat_idle` |
| Walk             | `walking`          | `walking`              | `walking`              |
| Run              | `running`          | `running`              | `running`              |

Walk/Run currently share clips across stances — combat-aware walk/run
variants exist as Mixamo clips in the archive but aren't wired up.

`tickLocomotionStateMachine` runs each frame:

1. Update combat stance: any combat input pushes
   `stance_active_until` forward by `combat_idle_grace_seconds`. An
   attack fire pushes it by `clip_duration + grace` so the stance
   holds through recovery (otherwise the finisher's last clip runs
   out while the player is still in the recovery and the SM drops
   stance — visually jarring).
2. If transitioning and the clip just ended, hand off to the target
   state.
3. If steady state matches intent, return the loop clip.
4. Otherwise pick a transition (always nullptr today) or fall back to
   crossfade — the SM just hands the new clip name to the sampler and
   trusts the sampler's blend.

Per-clip blend overrides live in `config/locomotion.json` — e.g.
`run → idle` is intentionally slower than `idle → run` so quick
direction changes feel snappy but stops feel weighted. The SM picks a
default blend (0.10 for transitions, 0.20 for state changes) and
`LocomotionConfig::blendInSeconds` overrides it per destination clip.

---

## 6. Combat: chains, profiles, and the fire path

### 6.1 The chain state

Two `AttackChainState` instances — `sChainRight` and `sChainLeft` —
hold the per-hand state of an in-progress chain:

- `chain_index` — current step (0 = first attack).
- `technique_index` — which technique (within the active grip's light
  or heavy slot) is locked. -1 before the first press.
- `chain_kind` — `Light` or `Heavy`. Switching mid-chain resets to
  step 0.
- `cancel_window_open_at` — wall-clock when the next press becomes
  valid. The press CAN'T advance the chain before this. Computed at
  fire time from the just-fired clip's motion-end.
- `cancel_window_close_at` — wall-clock past which the press is
  considered late. Late presses still work (we don't drop input on
  the floor) but score lower on the rhythm meter.
- `chain_reset_at` — wall-clock at which the chain auto-resets to
  step 0 if no further presses happen. Long for non-finishers (allows
  a slow mid-chain press), short for finishers (snappy combo-end
  recovery).
- `is_finisher` — true once the *last* step has fired. No new attacks
  during a finisher's recovery; chain must reset first.
- `last_press_accuracy` — 1.0 at window center, 0.0 at edge. Used by
  the future scoring/feedback system.

### 6.2 Resolution + dispatch

`resolveAttackChainEntry(equipment, hand, kind, chain_index, technique_index)`
picks the WeaponClass, drills into the right grip's anim set
(`one_handed.light[technique_index]` or similar), and returns a
`ResolvedAttack { attack, clip, chain_size }`. `chain_size` tells the
caller "is this the last step?"

`dispatchTechniqueForPress(eq, hand, kind, chain_index, current_locked, button)`
handles button-routing within a chain. Some techniques are
`LMB → RMB → LMB`; others are `LMB → LMB → RMB`. On the first press,
it picks the technique whose slot-0 button matches; on subsequent
presses, it advances within the locked technique only if
`expected_button` matches. Mismatch is a "rhythm miss" — the press
was timed correctly but on the wrong button. (Currently the chain
just doesn't advance; future polish: visual feedback.)

### 6.3 The buffered-press mechanic

If the player presses LMB before `cancel_window_open_at`, the press
is **buffered**, not dropped:

```cpp
struct BufferedPress { bool pending; AttackKind kind; const char* button; float buffered_at; };
```

When the cancel window opens, the buffered press fires automatically.
Buffer expires after `tunables.combo_input_buffer_seconds` (~0.5s).
`sBufferedRight` / `sBufferedLeft` are independent.

### 6.4 The fire path

Roughly, on a press edge (LMB or RMB rising edge):

1. `tryAttackInput(hand, edge_this_frame, button)` — checks rhythm
   window, buffers if early, returns whether to fire.
2. If firing: `fireAttack(hand, kind)`:
   - If the kind changed (Light → Heavy or vice versa), reset the
     chain.
   - Sprint-swap check: if `sPlayer.sprinting && kind == Light`,
     substitute the running-attack clip from
     `WeaponGripAnimSet::running` (e.g. `flying_knee_punch_combo`).
     Treats the chain as a single-step finisher.
   - Otherwise resolve the clip via `resolveAttackChainEntry`.
   - Compute the blend duration: per-attack override
     (`chain_link_blend_seconds`) wins over global tunable.
   - Compute the start time: pose-match scan against the loco track
     for first-strike, against the outgoing one-shot for chain-link.
     Sprint-swap uses the tunable start-trim.
   - Pick the `TransitionProfile`: `firstStrike`, `chainLink`, or
     `sprintFinisher`.
   - `fireOneShotWithProfile(clip, profile, start_seconds, rate)`.
   - Compute the cancel/reset windows from the clip's motion-end and
     the next step's motion-start (cached at startup). Update
     `chain.cancel_window_*_at`, `chain.chain_reset_at`.
   - `applyProfileLockout(profile, cancel_window_close_at)` — see
     section 6.5.
   - Advance `chain_index` (or set `is_finisher = true` if this was
     the last step).

### 6.5 TransitionProfile

The whole "fire a one-shot from the live state" pattern recurs in six
places (three attack variants, two block variants, one dodge). Each
has its own combination of: blend duration, source-pose strategy,
inertialization, lockout, mask, freeze-last. Hard-coding those at
each call site meant a change to one risked regressing the others
(and made cross-site consistency invisible).

`TransitionProfile` puts every axis on one struct:

```cpp
struct TransitionProfile {
    enum class SourcePrep { None, SnapLocoToZero };
    enum class Lockout    { None, CancelWindowClose, WallClockSeconds };
    float blend_in_seconds   = 0.20f;
    float blend_out_seconds  = 0.20f;
    SourcePrep source_prep   = None;
    bool enroll_inertialization = true;
    Lockout lockout          = None;
    float lockout_seconds    = 0.0f;
    BodyMask mask            = Full;
    bool freeze_last         = false;
};
```

The six profiles in `namespace profiles` (`main.cpp` ~1446-1520):

| Profile           | Use case                              | Blend | Inert | Lockout                  |
|-------------------|---------------------------------------|-------|-------|--------------------------|
| `firstStrike`     | cold attack from combat-idle          | 0.10s | yes   | CancelWindowClose        |
| `chainLink`       | mid-chain attack splice               | 0.22s | yes   | CancelWindowClose        |
| `sprintFinisher`  | sprint+LMB → running attack           | 0.10s | NO    | WallClockSeconds (1.0s)  |
| `blockFromLatch`  | block fired through combat-entry latch | 0.10s | yes   | None                     |
| `blockLive`       | block fired live from CombatReady     | 0.10s | yes   | None (snap loco to t=0)  |
| `dodge`           | roll / backstep                        | 0.10s | NO    | None (own gate via sDodgeActive) |

`fireOneShotWithProfile(clip, profile, start_seconds, rate)` is the
one entry point: applies source-prep, enrolls inertialization if
requested, calls `playOneShot`. `applyProfileLockout(profile,
cancel_window_close_at)` is the post-fire lockout assignment, called
separately because the cancel-window anchor only exists after the
chain code computes it.

The decision tree `is_sprint_swap ? sprintFinisher() : first_strike ?
firstStrike() : chainLink()` lives in `fireAttack`. To add a new
transition variant: write a profile, route to it from the right
caller. Don't inline the settings.

### 6.6 The loco lockout chain

Three flags decide whether locomotion responds to WASD this frame:

- `attack_in_flight = sSampler.isOneShotActive() || sPendingFirstAction.active`
  — true while a one-shot is blending in / playing / blending out, or
  while the first-action latch is waiting to fire.
- `in_attack_recovery = sWallClockSeconds < sLocoLockoutUntil`
  — true while the post-fire lockout window hasn't elapsed.
- `loco_lockout = attack_in_flight || in_attack_recovery`
- `is_moving = wasd_intent && !loco_lockout`

`sLocoLockoutUntil` is set by `applyProfileLockout`:

- `CancelWindowClose`: `cancel_window_close_at + attack_lockout_extension_seconds`
  (the attack's recovery window plus a small grace).
- `WallClockSeconds`: `now + profile.lockout_seconds` (sprint-finisher
  uses 1.0s — the running attack commits, can't be canceled).
- `None`: no change. Dodge and block don't lock locomotion this way;
  dodge has its own `sDodgeActive` gate, block is upper-body.

This gating is what makes attacks "commit" — the player can't break
out of an attack by holding W. They can buffer the next attack press
(see 6.3) and they can fire a dodge after `dodge_attack_cancel_fraction`
of dodge duration, but locomotion is locked until the lockout drops.

---

## 7. Dodge

Tap-vs-hold disambiguation on Space:

- Press edge: start `sSpaceHeldSeconds` timer.
- While held: increment timer. Once past `tunables.dodge_tap_window`
  (~0.20s), engage `sPlayer.sprinting = true`.
- Release: if held for less than the tap window AND no dodge has
  fired this press AND not already mid-dodge, fire a dodge.

Clip choice:

- WASD held at release: `falling_to_roll` (forward roll, ~3m authored
  travel).
- WASD not held: `standing_dodge_backward` (backstep).

`fireOneShotWithProfile(*dodgeClip, profiles::dodge(), 0.0f,
playback_rate)`. `sDodgeActive` flips true; gameplay-driven movement
yields to the clip's authored hip translation:

```cpp
if (sDodgeActive) {
    sPlayer.pos += rotateY(sSampler.consumedHipDelta(), sPlayer.yaw);
}
```

Mid-roll steering: if the dodge isn't a backstep and WASD intent
exists, the player's yaw blends toward the new direction at
`dodge_steer_rate` rad/s (~285°/s).

Post-dodge attack handoff: if the player presses LMB during the
dodge, it's buffered into `sPostDodgeAttack`. When dodge elapsed time
crosses `dodge_attack_cancel_fraction × duration` (default 0.65), the
buffered attack fires — turning a roll into a roll-attack combo
without the player having to time the press exactly at dodge end.

Dodge can't cancel an active attack one-shot; an attack can't cancel
an active dodge. (Both gates explicit at the press handlers.)

---

## 8. Block

Three entry paths:

1. **Buckler equipped**: RMB always blocks. Clip:
   `sword_and_shield_block`.
2. **Unarmed + Shift held**: RMB blocks. Clip: `unarmed_block`,
   `freeze_last = true` (no dedicated block-idle, so we hold the
   block-raise's last frame).
3. **Anything else**: RMB routes to the attack chain.

If pressed from `Peaceful` stance, the block fires through a
**first-action latch**: the SM transitions to combat-idle first,
waits `combat_entry_delay_seconds`, then fires the block raise (so
the loco track has had time to crossfade to the combat stance).
Profile: `blockFromLatch` (inertialization from canonical baseline).

If pressed from `CombatReady`, the block fires immediately. Profile:
`blockLive` (snap loco to t=0 first so the splice has a known handoff
frame).

While `sBlockingActive`, the loco track is overridden to
`sword_and_shield_block_idle` (for buckler — unarmed has no
block-idle clip, the `freeze_last` one-shot holds the pose). On RMB
release, `sBlockingActive` flips false, `releaseOneShot()` is called,
and the loco override drops — the SM picks combat-idle / walking /
running normally.

---

## 9. Tunables

`Tunables` is a plain struct in `include/Tunables.h` — about 30 floats
covering every "feel" knob: blend durations, turn rate, mouse
sensitivity, FOV, attack playback rate, dodge rates, sprint-finisher
parameters, inertialization decay scaling, etc.

Loaded from `config/tunables.json` at startup via
`selva::tuning::loadFromFile()`. Backed by
`NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT` so missing keys
fall back to struct defaults — adding a new tunable in C++ doesn't
require updating every saved JSON.

`selva::tuning::current()` returns the global instance; every
gameplay site reads it as `const auto& tun = selva::tuning::current()`.

The F1 panel is a live ImGui editor for every tunable. Sliders,
checkboxes for debug flags, and a Save button that writes back to
`config/tunables.json`. Useful for combat-feel iteration without a
recompile.

`time_scale` is a global multiplier applied to `dt` at the top of
`selvaPerFrame`. <1.0 slows everything (animation, gameplay) for
debug; the F1 panel ships with a slider.

---

## 10. Weapons and equipment

`include/combat/WeaponClass.h` defines the data model:

- **`WeaponClass`** — animation class. `id` (e.g. "sword", "buckler",
  "unarmed"), `one_handed` and `two_handed` `WeaponGripAnimSet`s,
  `WeaponAttach` (bone names + offset), `block_clip_start_seconds`,
  `attack_playback_rate` override.
- **`WeaponGripAnimSet`** — `light`, `heavy`, `running` vectors of
  `WeaponTechnique` (so a class can have multiple light chains, e.g.
  jab-hook-combo vs jab-jab-hook).
- **`WeaponTechnique`** — `id` + ordered `attacks` vector.
- **`WeaponAttack`** — `clip` name, `recovery_seconds`,
  `cancel_open_seconds` (-1 = auto-detect via motion-end scan),
  `chain_link_start_seconds` (-1 = auto-detect via motion-start
  scan), `chain_link_blend_seconds` (per-link blend override),
  `expected_button` ("LMB" / "RMB"), `motion_joints` (which joints to
  watch). The `resolved_*` fields are runtime caches populated at
  load time.
- **`WeaponAttach`** — bone names (`mixamorig:RightHand`, etc.) and
  offset translation/rotation for the future weapon-mesh attachment.

`WeaponClassRegistry::loadDirectory("config/weapon_classes/")` reads
every `*.json` and populates a name → class map.

Per-weapon stats (damage, poise, etc.) live in `Weapon` /
`WeaponRegistry` (`include/combat/Weapon.h` + corresponding `.cpp`).
A `Weapon` references a class by id — class pointer resolved at load
time.

`PlayerEquipment { left, right, grip }` is the player's current
loadout. `loadEquipment("config/loadout.json", weapon_registry)` at
startup. Today: both empty (unarmed) while sword/shield work is
parked; flipping to a sword/buckler loadout is a one-line edit to
`loadout.json`.

Three loaders, three configs:

- `config/tunables.json` — feel parameters
- `config/locomotion.json` — per-clip blend overrides
- `config/loadout.json` — equipped weapons + grip
- `config/weapon_classes/*.json` — anim chains per class
- `config/weapons/*.json` — per-weapon stat sheets

---

## 11. Rendering

`drawSkeletalMesh(mesh, model, view_proj, bone_palette, tint)` is the
one skinned-draw call. The shader is a simple grayscale pass that
multiplies a per-vertex shade by a uniform tint. Vertex layout:
position + normal + uv + bone_indices + bone_weights.

`bone_palette` is a `std::vector<glm::mat4>`, one matrix per bone,
ozz joint order, model-space. Built per-frame by `PoseSampler` from
the sampled local transforms: local → model via
`ozz::animation::LocalToModelJob`, then model-space matrices are
multiplied by inverse-bind matrices (baked into the mesh) to produce
the skinning matrices the GPU expects.

Camera is hand-rolled, not part of the engine yet:

- Yaw: mouse X delta × `mouse_sensitivity`.
- Pitch: mouse Y delta × `mouse_sensitivity`, clamped to `pitch_min`/`pitch_max`.
- Position: `player.pos + offset(yaw, pitch, follow_distance, follow_height)`.
- Look-at: a low-pass-filtered hip position (avoids per-frame jitter).
- Projection: 60° FOV, aspect from window size.

The world is decorative: a checkerboard floor (50×50), a 1m grid +
cardinal axes for orientation debug, and a tumbling cube at the
origin so direction-of-motion is visible. None of it is gameplay.

---

## 12. Debug tooling

The animation/combat pipeline is the most-iterated system; the debug
infrastructure reflects that.

- **`combat-debug.log`** — flat text log written to `build/bin/`
  every run. `combatLog()` wraps the file write and is a no-op when
  `sCombatDebugEnabled` is false. Captures: input edges, rhythm
  hits/misses, chain advancement, attack fires, splice diagnostics.
  Tagged lines (`[combat:fire]`, `[sm]`, `[fr]`, etc.) so it's
  greppable.
- **F1 panel** — ImGui overlay. Toggle on F1 rising edge (releases
  mouse). Sliders for every tunable, checkboxes for debug flags
  (combat-debug, frame-capture-armed-next-chain, debug-record-armed-
  next-dodge), Save button to `tunables.json`.
- **Splice diagnostics** — `SpliceDiag` captures world positions of
  RH, LH, hips, both feet just before a fire. After fire, samples the
  new clip at `start_seconds` and logs joint-by-joint distance
  deltas. Tells you visually how much pose mismatch the splice has
  to absorb.
- **Frame capture** — armed via F1 before a chain or dodge. Writes
  quarter-resolution PNGs per frame to `frame_capture/`. Use to build
  contact sheets visualizing chain transitions frame-by-frame
  (especially with `time_scale = 0.3` for slow-mo).
- **CSV bone recording** — armed via F1 before a dodge. Writes
  per-frame world-space bone positions to
  `debug_bones_game_dodge_*.csv`. Use to diff against the browser
  previewer's CSV (`scripts/preview_clip.py`) when an in-game motion
  doesn't match the asset.
- **Tracy zones** — `ZoneScopedN` markers at the per-frame, render,
  sampler-update, and frame-capture-write boundaries. Capture a trace
  via the Tracy server app and analyze with
  `scripts/analyze-trace.sh` to see where frame time goes.

---

## 13. Tests

Two Catch2 suites in `tests/`, run from `build/bin/` so relative
asset paths resolve:

- `skeletal_loader_test.cpp` — verifies the bake pipeline produced a
  loadable skeleton + clips. X Bot has ≥49 bones; sword idle and walk
  load with non-zero tracks and duration; missing files fail
  gracefully.
- `weapon_data_test.cpp` — verifies the JSON config loaders. Sword
  class has light/heavy chains with one-handed variants; cancel-open
  uses auto-detect (negative sentinel); Mixamo bone names survive
  round-trip; unarmed has two techniques each with `expected_button`
  per attack; chain A (LMB→RMB→LMB) vs chain B (LMB→LMB→RMB) are
  distinct; weapon registry resolves class pointers; loadout reads
  equipment and grip.

Both depend on the asset-bake targets, so `selva-oscura-tests`
implicitly requires `selva-oscura-convert-xbot` +
`selva-oscura-convert-attacks`.

Systems that need an SDL window or GL context are tested by running
the game itself; that's documented in the header comments of the
test files.

---

## 14. Coordinate conventions

- Y is up. Floor sits at Y = 0.
- Yaw rotates around Y. Yaw = 0 faces -Z (CCW positive looking down).
- Mixamo's bind pose has the character facing -Z, but the rig has a
  180° offset at the root — gameplay yaw composes the asset's bind-
  pose offset before the model matrix is built.
- `yawFromGroundDir(unit_xz)` = `atan2(-x, -z)` — standard inverse of
  `(sin(yaw), -, -cos(yaw))` forward-vector formulation.
- World position `sPlayer.pos` is XZ ground + Y from
  `mesh.foot_offset_y` so the character's lowest-bind-pose vertex
  sits at world Y = 0.

---

## 15. Cross-references

- Engine doctrine and shared utilities:
  `engines/engine/docs/ENGINE.md`.
- 3D pipeline plan (forward-looking, much of which is now built):
  `engines/engine/docs/3D-EXTENSION.md`.
- Tracy / profiling workflow: see CLAUDE.md "Profiling workflow"
  section under `games/selva-oscura/`.
- Prison-escape-game architectural patterns (some shared, some
  diverged): `games/prison-escape-game/docs/ENGINE.md`.

When making structural changes, update this doc in the same PR. When
adding a new tunable, bone name, weapon class, or transition profile,
the right place to document it is here — the source code's job is to
*be* the system, not to explain it.
