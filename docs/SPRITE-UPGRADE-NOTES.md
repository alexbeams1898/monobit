# Issue #81 — Sprite Upgrade & Character Customization Architecture

## Context for Claude Code

This document captures design decisions and research for the sprite size upgrade and
character customization system. Read this alongside CLAUDE.md and PLAN.md before
implementing anything. These are settled decisions unless noted otherwise.

---

## Decision: 64x64 source frames on 32px tile grid

**Settled.** Sprites upgrade from 32x32 to 64x64. Tile grid stays 32px. This is Option A
from our analysis — sprites overflow their tile boundary, which is fine because:

- Y-sort already handles overlapping sprites (RenderSystem sorts by layer > foot Y > sub_layer)
- Colliders stay gameplay-driven (~32x32), independent of sprite size
- RenderSystem already supports arbitrary sprite dimensions
- This is the standard approach used by CrossCode (~42x62 frames), Moonlighter (~48-64px),
  and Graveyard Keeper (~64px+)

**Why 64x64 square and not portrait (e.g. 48x64):** The game has full character customization
with mix-and-match body parts. Square frames make the compositing pipeline uniform — every
layer renders onto the same 64x64 canvas with a consistent anchor point. The extra horizontal
space on idle frames is free (transparent pixels cost nothing) and pays for itself during wide
weapon swings and attack animations.

**What changes:**

- `assemble_spritesheet.py` — handle 64x64 source frames
- All animation sidecar JSONs in `config/animations/` — update frame_width/frame_height
- All entity JSONs with sprite blocks — update src_w/src_h
- Player and enemy sprite sheets — re-render/re-draw at 64x64
- Camera zoom may need adjustment so the world doesn't feel cramped
- Flow field, collision, map gen — **no changes needed**

**What does NOT change:**

- Tile size (32px)
- Collider sizes (gameplay-driven, still ~32x32)
- Flow field cell size (16px)
- Corridor widths, door sizes, room dimensions
- Any rendering architecture

---

## Decision: Layered sprite compositing for character customization

**Design vision:** Minecraft's modularity (swap any piece freely) × Straftat's top-down
pixel art equipment visibility × Elden Ring's depth of customization options (every slot
matters, builds feel distinct).

### Architecture: runtime layered compositing

Each equipment/appearance slot is a **separate sprite sheet** with an identical frame layout
(same 64x64 grid, same animation states, same direction order). At render time, layers are
drawn bottom-to-top. This is the same pattern used by:

- **LPC (Liberated Pixel Cup)** — the gold standard for modular 2D characters. Every asset
  follows a strict sheet layout. The Universal Character Generator stacks dozens of layers
  on the same frame grid. LPC already has 64x64 community assets.
- **Stardew Valley** — base body tinted by skin color, hair/shirt/pants as separate layers
  with palette swapping.

### Layer stack (draw order, bottom to top)

```
0. Shadow / ground effect
1. Base body (skin tone via palette swap)
2. Pants / leg armor
3. Boots
4. Chest / shirt / chest armor
5. Hair (behind, for long hair that goes behind shoulders)
6. Head / face
7. Hair (front)
8. Helmet / hat
9. Back arm + weapon/shield (when facing away from camera)
10. Front arm + weapon/shield (when facing toward camera)
11. Cape / back accessories (behind body for front-facing, in front for back-facing)
```

The exact layer count and order is a starting point — it will evolve during implementation.

### Z-depth direction flipping

**Critical gotcha from research:** When a character faces left vs. right, weapon/shield draw
order must flip. A sword drawn in front when facing right should be behind when facing left.

**Approach:** Per-direction draw order on each layer. The `BodyPart` component (or its
replacement) needs a draw order that can vary based on the entity's current facing direction.
Alternatively, separate "front arm" and "back arm" layers that swap visibility/order based on
facing.

### How this extends the existing system

The split-body rendering system already exists:

- Player rendered as child entities (lower body, upper body) linked via `BodyPart` component
- `BodyPart` has `parent` (entt::entity), `faces_aim` (bool), and draw_order
- Position sync copies parent transform to children
- `DeathSystem` cascades destruction to children

