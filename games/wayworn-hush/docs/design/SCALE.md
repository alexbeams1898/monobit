# Wayworn Hush — Scale & Sizing

> **Owns:** the foundational numbers — tile size, sprite size, render target,
> camera field, movement model, map/chunk sizing. These are load-bearing: they
> can't change cheaply once art and maps exist, so they're locked before code.
>
> **Status:** proposal. Decisions marked **[LOCKED]** were confirmed in session;
> the rest are recommendations with reasoning, open to revision.

## The register we're matching

All three primary references — **Mother 3**, **Pokémon Emerald**, **Chrono
Trigger** — use **16×16 pixel tiles** with characters drawn **taller than one
tile**. This is not incidental; it *is* the 8–16-bit overworld register.
Prison-escape's 32px tiles are an action-game choice (bigger hitboxes, closer
camera) and are the wrong register for Wayworn Hush.

Confirmed reference specs:

| Game | Native res | Tile | On-screen tiles | Character sprite |
|---|---|---|---|---|
| Pokémon Emerald (GBA) | 240×160 | 16px | 15×10 | ~16×32 (2 tiles tall) |
| Mother 3 (GBA) | 240×160 | 16px | 15×10 | 16px objects composited taller |
| Chrono Trigger (SNES) | 256×224 | 16px | 16×14 | overworld ~11×17; field ~15×36 |

The shared truth: **16px grid, sub-tile-precise character, camera showing
~15 tiles wide.**

---

## Decisions

### Tile size — **32×32 [LOCKED]**

32px logical tiles. Chosen to match the protagonist sprite's pixel density (see
below): the character is authored at 32×64, so a 32px tile keeps the
EarthBound/Pokémon **on-screen scale** (character ≈ 1 tile wide, 2 tall) while
the art carries more detail than a 16px grid could hold. This shifts the register
slightly toward "modern-HD-pixel" rather than strict 8-bit — which fits the
detail level of the protagonist art.

> **History:** an earlier draft locked 16px (Emerald-authentic minimal register).
> That was revised when the protagonist sprite — a higher-density ~1:2 design
> whose curls/beard/detail need the pixels — set the density. The *on-screen
> scale* is unchanged (still ~1 tile wide, 2 tall like the references); only the
> pixel density went up, and the tile grid scaled with it to preserve that scale.

**Engine consequence — [LOCKED]:** the engine's `TILE_SIZE` was a shared
`constexpr int = 32`. It was **promoted to a per-game runtime value** (config
over constants; SLICE.md step 0, shipped). Wayworn sets 32 *by its own deliberate
choice for its own reasons* (overworld scale + sprite density) — not by
inheriting a shared constant. Prison-escape independently sets 32 for its
action-combat reasons. The per-game field is exactly what lets each game own its
number; the fact that both land on 32 is coincidence, not coupling.

### Movement model — **free pixel movement (Chrono Trigger) [LOCKED]**

The character moves smoothly at sub-tile precision in any direction; collision is
AABB-vs-solid-tile. **Not** grid-step (Pokémon). The *motion* is Chrono-Trigger
fluid, for the "wander and look at vistas" feel.

**Why this is also the cheaper path:** the engine already does free-vector
movement + AABB-vs-solid-tile collision (`CollisionSystem` + prison-escape's
`MovementSystem` integrator). No grid-stepping system to invent. Prison-escape's
`MovementSystem` continuous integrator transfers nearly as-is.

### Character sprite — **32 wide × 64 tall (2 tiles) [LOCKED]**

- **32×64.** Set by the protagonist art — a ~1:2 (width:height) design with
  enough density for curly hair, beard, shirt detail, and sandals to read. At a
  32px tile grid this is **1 tile wide × 2 tall on screen** — the
  EarthBound/Pokémon character-to-world scale, at higher pixel density.
- **Foot anchor:** the collision AABB is a small box at the **feet** (roughly
  24×12 at the base), not the full 32×64 art bounds — so the character tucks
  behind objects and Y-sorts correctly (RenderSystem sorts by foot-Y). The art
  overhangs the collider upward; only the feet block.
- **Asset note:** the source protagonist art is an AI-generated pixel-*style*
  render, not true grid pixel art. It must be downscaled to a real 32×64 pixel
  grid (nearest-neighbor) and palette-quantized (indexed, ~15–30 colors) before
  it's a game asset — hand cleanup follows the resample. The 32×64 target was
  chosen by eye as the size at which the character's detail survives.

### Render target — **768×432 internal, integer-scaled [LOCKED]**

- Widescreen 16:9 internal target at the 32px density, integer-upscaled to the
  window (nearest-neighbor). The register is modern-retro, not strict 8-bit.
