#pragma once

#include "types.h"

namespace game {

void init();    // Set up entities, state, etc.
void update();  // Called once per frame, before draw.
// Called once per frame, after update. Returns true if the framebuffer
// changed and needs flushing to the display, false if the previous frame's
// content is still valid. Lets static menus skip both the redraw cost and
// the SPI transfer.
bool draw();

// Debug/test harness overrides. Populated from command-line flags by the
// SDL build's cli parser; applied once after game::init() and before the
// first frame. Arduboy build has no caller, so the implementation is
// gated behind ENABLE_DEBUG_OVERRIDES and costs zero bytes in release.
//
// Each "has_*" flag gates whether the corresponding value is applied.
// Keep this a plain data struct — no constructors, no STL.
struct DebugCfg {
  // State to jump to. If past TITLE, the title splash is skipped entirely.
  bool has_state = false;
  u8 state       = 0;
  // Override wave number (applies only when transitioning into PLAYING).
  bool has_wave = false;
  u16 wave      = 0;
  // Sprite id of a shade to spawn (requires PLAYING). Uses the same
  // routing as the old DEBUG spawner: id 18..26 -> spawn_boss, lower
  // -> minion with base stats.
  bool has_spawn     = false;
  u8 spawn_sprite_id = 0;
  // Meta character overrides (transient — never written to save).
  bool has_stats     = false;
  u8 level_hp        = 0;
  u8 level_damage    = 0;
  u8 level_fire_rate = 0;
  bool has_sangue    = false;
  u16 sangue_vessel  = 0;
  // Lifetime counters — these feed RECKONING. sangue_vessel is separate
  // (spendable now); total_sangue_earned is monotonic-lifetime. Setting
  // --sangue does NOT touch --total-sangue by design: one is wallet,
  // one is trophy.
  bool has_total_sangue = false;
  u32 total_sangue      = 0;
  bool has_runs         = false;
  u16 total_runs        = 0;
  bool has_kills        = false;
  u16 total_kills       = 0;
  bool has_keepers      = false;
  u8 total_keepers      = 0;
  bool has_bullet       = false;
  u8 bullet_tier        = 0;
  // Wipe meta to a fresh pilgrim. Writes to persistent storage; this IS
  // intentional since --wipe means "start from nothing."
  bool wipe = false;
  // Suppress all enemy/boss spawns — for visual sprite tests where you
  // want to see the player alone in the level. SDL only.
  bool no_enemies = false;
};

// Live runtime view of debug config — reads return 0/false on Arduboy
// (where ENABLE_DEBUG_OVERRIDES isn't defined). Game code uses the
// `debug_*` accessors below rather than poking the cfg struct directly.
bool debug_no_enemies();

void apply_debug_overrides(const DebugCfg& cfg);

}  // namespace game
