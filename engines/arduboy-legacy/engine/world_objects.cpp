// World-object renderer + collision query. See world_objects.h for the
// architecture rationale.

#include "world_objects.h"

#include "data_flash.h"

namespace world_objects {

// Read N records from FX in one SPI burst, then iterate them. 8 records
// = 48 B, fits comfortably on the stack and amortizes SPI setup over a
// useful chunk. Sized down to 8 (not larger) so the stack frame stays
// under 64 B even with the i16 locals — the AVR stack ceiling lives
// at ~430 B today (see feedback_check_stack_after_bss_changes.md).
constexpr u8 CHUNK = 8;

// Conservative max-sprite extent for viewport culling. Real sprites in
// the Wood will range up to ~32 wide / ~48 tall (a tree). We pad the
// viewport by this amount so a sprite with origin off-screen but body
// still on-screen isn't dropped. Larger pad = more reads, smaller pad
// = visible pop-in. 48 is the current ceiling.
constexpr u8 SPRITE_PAD_X = 48;
constexpr u8 SPRITE_PAD_Y = 48;

void draw_viewport(u32 objects_fx_offset, u16 count,
                   i16 cam_x, i16 cam_y,
                   DrawCallback draw) {
  if (count == 0 || draw == nullptr) return;

  // Viewport bounds in world pixels, padded so partially-on-screen
  // sprites still fire the callback. fb::draw_sprite already clips
  // its own off-screen pixels.
  const i16 vx0 = (i16)(cam_x - (i16)SPRITE_PAD_X);
  const i16 vy0 = (i16)(cam_y - (i16)SPRITE_PAD_Y);
  const i16 vx1 = (i16)(cam_x + 128);  // exclusive
  const i16 vy1 = (i16)(cam_y + 64);

  Object buf[CHUNK];
  u16 i = 0;
  while (i < count) {
    const u8 want = (count - i) < CHUNK ? (u8)(count - i) : CHUNK;
    // 32bit-ok: objects_fx_offset is u32 (FX byte addresses cross 16-bit)
    // and i * sizeof(Object) can exceed 16-bit for large worlds.
    const u32 read_off = objects_fx_offset + (u32)i * (u32)sizeof(Object);
    data_flash::read(read_off, buf, (u32)want * (u32)sizeof(Object));
    for (u8 k = 0; k < want; ++k) {
      const Object& o = buf[k];
      // Cheap viewport reject. Y is checked first because the list is
      // pre-sorted by Y — once we see an object whose y is past the
      // bottom of the viewport, every subsequent object is too. Could
      // early-out the outer loop for big worlds; for the Wood's scale
      // a full pass costs ~1 ms total, so we keep the code simple.
      if (o.y >= vy1 || o.x >= vx1) continue;
      if (o.x < vx0 || o.y < vy0) continue;
      const i16 sx = (i16)(o.x - cam_x);
      const i16 sy = (i16)(o.y - cam_y);
      draw(o.sprite_id, sx, sy);
    }
    i = (u16)(i + want);
  }
}

bool collides_solid(u32 objects_fx_offset, u16 count,
                    i16 test_x, i16 test_y, u8 w, u8 h) {
  if (count == 0) return false;
  const i16 tx1 = (i16)(test_x + (i16)w);  // exclusive
  const i16 ty1 = (i16)(test_y + (i16)h);

  // Hardcoded 16×16 bounding box anchored at (object.x, object.y). See
  // the world_objects.h note about extending this to per-sprite sizes.
  constexpr u8 OBJ_W = 16;
  constexpr u8 OBJ_H = 16;

  Object buf[CHUNK];
  u16 i = 0;
  while (i < count) {
    const u8 want = (count - i) < CHUNK ? (u8)(count - i) : CHUNK;
    // 32bit-ok: see draw_viewport above for the same arithmetic.
    const u32 read_off = objects_fx_offset + (u32)i * (u32)sizeof(Object);
    data_flash::read(read_off, buf, (u32)want * (u32)sizeof(Object));
    for (u8 k = 0; k < want; ++k) {
      const Object& o = buf[k];
      if (!(o.flags & FLAG_SOLID)) continue;
      const i16 ox1 = (i16)(o.x + (i16)OBJ_W);
      const i16 oy1 = (i16)(o.y + (i16)OBJ_H);
      // AABB overlap.
      if (test_x < ox1 && tx1 > o.x && test_y < oy1 && ty1 > o.y) {
        return true;
      }
    }
    i = (u16)(i + want);
  }
  return false;
}

}  // namespace world_objects