- **Internal resolution: 768×432** = exactly 16:9, = **24×13.5 tiles** at 32px.
  Same on-screen *tile count* as the earlier 16px/384×216 plan (24 wide) — the
  world shows the same amount of space; each tile is just denser.
  - 768×432 integer-scales cleanly: ×2 = 1536×864, ×2.5 = 1920×1080 **exactly**.
  - At 1080p, ×2.5 hits native perfectly (no letterbox on 16:9).
- **Built and verified** (SLICE.md step 2 / commit `04a6ac9`): the engine
  `PixelRenderTarget` renders the world into an offscreen FBO at `GL_NEAREST` and
  blits it up at the largest integer scale, letterboxing the remainder.
  **The internal resolution constant in code must change from 384×216 → 768×432**
  (`kInternalWidth`/`kInternalHeight` in `games/wayworn-hush/include/GameLoop.h`).
- **Alternative considered & rejected:** downscaling the protagonist art to fit a
  16px grid. Rejected — it throws away the detail that made the sprite right.
  Scaling the whole budget up (tiles + internal res together) preserves both the
  detail and the on-screen scale.

### Camera field — **~24×13.5 tiles, follow with dead-zone**

Falls out of the render target: 768÷32 = 24 wide, 432÷32 = 13.5 tall. Camera
follows the player (engine `CameraSystem` + interpolation already do this).

- **Camera zoom stays 1.0** — the internal target *is* the pixel-art resolution;
  the integer upscale to the window is the only scaling. (Prison-escape uses
  `setCameraZoom(2.0f)` on a full-res window because it has no render target;
  we don't want that — zoom-scaling a no-FBO pipeline reintroduces non-integer
  sampling.)
- **Recommend a follow dead-zone** (a small central box the player moves within
  before the camera scrolls) for the calmer, less-glued Mushishi feel, rather
  than a hard-locked center. Small polish item, not foundational.

### Tilemap / region sizing — **authored regions, whole-region load** *(recommended)*

- **Author whole regions as static hand-made maps** (not procedural, not
  streamed sub-chunks). The world is finite and authored (DESIGN.md: "not
  infinite / procgen"). One region = one authored tilemap loaded whole on entry.
- **Region size guidance:** a region that takes ~30–90 seconds to cross on foot
  feels like a Pokémon route / Mother-3 area. The viewport is 24 tiles wide; a
  **~48×48 to ~96×96 tile** region (1536×1536 to 3072×3072 px at 32px tiles) is a
  comfortable authored unit — a few screens each direction, walkable in a minute
  or two, memorable as a place.
- **Whole-region load is fine at this scale.** A 96×96 tile map is 9,216 tiles —
  the engine bakes the whole tilemap into one VBO with one draw call regardless
  of size. No streaming needed until regions get much larger than the aesthetic
  wants.
- **World-connection model:** hybrid open world — discrete authored regions
  stitched seamlessly at edges + warps for interiors. Locked in
  [MAP-ARCHITECTURE.md](MAP-ARCHITECTURE.md) §3.

---

## Summary table

| Parameter | Value | Status |
|---|---|---|
| Tile size | 32×32 px | **LOCKED** |
| Movement | Free pixel (sub-tile AABB), Chrono-Trigger fluid | **LOCKED** |
| Character sprite | 32×64 px (1×2 tiles), foot-anchored collider | **LOCKED** |
| Internal render res | 768×432 (16:9, 24×13.5 tiles) | **LOCKED** |
| Upscale | Integer nearest-neighbor, largest fit, letterbox remainder | **LOCKED** (shipped) |
| Camera zoom | 1.0 (upscale does all scaling) | recommended |
| Camera field | ~24×13.5 tiles, follow + dead-zone | recommended |
| Region size | ~48×48 to ~96×96 tiles, authored, whole-region load | recommended |
| Engine TILE_SIZE | Per-game runtime field (shipped) | **LOCKED** (shipped) |

---

## Open questions

1. **Internal resolution: 768×432 vs a taller 4:3-ish frame.** 768×432 is
   modern-widescreen (locked). If a scene ever wants more *vertical* landscape
   (tall mountains, sky), a 4:3-ish frame shows more sky but letterboxes on
   widescreen displays. Sticking with 768×432 unless a strong reason appears.
2. **Camera follow dead-zone size** — a polish tuning value, not foundational.

## Cross-references

- [AUDIT.md](AUDIT.md) — what exists to reuse.
- [SLICE.md](SLICE.md) — the build plan that implements these numbers.
- [AESTHETIC.md](AESTHETIC.md) — palette/art register these numbers serve.