**The extension is natural:** Instead of 2 `BodyPart` children, the player has N children —
one per visible layer. Each gets:

- `layer_slot` — which equipment/appearance slot this represents (body, hair, helmet, etc.)
- `z_order` — draw priority, potentially varying per facing direction
- `faces_aim` — whether this layer tracks mouse aim (upper body layers) or velocity (lower body)

`EquipmentSystem` swaps the texture on the relevant child entity when gear changes. Unequipping
a slot either hides the child or swaps to a default/naked texture.

### Palette swapping (high value, implement early)

Tinting a base sprite with different colors gives massive variety for minimal art cost:

- Skin tone — tint the base body layer
- Hair color — tint the hair layer
- Armor material — tint armor layers (bone = white, iron = grey, demon = red, etc.)

This can be done via shader uniform (multiply vertex color) or by having a small set of
pre-colored variants. Shader approach is more flexible and uses less memory.

### Shared frame grid contract

**All layers MUST share:**

- 64x64 frame size
- Same animation state rows (Idle, Walk, Attack, Hit, Death)
- Same direction column order (South=0, West=1, East=2, North=3)
- Same frame count per state
- Same anchor point (center-bottom or center-center — pick one, enforce everywhere)

This is the non-negotiable contract that makes mix-and-match work. If a helmet sheet has
different frame timing than a body sheet, the layers desync and it looks broken.

### Modding implication

This architecture is inherently mod-friendly. A modder adds a new armor piece by:

1. Drawing a 64x64 sprite sheet following the frame grid contract
2. Adding a JSON config pointing to the sheet and specifying the layer_slot
3. Done — the engine composites it automatically

This aligns with the engine's config-driven philosophy from CLAUDE.md.

---

## Open questions (not yet decided)

- **Exactly which customization options exist at character creation vs. found in-game?**
  Body type, skin tone, hair style, hair color are likely creation-time. Equipment is in-game.
  Face/head details TBD.
- **How many body type variants?** Minecraft has one base. Elden Ring has sliders. This game
  is pixel art so full sliders don't make sense — probably a small set of base body types
  (2-4) that the player picks.
- **Cape / back layer behavior:** Does it animate independently (cloth sim lite) or just
  follow the body animation? Probably follows for now, independent later.
- **How does the character creator UI work?** Paper doll preview with cycling through options
  per slot? Live preview on a walking animation? TBD.
- **Weapon visibility during gameplay:** Is the weapon always drawn as part of the character
  sprite layers, or only during attack animations? Always-visible is the goal but may need
  idle weapon poses added to every weapon sheet.

---

## Reference games

| Game | Sprite Size | Customization Approach | Relevant Lesson |
|------|-------------|----------------------|-----------------|
| CrossCode | 42x62 | Predefined character, no customization | Frame size reference for top-down action |
| Moonlighter | ~48-64px | Equipment sets (not per-slot) | Visual style reference |
| Graveyard Keeper | ~64px+ | Predefined character | Large sprite detail reference |
| Stardew Valley | 16x32 | Layered (body + hair + shirt + pants), palette swap | Compositing architecture reference |
| LPC system | 64x64 | Full modular layering, community standard | Direct technical reference — same frame size |
| Minecraft | 64x64 skin | Full body texture, community skins | Modularity and modding culture |
| Elden Ring | 3D mesh | Deep sliders, every slot visible | Depth-of-customization aspiration |

---

## Implementation order suggestion (not yet committed)

1. **Upgrade frame size to 64x64** — pipeline change only (assemble_spritesheet.py, JSONs,
   camera adjustment). Get the game running with 64x64 frames before touching customization.
2. **Extend BodyPart to N layers** — generalize the 2-child split-body system to support
   arbitrary layer count with per-slot draw order.
3. **Palette swap shader** — add vertex color tinting to RenderSystem so layers can be
   recolored without separate textures.
