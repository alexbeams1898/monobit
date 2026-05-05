// World-object renderer for the Wood (and any future map-driven scene).
//
// Two-layer scene composition:
//   1. Ground tilemap (engine/tilemap.h, draw_viewport_fx) — repeating
//      substrate, drawn first.
//   2. Object layer (this header) — discrete sprites placed at arbitrary
//      world coordinates (trees, stones, NPCs, structures). Drawn after
//      the ground, viewport-culled, Y-sorted.
//
// Why two layers: the ground is cheap to author as "stamps in a grid"
// and compresses well via tile palette dedup. The object layer carries
// shape, identity, collision, and interaction — things tile-grid
// composition handles awkwardly. Pokémon-GBC overworld is the model.
//
// Authoring path (eventual): a browser map editor emits a JSON list of
// objects ({x, y, sprite_id, flags}); a bake script produces the binary
// blob this code consumes. Same pipeline shape as the existing sprite-
// editor → touchup → bake → FX flow.

#pragma once

#include "types.h"

namespace world_objects {

// On-disk record. 6 bytes, packed tight, pre-sorted by `y` at bake time
// so the renderer's Y-sort is free (just iterate in order, insert the
// player at his Y when drawing).
//
// `x`, `y` are world pixels (top-left of the sprite). i16 covers world
// sizes up to 32k px in each axis, far past anything authorable.
//
// `sprite_id` indexes into the Wood's object-sprite table — game-side
// dispatch maps it to a sprite::* art entry. The renderer just hands
// (sprite_id, screen_x, screen_y) to a caller-supplied draw callback,
// so the engine layer stays game-agnostic.
//
// `flags` bit layout (low→high):
//   bit 0:    SOLID — the object's bounding box blocks movement
//   bit 1:    INTERACT — A-press while in proximity fires an event
//   bits 2-5: INTERACT_ID — which interaction (4 bits = 16 distinct)
//   bits 6-7: reserved (Y-anchor mode? animation flag? TBD)
struct Object {
  i16 x;
  i16 y;
  u8  sprite_id;
  u8  flags;
};

constexpr u8 FLAG_SOLID    = 0x01;
constexpr u8 FLAG_INTERACT = 0x02;
constexpr u8 INTERACT_ID_SHIFT = 2;
constexpr u8 INTERACT_ID_MASK  = 0x3C;  // bits 2-5

// Draw-callback contract. The engine doesn't know about the game's
// sprite table, so the caller hands in a function that takes a
// sprite_id + screen-pixel position and renders. This keeps engine/
// platform-agnostic AND game-agnostic — same code can serve Wood,
// circles, future maps.
//
// Using a fnptr (not a virtual or std::function) keeps the call
// indirection cheap and keeps us out of any STL territory.
using DrawCallback = void (*)(u8 sprite_id, i16 screen_x, i16 screen_y);

// Stream the object list from FX flash, viewport-cull, and dispatch
// each visible object's draw via the callback.
//
// `count` is the number of records (NOT bytes) at `objects_fx_offset`.
// At ~6 B per record the SPI cost is small even for a few-hundred-
// object world; we read the list in chunks (see implementation) to
// amortize SPI setup over multiple records per call.
//
// Caller is responsible for:
//   - Drawing the ground tilemap FIRST (call this AFTER).
//   - Inserting any dynamic entities (player, NPCs that move) into the
//     draw order at their own Y; for now caller draws them after the
//     object list (player on top of all decorations).
void draw_viewport(u32 objects_fx_offset, u16 count,
                   i16 cam_x, i16 cam_y,
                   DrawCallback draw);

// Solid-collision query. Returns true if any SOLID object's 16×16
// bounding box (anchored at object.x, object.y) overlaps the AABB
// [test_x .. test_x + w - 1, test_y .. test_y + h - 1]. Iterates the
// FX object list — for the Wood's hundreds-of-objects scale this is
// a sub-millisecond per-frame check.
//
// Bounding-box size is currently hardcoded to 16×16 (one tile). Future
// extension: a per-sprite-id width/height table reused from the
// existing sprites:: metadata, or pack collision-w/h into the record's
// reserved bits.
bool collides_solid(u32 objects_fx_offset, u16 count,
                    i16 test_x, i16 test_y, u8 w, u8 h);

}  // namespace world_objects
