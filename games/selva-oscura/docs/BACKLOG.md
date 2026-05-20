# Selva Oscura — Backlog

Known imperfections that are **deferred deliberately**: we know they exist,
we know how to fix them, and the fix is gated on a milestone that hasn't
landed yet. This is *not* tech-debt-as-bug-list — items here are
architectural waits, not papered-over flaws.

When a milestone lands that addresses an item, **delete the item** rather
than crossing it out. This doc is a current picture of what's pending,
not a history.

Each entry: **Symptom**, **Why**, **Fix path**, **Resolves when**.
Code that knowingly produces these symptoms links here by exact entry
title in a `// see docs/BACKLOG.md "<title>"` comment so a future reader
can match the visible behavior to its rationale fast.

---

## Roll through floor

**Symptom:** Pressing Space to dodge causes the soldier's body to clip
through the floor as the procedural pitch curve tumbles him forward.

**Why:** No physics. The dodge is a procedural Y arc + pitch rotation,
both authored in code without any awareness of world geometry. When pitch
swings the body, head/feet sweep below `y = 0` for fractions of a second.

**Fix path:** Three pieces converge to resolve this:
1. **Jolt physics integration** — capsule character collider against a
   floor collision shape. Y position can never go below the foot plant.
2. **Skeletal roll clip** replaces the procedural pitch curve. The clip
   is animator-authored and has no body-through-floor pitch.
3. **Animation root motion** drives the dodge translation directly from
   the clip's hip-bone track, so visual and gameplay positions match.

**Resolves when:** Jolt physics integration milestone (3D-EXTENSION.md
milestone 3) plus the skeletal dodge clip milestone (3D-EXTENSION.md
milestone 4-5).

---

## First-frame pop / init flash

**Symptom:** The very first frame after the game launches shows broken
geometry (T-pose, untransformed cube, default camera position) which
snaps into the correct frame ~16ms later.

**Why:** Sampler / interpolation state is uninitialized on frame 0:
- Bone palette is zeros until the first `sample()` call.
- Camera previous-position is zero until the first tick runs.
- Player previous-position is zero until the first tick runs.

The pre-warm in `initSkeletalAssets()` (sample the Idle clip at t=0
before any rendering) addresses the bone-palette case. The camera and
player prev-state cases still produce a one-frame lerp pop.

**Fix path:** Two layers, can land independently:
1. **Frame 0 prev-state fix**: explicitly set every interpolated entity's
   `prev_*` fields to match its current state during init. Eliminates the
   one-frame lerp from origin to first real position.
2. **Loading sequence + fade-in**: standard production polish — game
   launches into a black screen that fades to gameplay over ~250ms,
   hiding any composition issues entirely. Industry-standard for shipped
   games (Souls, Elden Ring, virtually every console title). Premature
   until there's an actual loading sequence to fade in from.

**Resolves when:** A small (1) lands as a low-effort polish pass; (2)
lands as part of the eventual title-screen / loading-flow milestone.

---

## Combo finisher recovery feels "stalled"

**Symptom:** After firing the unarmed combo finisher (LMB→RMB→LMB =
jab→hook→combo), there is ~0.5–1.0s of recovery time during which the
character looks idle and the player can't fire another attack until
the clip's BlendOut completes.

**Why:** The `combo.ozz` clip authored by Mixamo includes a long post-
swing recovery tail (a "waddle" stand-up after the punch lands). The
combat system enforces "attacks play to full duration" — presses
during the recovery buffer until the clip ends, instead of cancelling
into a fresh chain. After a finisher there is no chain step to
advance into, so `is_chain_advance=false`, so the press buffers.

We tried two fixes and reverted both:
1. **Per-attack `blend_out_seconds` override (0.5–0.8s)** — trims the
   visible recovery by fading out earlier into the loco track. Worked
   but felt like cutting off the swing.
2. **Cancel-into-cold-strike when chain is empty** — allowed any
   press during a finisher's cancel window to fire fresh
   immediately. Worked but added a special-case branch to the press
   handler that complicates the "play to full duration" rule.

**Fix path:** Re-bake `combo.ozz` in Blender to trim the waddle tail
so the authored clip ends close to the upright stance the BlendOut is
fading toward. Same pattern as other Mixamo clips that ping-pong or
have authored drift — out of code, into asset prep.

**Resolves when:** Asset-prep pass on combat clips (TBD milestone).

---
