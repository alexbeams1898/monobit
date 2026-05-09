# Blender authoring scripts

Headless Blender tools for fixing asset-side animation issues. Each
script is invoked via `blender --background --python` so they don't
require opening the GUI.

## rebookend_clip.py

Re-poses the first N frames of a Mixamo FBX clip so it starts at a
specified pose from another clip — the AAA "bookend matching" technique
that makes chain-link transitions feel continuous instead of jerky.

### Why we need it

AAA action-game animators author each chain swing's END pose to land on
the NEXT swing's anticipation pose, so blends at the splice are nearly
invisible. Mixamo packs ship clips that each return to a generic
combat-stance neutral instead — so consecutive Mixamo swings have
mismatched bookend poses, producing a visible jerk at the splice (see
combat-debug.log for the per-splice telemetry that proves this).

This script fixes the mismatch at the asset level by overwriting the
target clip's first N frames with a smooth blend from the source's
pose-at-time → target's authored pose at frame N.

### Usage

From the repo root (so relative paths resolve):

```bash
games/selva-oscura/scripts/blender/rebookend.sh \
    --source "games/selva-oscura/assets/characters/x_bot/source/Pro Sword and Shield Pack/sword and shield slash.fbx" \
    --source-time 0.683 \
    --target "games/selva-oscura/assets/characters/x_bot/source/Pro Sword and Shield Pack/sword and shield slash (3).fbx" \
    --target-blend-frames 8 \
    --output "games/selva-oscura/assets/characters/x_bot/source/Pro Sword and Shield Pack Reposed/sword and shield slash (3).fbx"
```

### Wiring the output back into the build

The output FBX needs to land in a directory that CMake scans for clips.
Two options:

1. **Replace in-place.** Output to the same path as the source, with
   the original moved to `*_original.fbx`. The existing pipeline picks
   it up unchanged. Risk: easy to forget which file is rebookended.

2. **Reposed/ subdir + CMake update.** Output to a parallel
   `Pro Sword and Shield Pack Reposed/` directory, add it to
   `mixamo_pack_dirs` in `games/selva-oscura/CMakeLists.txt`. Both
   versions exist; either can be referenced by clip name (with a
   suffix). Cleaner for iteration.

Recommended: (2) until the workflow stabilizes, (1) once we trust the
process.

### Args

| Flag | Required | Meaning |
|---|---|---|
| `--source <path>` | yes | Source FBX (the *previous* clip in the chain) |
| `--source-time <seconds>` | yes | Time in source clip whose pose becomes target's new bookend start. Usually source's `cancel_open_seconds` from combat-debug.log. |
| `--target <path>` | yes | Target FBX (the *next* clip in the chain) — gets edited |
| `--target-blend-frames <N>` | default 8 | How many frames at the start of target to overwrite with blended poses. ~8 frames at 30fps ≈ 0.27s — enough to ease in without erasing the swing's authored windup. |
| `--output <path>` | yes | Output FBX path. Parent dir is created if missing. |

### Picking source-time

Look in `build/bin/combat-debug.log` after a run. For each chain entry,
the resolver logs `cancel_open=N.NNNs (NN%)`. That's the source-time
for re-bookending the *next* chain entry's first frames.

Example: to fix `slash → slash_3`, use slash's `cancel_open` value
(0.683s in our current data) as `--source-time`.

### Picking target-blend-frames

Trade-off:
- **Too few (1-3)**: bookend is too abrupt, looks identical to no fix.
- **Too many (>15)**: erases the swing's authored windup phase; the
  swing reads as starting late or feeling weak.
- **Sweet spot**: 6-10 frames at 30fps. Default 8 is a good first try.

If verifying via `combat-debug.log` shows `decay_dist` is still > 0.3m
after a rebookend, increase the blend-frames count (more time for the
arm to migrate to its authored pose). If the swing looks "weak" or
visibly lacks windup, decrease.
