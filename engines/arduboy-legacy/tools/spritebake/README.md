# spritebake

1-bit sprite baker for the mono engine. Converts source artwork into
PROGMEM-packed byte arrays and patches them into per-game sprite tables.

Designed as a long-lived engine component, reusable across future
monobit titles.

## Why

Earlier we had four or five one-off `_bake_*.py` / `_preview_*.py` scripts
each reimplementing Otsu, morphology, autocrop, slicing, and cpp-patching.
This consolidates that into one declarative tool:

- **Manifest-driven** — sprite list + pipeline choices live in
  `sprites.toml`, not code.
- **Pipeline engine** — composable named stages (autocrop, gaussian_blur,
  threshold_otsu, morph_close_holes, etc.). Swap them from the manifest.
- **Safe cpp patcher** — only replaces matching `const u8 NAME_data[N]
  PROGMEM = { ... };` blocks. Never invents new declarations; never
  touches unrelated text. Roundtrip-verifies every write.
- **Testable** — `tools/spritebake/tests/` covers the dangerous parts:
  encoder bit-layout, cpp patching, pipeline determinism.

## Install

Python 3.11+ (for stdlib `tomllib`). Required packages: `pillow`, `numpy`.

```bash
pip install pillow numpy
# On Python 3.10 or older, also: pip install tomli
```

The tool runs as `python -m tools.spritebake` from the repo root.

## Quick start

```bash
# List every registered pipeline stage
python -m tools.spritebake list-stages

# Preview all RPG sprites through their manifest pipelines
python -m tools.spritebake -m games/rpg/sprites.toml preview --open

# Compare two pipelines on every sprite
python -m tools.spritebake -m games/rpg/sprites.toml preview \
    -p default_boss -p default_lucifer --open

# Diff freshly-baked output vs committed sprites.cpp
python -m tools.spritebake -m games/rpg/sprites.toml diff

# Actually rewrite sprites.cpp with baked bytes
python -m tools.spritebake -m games/rpg/sprites.toml bake

# Verify encode/decode roundtrip for every NAME_data currently in the file
python -m tools.spritebake -m games/rpg/sprites.toml roundtrip
```

## Manifest format

See [`games/rpg/sprites.toml`](../../games/rpg/sprites.toml) for a
worked example. Minimal:

```toml
[output]
cpp = "games/rpg/sprites.cpp"
layout = "col-major-topbit0"
progmem = true

[pipelines.default]
stages = [
  "autocrop",
  { gaussian_blur = { radius_ratio = 0.5 } },
  { aspect_fit = { width = "$width", height = "$height" } },
  { downscale = { width = "$width", height = "$height", method = "lanczos" } },
  "threshold_otsu",
  "morph_close_holes",
]

[[sprites]]
ident = "my_sprite_data"
source = "art/my_sprite.png"
width = 16
height = 16
pipeline = "default"
```

### Stage parameter placeholders

Use `"$width"` and `"$height"` anywhere in stage kwargs — the baker
substitutes each sprite's target dimensions at bind time. This lets one
pipeline work for sprites of different sizes.

### Slicing spritesheets

For sprites cut from a grid:

```toml
[[sprites]]
ident = "boss_charon_data"
source = "art/bosses.png"
width = 16
height = 16
pipeline = "default_boss"
slice = { cols = 4, rows = 2, index = 0, trim_top_ratio = 0.22 }
```

`index` is row-major (0 = top-left). `trim_*_ratio` strips that fraction
off each side before the cell enters the pipeline — useful when the
cell contains label text above the figure.

### Per-sprite inline pipelines

To override the shared pipeline for a single sprite:

```toml
[[sprites]]
ident = "special_data"
source = "art/special.png"
width = 8
height = 8
stages = [
  "autocrop",
  { downscale = { width = "$width", height = "$height" } },
  "floyd_steinberg",
]
```

## Pipeline stages

Every stage is a pure function `SpriteData -> SpriteData`. Stages may
pass a float "gray image" and "edges" mask through `sprite.meta` so
downstream stages can see the pre-threshold picture.

See `python -m tools.spritebake list-stages` for the current registry
with one-line descriptions. Full categories:

- **`stages/crop.py`** — autocrop, aspect_fit, pad, downscale, stretch,
  resize_to_target
- **`stages/filter.py`** — gaussian_blur, sobel_edges, dilate, erode
- **`stages/threshold.py`** — threshold_fixed, threshold_otsu,
  threshold_percentile, threshold_edge_biased,
  threshold_distance_weighted
- **`stages/morph.py`** — morph_open_speckle, morph_close_holes,
  vert_feature_boost, connected_component_filter, majority_smooth
- **`stages/dither.py`** — floyd_steinberg, edge_preserving_dither,
  bayer_ordered_dither
- **`stages/silhouette.py`** — outline_and_fill, detect_thin_features,
  reinforce_thin_features, silhouette_preserve

## Writing a new stage

1. Add the function to the appropriate `stages/*.py`.
2. Decorate with `@register_stage("your_name")`.
3. Signature: `def your_name(sprite: SpriteData, **kwargs) -> SpriteData:`.
4. If operating on the float gray image, use `sprite.meta["gray"]` if
   available and re-attach via a new SpriteData.
5. Use `sprite.with_pixels(new_mask)` for binary-only mutations.

## Editor integration

`tools/sprite_editor/index.html` is a standalone browser-based pixel
editor. It round-trips with spritebake via the **URL hash seed**:

```
file:///.../index.html#w=20&h=24&name=boss_lucifer_data&layout=col-major-topbit0&import=0x00,0x01,...
```

So a baking workflow is:

1. `python -m tools.spritebake ... preview` — eyeball the current bake.
2. Spot a problem; open the editor seeded with those bytes, hand-clean.
3. Copy the exported C array and paste back to spritebake, which patches
   `sprites.cpp`.

## Tests

```bash
python -m unittest discover tools/spritebake/tests -v
```

Three test modules:
- `test_encode.py` — every layout roundtrips; teardrop sprite matches
  the exact committed bytes.
- `test_patch.py` — cpp patching is idempotent, preserves structure,
  never touches unrelated text. **Creates its own tmp files — never
  reads or writes real project sprites.**
- `test_pipeline.py` — every documented stage is registered; pipelines
  are deterministic; stage contracts hold.

## Layout: why column-major bit-0-top?

Matches the SSD1306's page-major video memory. A sprite's byte can be
streamed into framebuffer memory without per-pixel shifting in the
renderer's inner loop — `draw_sprite` scans a byte at a time. Other
layouts exist for interop (fonts, bitmap imports) but the engine's own
framebuffer format is this one.