4. **Character creation screen** — basic slot selection UI (body type, skin, hair, colors).
5. **Equipment-to-layer binding** — EquipmentSystem drives layer texture swaps when gear
   changes.
6. **Weapon visibility** — weapon layer drawn during all states (idle pose + attack anim).
7. **Content** — draw the actual layer sheets for each equipment piece.

---

## Implementation status (shipped on `minor/81-sprite-visual-upgrade`)

This section is the ground-truth log of what actually shipped. The design section above
captures the original vision; anything that diverged is called out below.

### What shipped

- **Frame size** upgraded from 32x32 to 64x64. Tile grid still 32px. Camera zoom adjusted.
  All animation sidecar JSONs and entity sprite blocks updated.
- **LPC asset pipeline** with three stages:
  1. `game/scripts/fetch_lpc.py` -- pulls per-layer, per-animation PNGs from the upstream
     LiberatedPixelCup repo into `game/assets/sprites/lpc/raw/`. Idempotent and scoped:
     only fetches the styles/colors the game actually uses (`HAIR_STYLES`, `FACIAL_STYLES`,
     `HEADWEAR_STYLES`, `FEET_COLORS`, `EYE_COLORS`, etc.).
  2. `game/scripts/bake_palettes.py` -- reads palette definition JSONs from upstream
     (`game/assets/sprites/lpc/raw/_palettes/`) and bakes per-palette color variants of the
     grayscale master PNGs. Drives skin tone + clothing color without touching the art by
     hand. 12 skin tones including 5 non-human (Goblin, Swamp, Demonic, Wraith, Undead).
  3. `game/scripts/assemble_spritesheet.py` -- stitches per-animation PNGs into the
     2048x384 character sheet layout the engine expects (Idle, Walk, Attack, Hit, Death,
     Run rows; one row per state; direction blocks per frame).
- **SpriteCompositor** (`engine/include/SpriteCompositor.h`, `.cpp`) -- generic engine-side
  paper-doll compositor. Takes a vector of layer PNG paths, alpha-blends them CPU-side into
  a single GL texture, caches by joined-path key. Not thread-safe (GL-thread only).
- **AppearanceConfig** (`game/include/ecs/AppearanceConfig.h`) -- in-registry singleton that
  holds the layer manifest loaded from `game/config/appearance/layers.json`. Categories:
  - `Select` categories contribute one sprite layer. Special flags:
    - `linked_to` copies another category's option id (e.g. `head` linked to `body_color`).
    - `combine_with` builds the file name by joining two selections
      (`hair_color` combine_with `hair_style` -> `long_black.png`).
  - `Slider` categories don't contribute a layer (currently only `size`, which writes
    `Transform.scale`).
  - Each `AppearanceOption` can carry a `swatch` (packed RGBA) for the palette-grid UI.
- **AppearanceOps** (`game/include/ops/AppearanceOps.h`, `.cpp`) -- the bridge between the
  config and the compositor.
  - `buildLayerPaths(em, selections)` -- resolves the category list into final layer paths,
    honoring `linked_to` and `combine_with`.
  - `resolveAppearance(em, entity, overrides)` -- pulls the pending `AppearanceDef` from an
    entity, composites via SpriteCompositor, writes `Sprite.texture_id`, removes the def.
    Also sets `src_w`/`src_h` from `Animation` when present.
  - `applyAppearanceScale(em, entity, selections)` -- resolves the `size` slider to
    `Transform.scale`.
- **CharCreateScreen** (`game/include/screens/CharCreateScreen.h`, `.cpp`) -- reads the
  layer manifest and renders cycling selectors, swatch palette grids (for colors with
  `swatch` set), and sliders. Lives preview composites in real time. Two-column layout
  with measured label/value spacing; no hardcoded panel sizes.
- **Palette-swatch color pickers** -- the "Facial Hair Color", "Hair Color", "Skin Color",
  "Eye Color", "Shoes Color", and "Hat Color" rows render a grid of colored squares instead
  of the `< Black >` text cycler. Clicking a square selects that color; keyboard still
  cycles. Swatch hex values live in `layers.json` next to each option.
