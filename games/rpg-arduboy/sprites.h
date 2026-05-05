// Per-game sprite table.
//
// Sprites are stored column-major in PROGMEM. For sprites <=8 tall, each
// column is one byte (bit 0 = top row, bit 7 = row 7). For taller sprites,
// each column is multiple bytes stacked top-to-bottom — byte 0 covers rows
// 0-7, byte 1 covers rows 8-15, etc. Total bytes per sprite =
// width * ceil(height / 8).
//
// The renderer side (`draw_progmem_sprite`) walks both dimensions and emits
// one fb::draw_sprite call per "page" (8-row band).

#pragma once

#include "types.h"

namespace sprites {

enum Id : u8 {
  // Player + projectiles + pickups.
  PLAYER      = 0,
  ENEMY       = 1,  // generic enemy fallback (still used for non-circle waves)
  BULLET      = 2,
  SANGUE_DROP = 3,  // tier 1 (1-4 sangue): a single dot/pip
  // UI logos.
  LOGO_TITLE        = 4,
  LOGO_SECOND_DEATH = 5,
  // Dante base enemies (12 total, all 8x8). Bestiary list expects these
  // to be a contiguous range starting at ENE_WRAITH; do not insert other
  // sprite kinds in the middle.
  ENE_WRAITH      = 6,   // limbo,    SINNER
  ENE_LUST_SPIRIT = 7,   // lust,     SINNER
  ENE_WORM        = 8,   // gluttony, SINNER
  ENE_SWINE       = 9,   // gluttony, SINNER (face-on hog)
  ENE_JOUSTER     = 10,  // greed,    SINNER (hoarder)
  ENE_PROFLIGATE  = 11,  // greed,    SINNER (squanderer; mirror of JOUSTER)
  ENE_BRAWLER     = 12,  // wrath,    THRALL
  ENE_TOMB_SHADE  = 13,  // heresy,   THRALL
  ENE_HARPY       = 14,  // violence, THRALL
  ENE_CENTAUR     = 15,  // violence, FIEND  (patroller of Phlegethon)
  ENE_MALEBRANCHE = 16,  // fraud,    FIEND
  ENE_ICE_TRAITOR = 17,  // treachery, FIEND
  // Dante bosses (16x16 except Lucifer 20x24). Contiguous range starting
  // right after the minions so bestiary indices flow cleanly.
  BOSS_CHARON   = 18,  // limbo (wave 1 boss)
  BOSS_MINOS    = 19,  // lust
  BOSS_CERBERUS = 20,  // gluttony
  BOSS_PLUTUS   = 21,  // greed
  BOSS_PHLEGYAS = 22,  // wrath
  BOSS_MEDUSA   = 23,  // heresy
  BOSS_MINOTAUR = 24,  // violence
  BOSS_GERYON   = 25,  // fraud
  BOSS_LUCIFER  = 26,  // treachery (EMPEROR — own tier of one)
  // Sangue pickup tiers, escalating quantities of blood (Phlegethon, Inf.
  // XII). Names are Dante-coded liquid-volumes: drop -> rivulet -> river
  // -> flood -> deluge. Sprite art may still be the older coin shapes —
  // a follow-up will redraw them as actual blood imagery.
  SANGUE_RIVULET = 27,  // tier 2 (5-9)
  SANGUE_RIVER   = 28,  // tier 3 (10-24)
  SANGUE_FLOOD   = 29,  // tier 4 (25-99)
  SANGUE_DELUGE  = 30,  // tier 5 (100+; boss-tier drop)
  // Enemy projectiles — pilgrim fires BULLET (modern intrusion), damned
  // shades hurl canon-coded weapons. STONE is the generic fallback (Canto
  // VII's hoarders and wastrels roll stones); the four named shapes below
  // are circle-specific overrides for their respective shooter.
  DART_STONE = 31,  // default enemy projectile — 3x3 chunk
  DART_ARROW = 32,  // centaur (Canto XII, Nessus/Chiron/Pholus) — 4x1 streak
  DART_HOOK  = 33,  // malebranche (Canto XXI-XXII) — 3x3 barb
  DART_SHARD = 34,  // ice traitor (Canto XXXII-XXXIV, Cocytus) — 1x3 icicle
  DART_WIND  = 35,  // lust spirit (Canto V, tempest) — 3x1 horizontal gust
  // Penitent L1 world animations (12x16). Drawn right-facing only;
  // the renderer mirrors horizontally for left-facing motion. Frame
  // counts come from the saved strips in art/animations/.
  // Order is contiguous so the frame table can index by base + offset.
  PEN_L1_IDLE_F0   = 36,
  PEN_L1_IDLE_F1   = 37,
  PEN_L1_WALK_F0   = 38,
  PEN_L1_WALK_F1   = 39,
  PEN_L1_WALK_F2   = 40,
  PEN_L1_WALK_F3   = 41,
  PEN_L1_ATTACK_F0 = 42,
  PEN_L1_ATTACK_F1 = 43,
  // Player class portraits — 3 classes × 3 levels = 9 sprites, interleaved
  // by class so (class * 3 + level - 1) indexes directly. Class order:
  // 0 = penitent, 1 = wretched, 2 = heretic. Levels 1..3.
  // All FX-resident; portrait sizes vary 26..62 px wide so they don't fit
  // PROGMEM headroom. Reckoning + Guide screens dispatch via fx_offset().
  // Note: SP_PENITENT_L1 currently aliases pen_l1_idle_f0_data's 12x16
  // bytes via OFFSET_PEN_L1_IDLE_F0, since the canonical L1 sprite is the
  // touched-up animation frame. The other 8 portraits source from
  // art/maincharevo.png at their declared sizes.
  SP_PENITENT_L1 = 44,
  SP_PENITENT_L2 = 45,
  SP_PENITENT_L3 = 46,
  SP_WRETCHED_L1 = 47,
  SP_WRETCHED_L2 = 48,
  SP_WRETCHED_L3 = 49,
  SP_HERETIC_L1  = 50,
  SP_HERETIC_L2  = 51,
  SP_HERETIC_L3  = 52,
  // Player class world tokens — same 9 (class, level) combinations, but
  // small in-world sprites (~12x16 to ~27x24). Same indexing rule.
  // SP_PENITENT_L1_WORLD aliases PEN_L1_IDLE_F0 (no separate sprite).
  SP_PENITENT_L1_WORLD = 53,
  SP_PENITENT_L2_WORLD = 54,
  SP_PENITENT_L3_WORLD = 55,
  SP_WRETCHED_L1_WORLD = 56,
  SP_WRETCHED_L2_WORLD = 57,
  SP_WRETCHED_L3_WORLD = 58,
  SP_HERETIC_L1_WORLD  = 59,
  SP_HERETIC_L2_WORLD  = 60,
  SP_HERETIC_L3_WORLD  = 61,
  // Index base for the 18 class sprites — callers compute
  // SP_CLASS_FIRST + class * 3 + (level - 1) for the portrait,
  // SP_CLASS_FIRST + 9 + class * 3 + (level - 1) for the world token.
  SP_CLASS_FIRST = SP_PENITENT_L1,
  // Pre-class "unburdened" Pilgrim — the wraith-form vessel before
  // class-select. Hand-traced on FX flash (see art/touchups/). VITA/
  // IRA/FURIA all 0, BURDEN 0. Used at the start of a new run, in the
  // Charon cutscene + class-select screen, and as the Reckoning
  // page-0 dev preview slot 0. Lore: docs/design/ "Before the
  // naming — the unburdened".
  //   SP_UNBURDENED       — 40×52 portrait (Reckoning page 1, ceremony)
  //   SP_UNBURDENED_WORLD — 16×20 token   (Reckoning icon, in-world)
  SP_UNBURDENED       = 62,
  SP_UNBURDENED_WORLD = 63,
  // Unburdened world animation frames — same scheme as PEN_L1_*_F*.
  // Sliced from art/animations/player_unburdened_world_data/{idle,walk,
  // attack}.png. Idle = 2 frames, walk = 4, attack = 2. All FX-resident.
  UB_IDLE_F0   = 64,
  UB_IDLE_F1   = 65,
  UB_WALK_F0   = 66,
  UB_WALK_F1   = 67,
  UB_WALK_F2   = 68,
  UB_WALK_F3   = 69,
  UB_ATTACK_F0 = 70,
  UB_ATTACK_F1 = 71,
  COUNT        = 72,
};

// Sprite metadata accessors. The underlying tables live in PROGMEM on AVR
// (flash-resident, never in RAM) and .rodata on PC. Direct array access
// would dereference flash addresses as RAM on AVR and read garbage, so
// the arrays themselves are not exposed — go through these functions.
u8 width(u8 id);
u8 height(u8 id);  // pixels tall
// Returns the raw sprite-data pointer in PROGMEM, or nullptr for sprites
// stored as LZ77 streams (currently LOGO_TITLE and LOGO_SECOND_DEATH)
// or FX flash (player anim frames, eventually portraits + bosses).
// Most sprites are raw and this is the pointer the renderers want.
const u8* data(u8 id);

// FX-flash byte offset for this sprite, or 0xFFFFFFFF if the sprite
// lives in PROGMEM (data() is non-null) or LZ77 (lz77_data() is
// non-null). Used by the FX-source renderer to read bytes via
// data_flash::read(). Only one of {data, lz77_data, fx_offset} is
// valid for any given sprite; the renderer dispatches accordingly.
constexpr u32 FX_OFFSET_NONE = 0xFFFFFFFFu;
u32 fx_offset(u8 id);
// Returns the LZ77-compressed-stream pointer in PROGMEM, or nullptr if
// the sprite is stored raw (data() is non-null then). The caller decodes
// via lz77::decode into a RAM buffer, then renders from RAM.
const u8* lz77_data(u8 id);
// Convenience: true iff this sprite is stored LZ77-compressed.
bool is_lz77(u8 id);
// Boss-cache accessor: returns a RAM pointer to the decoded bytes of
// the requested boss, decoding lazily on first call (and on every
// boss-id change — only one boss is alive at a time, so the cache
// holds the active one). Returns nullptr if id isn't a boss.
const u8* ram_data(u8 id);

// Bytes per column for a given height (1 for <=8 tall, 2 for 9-16, ...).
inline u8 pages_for(u8 height) {
  return (u8)((height + 7) / 8);
}

}  // namespace sprites
