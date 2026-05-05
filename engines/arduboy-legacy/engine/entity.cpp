#include "entity.h"

#include <string.h>

namespace ent {

Entity pool[POOL_SIZE];
fx player_facing_dx;
fx player_facing_dy;

// Place spawn() in .hightext so it's linked just before the scene
// banks. This keeps it within ±4 KB rcall reach of any bank function,
// even when the banks are nearly full. PLAY's bank-tail callers were
// hitting R_AVR_13_PCREL truncation here; positional fix in scene.ld.
__attribute__((section(".hightext"))) Entity* spawn(Kind k) {
  for (u8 i = 0; i < POOL_SIZE; ++i) {
    if (!pool[i].active) {
      memset(&pool[i], 0, sizeof(Entity));
      pool[i].kind   = (u8)k;
      pool[i].active = 1;
      return &pool[i];
    }
  }
  return nullptr;
}

void despawn(Entity* e) {
  if (e) e->active = 0;
}

void clear_pool() {
  memset(pool, 0, sizeof(pool));
}

}  // namespace ent
