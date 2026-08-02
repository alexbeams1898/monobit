# Wayworn Hush — Sprite Pipeline (color, GBA-authentic)

> **Owns:** the art-authoring pipeline — how hand-drawn sprites become
> engine-ready, GBA-palette-authentic sprite sheets. A batch tool, not an editor:
> you draw/animate in Aseprite; the tool enforces the GBA palette discipline and
> assembles engine-format sheets.
>
> **Status:** design (locked decisions below), pre-build. Lives at
> `games/wayworn-hush/tools/spritetool/`.

## tldr

Draw in Aseprite → export frames/strips → the tool **quantizes each sprite to a
≤16-color RGB555 palette** (the two GBA hardware truths: valid GBA colors + the
16-color cap) → **assembles** frames into the engine's sprite-sheet layout →
**emits** a PNG atlas + the animation manifest JSON the game already consumes.
Ported from the archived Arduboy `spritebake` tool's proven bones (stage-registry
pipeline + TOML manifest), with a color RGBA core replacing its 1-bit spine.

## Why this exists

Aseprite lets you draw anything; it does **not** enforce that a sprite reads as
authentic GBA-era pixel art. Two hardware constraints define that look, and the
tool enforces exactly them:

1. **RGB555 color** — the GBA renders 15-bit color (5 bits/channel, 32 levels
   each). Modern 24-bit colors must snap to this grid or they read as "too clean"
   / not-period. Emerald's palettes live in RGB555.
2. **≤16 colors per sprite** — GBA 4bpp sprites use a 16-color palette (incl.
   transparent). Emerald characters are ~15 usable colors.

The "Emerald feel" comes from **your color choices in Aseprite**; the tool
enforces the **hardware grid + the cap**. No fixed master palette — each sprite
gets its own optimal ≤16-color RGB555 palette, exactly as GBA sprites worked.

## Locked decisions

- **Location:** `games/wayworn-hush/tools/spritetool/` (game-specific; promote to
  shared if a second 2D game needs it).
- **Not an editor:** Aseprite is the drawing tool. This is a batch pipeline. (The
  Arduboy tool's browser editor is NOT ported — it plays to Aseprite's strength
  instead, and the editor's pixel core was the biggest rewrite risk.)
- **Palette model:** per-sprite ≤16-color, every color snapped to RGB555. No
  master palette; hardware-authentic per-sprite palettes.
- **Language:** Python (matches the Arduboy tool + all repo asset scripts; Pillow
  + numpy).

## Pipeline architecture (harvested bones)

From the Arduboy `spritebake` assessment, the **depth-independent** design ports
directly:

- **Stage registry** — `@register_stage("name")` adds a transform to a global
  dict; the manifest names stages declaratively; the pipeline looks them up and
  chains them. Clean, testable, extensible.
- **Declarative TOML manifest** — named reusable pipelines + per-sprite entries
  with `$width`/`$height` binding.
- **Core type** — REWRITTEN: the Arduboy `SpriteData` is a boolean mask; ours is
  an **RGBA `uint8` array** `(H, W, 4)`. Every stage is `SpriteData → SpriteData`.

## The stages

Color-agnostic geometry stages port from the Arduboy tool (logic survives, signature
widens to RGBA): `autocrop` (alpha bbox), `pad`, `resize`, `downscale`.

New/rewritten stages (the value-add):

### `rgb555_snap`
Snap every pixel's RGB to the nearest RGB555 grid point: each channel
`round(c / 255 * 31) / 31 * 255` (0–255 → one of 32 levels → back to 0–255 for
storage/display). Alpha is left binary (0 or 255) — GBA sprites have 1-bit
transparency, no partial alpha. This is the hardware-color-grid enforcement.

### `palette_reduce`
Reduce the sprite to ≤16 colors (15 + transparent):
1. Snap to RGB555 first (so reduction happens in valid GBA color space).
2. Count distinct opaque colors. If ≤15, done.
3. If >15, cluster to 15 (median-cut or k-means over the RGB555 points) and remap
   each pixel to its nearest cluster color.
4. Transparent pixels (alpha 0) are the 16th "color" — never counted against the
   15.
Output carries the resulting palette (≤16 colors) in `meta['palette']` for the
emitter + for authoring feedback ("this sprite used N colors").

### `assemble` (sheet layout)
Lay per-direction/per-state frames into the engine's sprite-sheet layout —
`column = dir_index * max_frames_per_state + frame_index`, rows = states — exactly
what `AnimationSystem` consumes (matches the existing hand-rolled `player_walk.png`
assembler). Frame size, direction order (S/W/E/N), and per-state frame counts come
from the manifest.

## Output (replaces the Arduboy PROGMEM back end)

- **PNG atlas** — the assembled sheet, RGBA, quantized. Drops straight into
  `assets/sprites/`.
- **Animation manifest JSON** — the `{ texture, frame_width, frame_height,
  direction_count, max_frames_per_state, states: { name: {row, frames, duration} } }`
  shape the game's `PlayerConfig`/animation loader already reads. The tool emits
  it so authoring a new animated thing = draw + run tool, no C++.
- **(Optional) palette report** — per-sprite color count + palette swatch, so you
  can see whether a sprite is within budget.

The Arduboy `sprites.cpp`/PROGMEM/FX-`.bin` emitter and `patch.py` are NOT
ported — full-color engine emits PNG + JSON, not packed C arrays.

## Manifest format (ported, color-adapted)

```toml
[output]
atlas_dir = "assets/sprites"        # where PNG atlases land
config_dir = "config/animations"    # where manifest JSON lands

[pipelines.character]                # named, reusable stage chain
stages = [
  "autocrop",
  { resize = { width = "$width", height = "$height" } },
  "rgb555_snap",
  { palette_reduce = { max_colors = 16 } },
]

[[sprites]]
ident = "player"
source = "art/player/"              # dir of per-state strips (Aseprite export)
width = 32
height = 64
pipeline = "character"
direction_count = 4
states = [
  { name = "idle", frames = 1, duration = 0.0 },
  { name = "walk", frames = 4, duration = 0.14 },
]
```

## Build order

1. **Scaffold** — manifest loader (TOML), stage registry, RGBA `SpriteData` core.
   (Port the Arduboy structure; swap the type.)
2. **`rgb555_snap` + `palette_reduce`** — the palette discipline, with unit tests
   (deterministic pure functions: a known input → known snapped/reduced output).
3. **`assemble`** — frames → engine sheet layout (reuses the layout math already
   proven in the hand-rolled walk-sheet assembler).
4. **Output** — PNG atlas + animation manifest JSON.
5. **Prove it** — re-run the existing `player_walk.png` through the tool; confirm
   it produces an equivalent (now palette-quantized) sheet + a valid manifest the
   game loads.

## Cross-references

- Arduboy tool the bones are ported from: `engines/arduboy-legacy/tools/spritebake/`
  (stage registry, manifest, animation-orchestration design).
- [SCALE.md](SCALE.md) — 32×64 sprite, GBA/Emerald register, the numbers.
- The engine `AnimationSystem` sheet layout this emits for
  (`engines/engine/docs/ENGINE.md` "Animation system").
- The game's animation manifest consumer (`config/player.json` +
  `PlayerConfig`), whose shape the tool's JSON output matches.