- **Cop enemy** -- fully paper-dolled using the same AppearanceDef flow as the player, with
  the LPC bowler hat as its signature headwear. Cop appearance is pre-resolved (layers are
  literal file paths, no selection UI).
- **Skeleton enemy** -- still uses a single hand-composited LPC skeleton sheet (no
  SpriteCompositor), kept as the baseline for entities that don't need customization.
- **Dead -> Hit animation fix** -- `AnimStateSystem` routes `Dead` to the `Hit` row (hurt
  pose reads as a death reaction with the current LPC animation set). `AnimationSystem`
  now freezes the last frame for both `Death` and `Hit` so the pose doesn't loop. The
  `Dead.timer` emplaced by `DamageSystem` is sized from the `Hit` row duration to match.

### What diverged from the original plan

- **No split lower/upper body.** The original issue #81 plan extended the existing
  `BodyPart` two-child split to N child layers. That approach shipped and was then replaced
  with a single-entity paper-doll: SpriteCompositor composites every layer into one texture
  at character-creation / equip time. The player is one entity, one Sprite, one Animation.
  The old BodyPart component and its system are deleted.
  - **Why:** N child entities meant N animation ticks, N state-machine runs, and N render
    draws per character per frame. Compositing once at equip time collapses all of that to
    a single sprite draw, and there are no position-sync seams between layers. It also
    removes the "upper body faces mouse, lower body faces movement" constraint, which was
    never going to survive weapon visibility work.
  - **Trade-off:** The torso doesn't rotate independently of the legs anymore. Acceptable:
    LPC has no torso-twist frames anyway, and the original split only rotated head +
    shoulders. WASD-locked facing (docs/CLAUDE.md) gives the movement/aim feel that the
    split was supposed to provide.
- **No palette-swap shader.** Palette swapping happens offline in `bake_palettes.py` rather
  than at runtime. Runtime uses plain textures; no shader uniforms for color. Simpler and
  more portable (still "runs on a calculator"). Can revisit if we need live tint for
  status effects beyond the existing `TintOverride` modulation.
- **Equipment-to-layer binding not wired.** `EquipmentSystem` does not currently rebuild
  the composited sheet on equip change. That's the follow-up: on equip/unequip, rebuild
  `AppearanceDef.layers` and call `resolveAppearance` again to swap the composited texture.
  The compositor cache keys on the joined path list, so repeated equip swaps reuse the
  same GL texture.
- **Weapon visibility not implemented.** Held weapon sprites are a separate follow-up
  (issue #92). The layer slot for a weapon already fits the compositor; only the art + JSON
  entries are missing.

### Layers actually in `layers.json` today

In draw order (bottom first):

1. `body_color` (skin tone, 12 options including non-human)
2. `body_shape` (male for now)
3. `feet_style` (shoes / boots) + `feet_color` combine_with
4. `legs_style` (pants / shorts)
5. `torso_style` (shortsleeve / longsleeve)
6. `hair_style` + `hair_color` combine_with
7. `head_type` (5 face variants, linked_to body_color for skin match)
8. `eye_color`
9. `facial_style` + `facial_color` combine_with
10. `headwear_style` + `headwear_color` combine_with
11. `size` (slider, no layer contribution)

### Build pipeline recap

```
fetch_lpc.py               -- pulls ~800 PNGs from upstream
bake_palettes.py           -- bakes ~500 palette-swapped variants
assemble_spritesheet.py    -- ~500 final sheets under assets/sprites/lpc/assembled/
CMake sync                 -- copies assembled sheets + layers.json to build/bin/
SpriteCompositor (runtime) -- composites final per-character textures on demand
```

### Attribution

`game/assets/sprites/lpc/LICENSE/CREDITS.csv` is the authoritative per-file attribution from
upstream, vendored verbatim. `game/assets/sprites/lpc/LICENSE/LICENSE-upstream.txt` is the
upstream project's LICENSE (GPL 3.0 copy). Top-level `CREDITS.md` summarizes and points
into these files.
