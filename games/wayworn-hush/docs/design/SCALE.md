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

### Tile size — **16×16 [LOCKED]**

16px logical tiles. Matches every reference. Sprites and maps author on a 16px
grid.

**Engine consequence — [LOCKED]:** the engine's `TILE_SIZE` is a shared
`constexpr int = 32` in `TileMap.h`. It's being **promoted to a per-game runtime
value** (config over constants). Prison-escape sets 32; Wayworn sets 16. This is
the correct structural fix — the engine should not bake one game's tile size —
and it's the first engine work item (SLICE.md step 0). Blast radius on the engine
side: `TileMap` (grid math), `CollisionSystem` (tilemap depenetration cell math),
and prison-escape's calibrated constants (`MOVEMENT_INSET`, flow-field
`CELL_SIZE`) which stay at their 32-derived values because prison-escape keeps
tile_size=32.

### Movement model — **free pixel movement (Chrono Trigger) [LOCKED]**

The character moves smoothly at sub-tile precision in any direction; collision is
AABB-vs-solid-tile. **Not** grid-step (Pokémon). Art register stays Emerald —
minimal, cute, affective sprites — but the *motion* is Chrono-Trigger fluid, for
the "wander and look at vistas" feel.

**Why this is also the cheaper path:** the engine already does free-vector
movement + AABB-vs-solid-tile collision (`CollisionSystem` + prison-escape's
`MovementSystem` integrator). No grid-stepping system to invent. Prison-escape's
`MovementSystem` continuous integrator transfers nearly as-is.

### Character sprite — **16 wide × 32 tall (2 tiles)** *(recommended)*

- **16×32** matches Pokémon Emerald's overworld sprite proportions. Since the
  art direction leans toward studying and replicating Emerald's actual sprite
  construction, the character should share its proportions: 2 tiles tall, with
  room for legible head/torso/legs and expressive idle/walk detail at 16px
  width.
- **Foot anchor:** the sprite's collision AABB is a small box at the **feet**
  (roughly 12×8 at the base), not the full 16×32 art bounds — so the character
  can tuck behind objects and Y-sort correctly (RenderSystem already sorts by
  foot-Y). The art overhangs the collider upward; only the feet block.
- **Open:** 16×32 vs 16×24. 16×32 is Emerald-authentic and detail-rich; 16×24
  is daintier and more Mother-3. Recommendation follows the Emerald-study
  direction, but this is an art-feel call, not locked.

### Render target — **384×216 internal, integer-scaled [LOCKED register]**

- **[LOCKED]** widescreen 16px modern-retro (your call): 16px tiles + taller
  sprites + a 16:9-ish internal target, integer-upscaled to the window.
- **Proposed internal resolution: 384×216** = exactly 16:9, = **24×13.5 tiles**
  at 16px. Wider than Emerald's 15 tiles — good, "landscape as protagonist"
  wants breathing room — while keeping the character large on screen.
  - 384×216 integer-scales cleanly: ×3 = 1152×648, ×4 = 1536×864, ×5 =
    1920×1080 (near-perfect for the most common display).
  - At 1080p, ×5 = 1920×1080 exactly. No letterbox on 16:9 displays.
- **This does not exist in the engine yet** — there is no FBO. The build item is:
  render the world + sprites into a 384×216 offscreen framebuffer at
  `GL_NEAREST`, then blit it to the window at the largest integer scale that
  fits (letterbox the remainder with the ambient background color). This is
  SLICE.md's first *visible* milestone — nothing looks right until it exists.
- **Alternative considered & rejected:** authoring at 32px and downscaling to
  fake 16px. Rejected — it fakes the register instead of living in it; the 16px
  grid the references actually use would never exist. (Matches monobit doctrine:
  don't trade correctness for a smaller diff.)

### Camera field — **~24×13.5 tiles, follow with dead-zone**

Falls out of the render target: 384÷16 = 24 wide, 216÷16 = 13.5 tall. Camera
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
  feels like a Pokémon route / Mother-3 area. At 384px view width and a walk
  speed of ~1.5 tiles/sec, a **~48×48 to ~96×96 tile** region (768×768 to
  1536×1536 px) is a comfortable authored unit — a few screens in each
  direction, walkable in a minute or two, memorable as a place.
- **Whole-region load is fine at this scale.** A 96×96 tile map is 9,216 tiles —
  the engine bakes the whole tilemap into one VBO with one draw call regardless
  of size. No streaming needed until regions get much larger than the aesthetic
  wants. Region-to-region transitions (edge warps / a world map) are the seam,
  not sub-chunk streaming.
- **Deferred:** whether regions connect seamlessly (walk off the edge → next
  region loads) or via a world-map/route-select screen (Pokémon style). Both
  work with whole-region load; it's a design-feel call for later.

---

## Summary table

| Parameter | Value | Status |
|---|---|---|
| Tile size | 16×16 px | **LOCKED** |
| Movement | Free pixel (sub-tile AABB), Chrono-Trigger fluid | **LOCKED** |
| Character sprite | 16×32 px (2 tiles, Emerald proportions), foot-anchored collider | recommended (16×32 vs 16×24 open) |
| Internal render res | 384×216 (16:9, 24×13.5 tiles) | recommended |
| Upscale | Integer nearest-neighbor, largest fit, letterbox remainder | **LOCKED register** |
| Camera zoom | 1.0 (upscale does all scaling) | recommended |
| Camera field | ~24×13.5 tiles, follow + dead-zone | recommended |
| Region size | ~48×48 to ~96×96 tiles, authored, whole-region load | recommended |
| Engine TILE_SIZE | Promote constexpr → per-game runtime | **LOCKED** |

---

## Open questions (need an aesthetic call before locking)

1. **Character height: 16×32 vs 16×24.** 32 = Emerald-authentic, more
   idle-detail room (current recommendation); 24 = daintier/Mother-3. Art-feel
   call, tied to the palette-direction question in AESTHETIC.md.
2. **Internal resolution: 384×216 vs a taller 4:3-ish frame.** 384×216 is
   modern-widescreen (your stated preference). If a scene ever wants more
   *vertical* landscape (tall mountains, sky), a 4:3-ish 320×240 shows more sky
   but letterboxes on widescreen displays. Sticking with 384×216 unless you want
   the taller frame.
3. **Region connection model** (seamless edge-warp vs world-map select) —
   deferred; doesn't block the slice.

## Cross-references

- [AUDIT.md](AUDIT.md) — what exists to reuse.
- [SLICE.md](SLICE.md) — the build plan that implements these numbers.
- [AESTHETIC.md](AESTHETIC.md) — palette/art register these numbers serve.
