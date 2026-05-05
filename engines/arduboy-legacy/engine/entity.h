// Fixed-size entity pool.
//
// Per the design doc: player, enemies, bosses, and bullets are all the same
// shape. One struct, one pool, no inheritance.
//
// Pool slots are reused — spawn() finds the first inactive slot and returns
// a pointer; despawn() flips `active` off. No heap allocation, no pointer
// stability issues from reallocation, no per-frame churn.
//
// Sizing: 24 slots is a comfortable upper bound for the current design.
// Worst-case scene audited: 1 player + ~3 enemies (incl. boss) + ~6 player
// bullets + ~6 enemy bullets + ~4 sangue drops = ~20 active. 24 gives a
// small margin; drop on spawn failure is the fallback. Each slot costs
// sizeof(Entity) (~20B) of RAM; shrinking from 32 -> 24 reclaims 160B.

#pragma once

#include "fixed.h"
#include "types.h"

namespace ent {

constexpr u8 POOL_SIZE = 24;

// Categorizes an entity for collision filtering and AI dispatch. Keep this
// flat — it's a tag, not a class hierarchy. New behaviors should be new
// values, not new code branches at the call site.
enum Kind : u8 {
  NONE        = 0,
  PLAYER      = 1,
  ENEMY       = 2,
  PLAYER_SHOT = 3,
  ENEMY_SHOT  = 4,
  PICKUP      = 5,  // sangue drops, future power-ups, etc
};

struct Entity {
  // Position in 8.8 fixed-point pixels.
  fx x;
  fx y;

  // Velocity in 8.8 fixed-point pixels per frame.
  fx dx;
  fx dy;

  // The three RPG stats. Same block for player / enemies / bosses.
  u8 hp;
  u8 max_hp;  // capacity (for HP bars + size-by-stat scaling)
  u8 fire_rate;
  u8 damage;

  // Tag + state.
  u8 kind;       // one of Kind
  u8 sprite_id;  // index into the per-game sprite table (or PROCEDURAL marker)
  u8 active;
  u8 timer;  // per-entity, multipurpose (cooldowns / despawn-after-N)

  // Facing (last-known shoot/move direction) lives in a player-specific
  // global instead of the Entity struct — only the player reads/writes it,
  // so storing it on every slot wastes 4B * POOL_SIZE. The player can read
  // it via ent::player_facing_dx / _dy in game code.
};

// Player-only facing vector, 8.8 fixed-point. Not normalized — only the
// direction matters (the facing-pixel renderer picks an axis by dominance).
// Updated when the player spawns or moves; read by the bullet spawner and
// the player overlay.
extern fx player_facing_dx;
extern fx player_facing_dy;

// Size guard: the pool is POOL_SIZE * sizeof(Entity) bytes of RAM. Growing
// the struct silently quadruples the pool cost. This assertion breaks the
// build if anyone adds a field without updating the expected size.
// Current: 4 fx (8B) + 8 u8 (8B) = 16B. No padding (u8 × 8 fits the tail).
static_assert(sizeof(Entity) == 16, "Entity size changed -- check pool RAM impact");

// The pool itself. Iterate as `for (u8 i = 0; i < POOL_SIZE; ++i) if (pool[i].active) ...`.
extern Entity pool[POOL_SIZE];

// Find a free slot, mark it active, zero it out, set kind. Returns nullptr
// if the pool is full (caller decides what to do — usually drop the spawn).
Entity* spawn(Kind k);

// Mark an entity inactive. Safe to call on an already-inactive slot.
void despawn(Entity* e);

// Convenience: zero everything (call once at game start / on death).
void clear_pool();

}  // namespace ent
