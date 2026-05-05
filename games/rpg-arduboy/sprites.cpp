#include "sprites.h"

#include "data_offsets.h"
#include "lz77.h"
#include "progmem.h"

namespace sprites {

// ---------------------------------------------------------------- player ---
// Player class portraits + in-world tokens (3 classes × 3 levels × 2
// size-tiers) all live on FX flash — see SP_PENITENT_L1..SP_HERETIC_L3
// (portraits) and SP_PENITENT_L1_WORLD..SP_HERETIC_L3_WORLD (tokens) in
// sprites.h. Bytes are baked from games/rpg/sprites.toml into
// data/fx/player/*.bin and packed into data.bin by tools/fxdata/build.py.
// Penitent L1 in both size-tiers aliases the touched-up
// pen_l1_idle_f0_data (the canonical L1 sprite) — see fx_offset()
// dispatch below.
//
// PLAYER (id 0) is dead-coded for rendering — game.cpp's PLAYING render
// loop dispatches via anim_frame_sprite_id() to the FX anim frames
// (PEN_L1_IDLE_F0..PEN_L1_ATTACK_F1) directly. The id 0 slot stays as a
// pool-identity placeholder so entity::kind == ent::PLAYER still works.
// Width/height for id 0 = the f0 frame's 12x16 so any incidental
// sprites::width(PLAYER) call returns sensible numbers.

// Penitent L1 world animation frames migrated to FX flash. The bytes
// live in art/animations/.../strip.png, baked into data.bin by the
// spritebake bake step (see tools/fxdata/manifest.txt). Game code
// reads them via sprites::fx_offset(id) + draw_fx_sprite. Net effect:
// 192 B PROGMEM freed; sprite count can grow without ceiling pressure.
// Legacy 5x8 humanoid (was originally enemy_data; swapped roles long
// ago). Kept around in case we want to A/B against the small sprite.
// Not currently referenced by the DATA[] table.
const u8 player_data[5] PROGMEM = {
    0x94,  // col 0  (left arm + left foot)
    0x7F,  // col 1
    0x1F,  // col 2  (head/torso center, no legs)
    0x7F,  // col 3
    0x94,  // col 4  (right arm + right foot)
};

// ----------------------------------------------- legacy enemy fallback ---
// Slot kept for ID stability (sprites::ENEMY = 1 is referenced by index in
// downstream tables). Payload trimmed to a single 0 byte — the renderer
// never draws this anymore, but we can't shift the ID without touching
// every sprite-id-arithmetic site.
const u8 enemy_data[1] PROGMEM = {0};

// ----------------------------------------------------------------- bullet ---
const u8 bullet_data[2] PROGMEM = {
    0b00011000,
    0b00011000,
};

// ----------------------------------------------------------- enemy projectiles
// Pilgrim fires BULLET (modern intrusion); damned shades hurl what their
// canto prescribes. STONE is the shared fallback (Canto VII's hoarders and
// wastrels roll stones); the four named shapes are circle-specific overrides
// fired by the matching enemy. All are centered on rows 2-4 of the 8-tall
// byte so they render at the same vertical band as BULLET.
const u8 dart_stone_data[3] PROGMEM = {0x08, 0x1C, 0x18};        // 3x3 chunk
const u8 dart_arrow_data[4] PROGMEM = {0x10, 0x18, 0x18, 0x3C};  // 4w horizontal with head
const u8 dart_hook_data[3] PROGMEM  = {0x1C, 0x04, 0x1C};        // 3x3 barb
const u8 dart_shard_data[1] PROGMEM = {0x1C};                    // 1x3 icicle
const u8 dart_wind_data[3] PROGMEM  = {0x08, 0x08, 0x08};        // 3x1 streak

// ----------------------------------------------------------------- sangue ---
// Escalating blood pickups, tier N+1 adds pixels to tier N (additive-evolution
// chain — the pilgrim reads "more blood" at a glance). All 8x8, column-major,
// bit 0 = row 0 (top). Drop -> rivulet -> river -> flood -> deluge.
//
// Tier 1 (1-4 sangue): 3x5 teardrop, apex up. Byte-identical to the font's
// '~' glyph (engine/font.cpp) — single canonical sangue mark used for
// counters, pickups, and the app icon. Tier 2..5 keep 8x8 escalation art.
const u8 sangue_drop_data[3] PROGMEM = {
    0x0C,
    0x1F,
    0x0C,
};
// Tier 2 (5-9): teardrop + trailing dribble.
const u8 sangue_rivulet_data[8] PROGMEM = {
    0x00, 0x00, 0x98, 0xFC, 0x30, 0x00, 0x00, 0x00,
};
// Tier 3 (10-24): widened stream with a crown reaching up.
const u8 sangue_river_data[8] PROGMEM = {
    0x00, 0xA0, 0xB8, 0xFE, 0xB0, 0x20, 0x00, 0x00,
};
// Tier 4 (25-99): cresting wave, wide banks.
const u8 sangue_flood_data[8] PROGMEM = {
    0xA0, 0xF8, 0xFC, 0xFF, 0xFF, 0xFE, 0x30, 0x00,
};
// Tier 5 (100+, boss drop): drowning mass, rounded top & bottom.
const u8 sangue_deluge_data[8] PROGMEM = {
    0x7E, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0x7E,
};

// ----------------------------------------------------------------- logos ---
// 123x12 BULLETS IN HELL (Optimus @ 16px, no supersample — byte-identical letters).
// logo_title_LZ77: 246 -> 122 B  (50%)
// Lossless byte-for-byte; decode via lz77::decode at logo render time.
const u8 logo_title_LZ77[122] PROGMEM = {
    0x0D, 0x01, 0xFF, 0x21, 0x21, 0x33, 0x4E, 0xC0, 0x80, 0x00, 0x01, 0xFF, 0x01, 0x00, 0x81, 0x00,
    0x81, 0x07, 0x02, 0x01, 0xFF, 0x82, 0x0A, 0x88, 0x07, 0x01, 0x21, 0x80, 0x00, 0x80, 0x1B, 0x80,
    0x00, 0x01, 0xFF, 0x81, 0x04, 0x08, 0x00, 0x00, 0x0E, 0x11, 0x21, 0x61, 0xC1, 0x83, 0x82, 0x31,
    0x82, 0x2C, 0x06, 0xFF, 0x06, 0x0C, 0x18, 0x30, 0x60, 0x80, 0x47, 0x86, 0x3A, 0x01, 0x20, 0x82,
    0x00, 0x01, 0x21, 0x81, 0x4A, 0x86, 0x3F, 0x8A, 0x57, 0x03, 0x08, 0x0F, 0x08, 0x80, 0x00, 0x07,
    0x04, 0x03, 0x00, 0x00, 0x03, 0x04, 0x00, 0x81, 0x0A, 0x01, 0x06, 0x80, 0x81, 0x83, 0x14, 0x8F,
    0x07, 0x83, 0x2F, 0x82, 0xA1, 0x01, 0x0C, 0x84, 0x35, 0x81, 0x41, 0x85, 0x04, 0x81, 0xB8, 0x01,
    0x03, 0x84, 0x08, 0x85, 0x27, 0x82, 0x2F, 0x93, 0x4F, 0xC0,
};

// SECOND DEATH logo (121x12 — Optimus @ 16px, narrowed 2 px + canonical S).
// LZ77-compressed bytes migrated to FX flash 2026-04-26 — see
// tools/fxdata/manifest.txt's SECOND_DEATH_LZ77 entry. Net effect:
// 157 B PROGMEM freed. Decode path: lz77::decode_from_fx into the
// shared LZ77_CACHE (same path TITLE_LZ77 already uses).

// ---------------------------------------------- Dante base enemies (8x8) ---
const u8 ene_wraith_data[8] PROGMEM      = {0x0C, 0x12, 0x55, 0xB5, 0xB1, 0x55, 0x16, 0x08};
const u8 ene_lust_spirit_data[8] PROGMEM = {0x1C, 0x36, 0x6B, 0x49, 0xC9, 0xEB, 0x36, 0x1C};
const u8 ene_worm_data[8] PROGMEM        = {0x1C, 0x22, 0x3E, 0x22, 0x3E, 0x22, 0x3E, 0x00};
// SWINE: face-on hog. Symmetric. Two negative-space gaps in row 3 are the
// nostrils — they break the snout band so the silhouette reads as a pig
// rather than a generic blob. Four short stub legs at the bottom corners.
const u8 ene_swine_data[8] PROGMEM   = {0xDC, 0x3E, 0x37, 0x3F, 0x3F, 0x37, 0x3E, 0xDC};
const u8 ene_jouster_data[8] PROGMEM = {0x08, 0x3E, 0x1C, 0x7F, 0x5C, 0xC8, 0xC0, 0x80};
// PROFLIGATE: squanderer of Greed (Canto VII). Mirror of JOUSTER — the
// hoarders and prodigals roll their weights against each other in
// opposite directions until they meet, crash, and turn around.
const u8 ene_profligate_data[8] PROGMEM = {0x80, 0xC0, 0xC8, 0x5C, 0x7F, 0x1C, 0x3E, 0x08};
const u8 ene_brawler_data[8] PROGMEM    = {0xB0, 0xE8, 0x1A, 0x3F, 0x3F, 0x1A, 0xE8, 0xB0};
const u8 ene_tomb_shade_data[8] PROGMEM = {0x9C, 0xDE, 0xFB, 0xB9, 0xB9, 0xFB, 0xDE, 0x9C};
const u8 ene_harpy_data[8] PROGMEM      = {0x03, 0xE6, 0xB5, 0x1F, 0x1F, 0xB5, 0xE6, 0x03};
// CENTAUR: 4-legged enforcer of Phlegethon (Canto XII). Patrols the
// bloody river firing arrows at any soul that surfaces. Small head at
// top-right, long horse back across the middle, four distinct legs.
const u8 ene_centaur_data[8] PROGMEM     = {0xD0, 0x38, 0xFA, 0x3A, 0x3E, 0xFE, 0x3F, 0xC5};
const u8 ene_malebranche_data[8] PROGMEM = {0xF0, 0x9F, 0x95, 0x9F, 0xCC, 0xC8, 0xC8, 0xF0};
const u8 ene_ice_traitor_data[8] PROGMEM = {0x88, 0xBD, 0xF6, 0xDF, 0xDF, 0xF6, 0xBD, 0x88};

// ---------------------------------------------- Dante bosses (raw, on FX flash) ---
// Bosses moved from PROGMEM-LZ77 storage to FX flash 2026-04-26.
// Each boss is now a raw byte image at the per-boss target dimensions
// (see WIDTHS/HEIGHTS tables below). The bytes live in
// data/fx/bosses/boss_*_data.bin, baked into build/fxdata/data.bin
// by tools/fxdata/build.py (manifest entries `BOSS_*`).
//
// At runtime sprites::fx_offset(id) returns the byte offset; the
// renderer reads via data_flash::read into a stack buffer (small
// bosses) or the shared LZ77 cache (Lucifer at 60x58 = 480 B exceeds
// the small-buffer ceiling). The cache stays in .bss for the logos
// and now also doubles as the large-boss read buffer.
//
// Saves ~1.4 KB internal flash that was holding the LZ77 streams.

// ----------------------------------------------------------------- tables ---
// All three metadata tables live in PROGMEM (flash on AVR, rodata on PC)
// so they don't eat RAM. Access only through width() / height() / data().

namespace {

// Boss widths/heights match sprites.toml portrait sizes — every boss has
// its own bust dimensions now, not a shared 16x16. Order is sprite-id order:
// charon, minos, cerberus, plutus, phlegyas, medusa, minotaur, geryon, lucifer.
const u8 WIDTHS[COUNT] PROGMEM = {
    12,   // PLAYER (id 0, dead-coded — uses anim frames at render time)
    0,    // ENEMY (decommissioned slot)
    2,    // BULLET
    3,    // SANGUE_DROP (tier 1, 3x5)
    123,  // LOGO_TITLE
    121,  // LOGO_SECOND_DEATH
    8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8,  // 12 base enemies
    36, 60, 58, 62, 40, 52, 62, 64, 60,  // 9 bosses (FX, threshold-only sizes)
    8, 8, 8, 8,                          // sangue tiers 2..5
    3, 4, 3, 1, 3,                       // darts: STONE ARROW HOOK SHARD WIND
    12, 12,                              // PEN_L1_IDLE_F0..F1
    12, 12, 12, 12,                      // PEN_L1_WALK_F0..F3
    12, 12,                              // PEN_L1_ATTACK_F0..F1
    // Player class portraits (FX). All 9 portraits source from
    // art/maincharevo.png at aspect-corrected dims; world tokens below
    // alias the touched-up idle f0 frame for Penitent L1 only.
    26, 45, 53,  // SP_PENITENT_L1..L3
    30, 45, 62,  // SP_WRETCHED_L1..L3
    28, 38, 54,  // SP_HERETIC_L1..L3
    // Player class world tokens (FX). PENITENT L1 WORLD aliases the same
    // f0 frame as PENITENT L1 portrait.
    12, 16, 20,      // SP_PENITENT_L1_WORLD..L3_WORLD
    12, 16, 20,      // SP_WRETCHED_L1_WORLD..L3_WORLD
    12, 16, 20,      // SP_HERETIC_L1_WORLD..L3_WORLD
    40,              // SP_UNBURDENED (40x52 wraith)
    16,              // SP_UNBURDENED_WORLD (16x20 wraith token)
    16, 16,          // UB_IDLE_F0..F1
    16, 16, 16, 16,  // UB_WALK_F0..F3
    16, 16,          // UB_ATTACK_F0..F1
};

// Heights are the *visible* extents — what collision and rendering layout
// math both consume. For projectiles whose storage byte is 8 tall but only
// rows 2-4 are lit (BULLET, all DART_*), the published height is the
// visible 2-3 px. The renderer's `pages_for(height)` still computes one
// page (height <= 8 → 1 page), so this doesn't change what's blitted; it
// just makes collision tighter without needing a special-case override
// in entity_h.
const u8 HEIGHTS[COUNT] PROGMEM = {
    16,  // PLAYER (id 0, dead-coded — uses anim frames at render time)
    0,   // ENEMY (decommissioned slot)
    2,   // BULLET (visible 2px)
    5,   // SANGUE_DROP (tier 1, 3x5)
    12,  // LOGO_TITLE
    12,  // LOGO_SECOND_DEATH
    8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8,  // 12 base enemies
    48, 48, 48, 48, 54, 46, 50, 40, 58,  // 9 bosses (FX, threshold-only sizes)
    8, 8, 8, 8,                          // sangue tiers 2..5
    3, 3, 3, 3, 3,                       // darts: visible ~3 px
    16, 16,                              // PEN_L1_IDLE_F0..F1
    16, 16, 16, 16,                      // PEN_L1_WALK_F0..F3
    16, 16,                              // PEN_L1_ATTACK_F0..F1
    // Player class portrait heights — anchored to existing declared
    // heights, widths derived from natural source aspect ratios so the
    // figures don't get squashed during downscale.
    30, 46, 64,  // SP_PENITENT_L1..L3
    28, 42, 56,  // SP_WRETCHED_L1..L3
    40, 52, 60,  // SP_HERETIC_L1..L3
    // Player class world tokens.
    16, 20, 24,      // SP_PENITENT_L1_WORLD..L3_WORLD
    16, 20, 24,      // SP_WRETCHED_L1_WORLD..L3_WORLD
    16, 20, 24,      // SP_HERETIC_L1_WORLD..L3_WORLD
    52,              // SP_UNBURDENED (40x52 wraith-form, hand-traced)
    20,              // SP_UNBURDENED_WORLD (16x20 wraith token, hand-traced)
    20, 20,          // UB_IDLE_F0..F1
    20, 20, 20, 20,  // UB_WALK_F0..F3
    20, 20,          // UB_ATTACK_F0..F1
};

// Table of sprite-data pointers. Each pointer still points into PROGMEM
// (the underlying sprite byte arrays). The pointer table ITSELF also
// lives in PROGMEM so we don't spend 62 bytes of RAM on 31 pointers.
//
// LZ77-compressed sprites (logos + 9 bosses) have nullptr here and
// live in the parallel LZ77 table accessed via lz77_data(). Render
// callers should check data() first — if non-null, render direct from
// PROGMEM; if null, take the LZ77 path (logos decode-and-render in
// place, bosses decode into a shared RAM cache via ensure_ram()).
const u8* const DATA[COUNT] PROGMEM = {
    nullptr,  // PLAYER (id 0, dead-coded — render path uses anim FX frames)
    enemy_data,
    bullet_data,
    sangue_drop_data,
    nullptr,
    nullptr,  // LOGO_TITLE, LOGO_SECOND_DEATH (LZ77)
    ene_wraith_data,
    ene_lust_spirit_data,
    ene_worm_data,
    ene_swine_data,
    ene_jouster_data,
    ene_profligate_data,
    ene_brawler_data,
    ene_tomb_shade_data,
    ene_harpy_data,
    ene_centaur_data,
    ene_malebranche_data,
    ene_ice_traitor_data,
    nullptr,
    nullptr,
    nullptr,
    nullptr,
    nullptr,
    nullptr,
    nullptr,
    nullptr,
    nullptr,  // 9 bosses (FX-flash; render via fx_offset + draw_fx_sprite)
    sangue_rivulet_data,
    sangue_river_data,
    sangue_flood_data,
    sangue_deluge_data,
    dart_stone_data,
    dart_arrow_data,
    dart_hook_data,
    dart_shard_data,
    dart_wind_data,
    nullptr,  // PEN_L1_IDLE_F0 (FX)
    nullptr,  // PEN_L1_IDLE_F1 (FX)
    nullptr,  // PEN_L1_WALK_F0 (FX)
    nullptr,  // PEN_L1_WALK_F1 (FX)
    nullptr,  // PEN_L1_WALK_F2 (FX)
    nullptr,  // PEN_L1_WALK_F3 (FX)
    nullptr,  // PEN_L1_ATTACK_F0 (FX)
    nullptr,  // PEN_L1_ATTACK_F1 (FX)
    nullptr,
    nullptr,
    nullptr,  // SP_PENITENT_L1..L3 (FX)
    nullptr,
    nullptr,
    nullptr,  // SP_WRETCHED_L1..L3 (FX)
    nullptr,
    nullptr,
    nullptr,  // SP_HERETIC_L1..L3 (FX)
    nullptr,
    nullptr,
    nullptr,  // SP_PENITENT_L1_WORLD..L3_WORLD (FX)
    nullptr,
    nullptr,
    nullptr,  // SP_WRETCHED_L1_WORLD..L3_WORLD (FX)
    nullptr,
    nullptr,
    nullptr,  // SP_HERETIC_L1_WORLD..L3_WORLD (FX)
    nullptr,  // SP_UNBURDENED (FX, hand-traced wraith)
    nullptr,  // SP_UNBURDENED_WORLD (FX, hand-traced wraith token)
    nullptr,
    nullptr,  // UB_IDLE_F0..F1 (FX)
    nullptr,
    nullptr,
    nullptr,
    nullptr,  // UB_WALK_F0..F3 (FX)
    nullptr,
    nullptr,  // UB_ATTACK_F0..F1 (FX)
};

// FX-flash byte offsets for the sprite IDs that ARE on FX. Indexed by
// (id - PEN_L1_IDLE_F0). Sparse — only the contiguous FX-bound range
// has entries. PROGMEM-resident u16 offsets (data image < 64 KB so 16
// bits is enough for years of growth). 16 B for 8 sprites vs 176 B if
// we'd kept a full COUNT-wide u32 table.
// Two FX-bound id ranges: bosses (BOSS_CHARON..BOSS_LUCIFER) and
// the penitent L1 anim frames. Each range has its own table to keep
// per-table memory tight as ranges sparse-hop around the id space.
// As more sprites migrate (other class anims, portraits) we'll either
// add more range tables or unify into a single sorted lookup. For now
// the dispatch is a couple of compares per frame — negligible.
const u16 FX_OFFSETS_BOSS[9] PROGMEM = {
    (u16)fxdata::OFFSET_BOSS_CHARON,   (u16)fxdata::OFFSET_BOSS_MINOS,
    (u16)fxdata::OFFSET_BOSS_CERBERUS, (u16)fxdata::OFFSET_BOSS_PLUTUS,
    (u16)fxdata::OFFSET_BOSS_PHLEGYAS, (u16)fxdata::OFFSET_BOSS_MEDUSA,
    (u16)fxdata::OFFSET_BOSS_MINOTAUR, (u16)fxdata::OFFSET_BOSS_GERYON,
    (u16)fxdata::OFFSET_BOSS_LUCIFER,
};
const u16 FX_OFFSETS_PEN_ANIM[8] PROGMEM = {
    (u16)fxdata::OFFSET_PEN_L1_IDLE_F0,   (u16)fxdata::OFFSET_PEN_L1_IDLE_F1,
    (u16)fxdata::OFFSET_PEN_L1_WALK_F0,   (u16)fxdata::OFFSET_PEN_L1_WALK_F1,
    (u16)fxdata::OFFSET_PEN_L1_WALK_F2,   (u16)fxdata::OFFSET_PEN_L1_WALK_F3,
    (u16)fxdata::OFFSET_PEN_L1_ATTACK_F0, (u16)fxdata::OFFSET_PEN_L1_ATTACK_F1,
};
// Unburdened world anim frames — same layout/order as the Penitent
// table (idle x2, walk x4, attack x2). Indexed by (id - UB_IDLE_F0).
const u16 FX_OFFSETS_UB_ANIM[8] PROGMEM = {
    (u16)fxdata::OFFSET_UB_IDLE_F0,   (u16)fxdata::OFFSET_UB_IDLE_F1,
    (u16)fxdata::OFFSET_UB_WALK_F0,   (u16)fxdata::OFFSET_UB_WALK_F1,
    (u16)fxdata::OFFSET_UB_WALK_F2,   (u16)fxdata::OFFSET_UB_WALK_F3,
    (u16)fxdata::OFFSET_UB_ATTACK_F0, (u16)fxdata::OFFSET_UB_ATTACK_F1,
};

// Player class portraits + world tokens. Indexed by (id - SP_CLASS_FIRST):
// portraits 0..8 (penitent L1..L3, wretched L1..L3, heretic L1..L3),
// world tokens 9..17 (same order).
//
// Penitent L1 *world token* aliases OFFSET_PEN_L1_IDLE_F0 — the
// touched-up idle frame is the canonical L1 in-world sprite. The
// PORTRAIT (SP_PENITENT_L1) reads its own 26×30 maincharevo bake so
// Reckoning + Guide get a properly-sized portrait next to the other
// 8 class portraits. The other 16 entries point at their own bins
// under data/fx/player/.
const u16 FX_OFFSETS_PLAYER[18] PROGMEM = {
    // portraits
    (u16)fxdata::OFFSET_PLAYER_PENITENT_LVL1,  // SP_PENITENT_L1 (26×30 maincharevo)
    (u16)fxdata::OFFSET_PLAYER_PENITENT_LVL2,  // SP_PENITENT_L2
    (u16)fxdata::OFFSET_PLAYER_PENITENT_LVL3,  // SP_PENITENT_L3
    (u16)fxdata::OFFSET_PLAYER_WRETCHED_LVL1,  // SP_WRETCHED_L1
    (u16)fxdata::OFFSET_PLAYER_WRETCHED_LVL2,  // SP_WRETCHED_L2
    (u16)fxdata::OFFSET_PLAYER_WRETCHED_LVL3,  // SP_WRETCHED_L3
    (u16)fxdata::OFFSET_PLAYER_HERETIC_LVL1,   // SP_HERETIC_L1
    (u16)fxdata::OFFSET_PLAYER_HERETIC_LVL2,   // SP_HERETIC_L2
    (u16)fxdata::OFFSET_PLAYER_HERETIC_LVL3,   // SP_HERETIC_L3
    // world tokens
    (u16)fxdata::OFFSET_PEN_L1_IDLE_F0,              // SP_PENITENT_L1_WORLD (alias)
    (u16)fxdata::OFFSET_PLAYER_PENITENT_LVL2_WORLD,  // SP_PENITENT_L2_WORLD
    (u16)fxdata::OFFSET_PLAYER_PENITENT_LVL3_WORLD,  // SP_PENITENT_L3_WORLD
    (u16)fxdata::OFFSET_PLAYER_WRETCHED_LVL1_WORLD,  // SP_WRETCHED_L1_WORLD
    (u16)fxdata::OFFSET_PLAYER_WRETCHED_LVL2_WORLD,  // SP_WRETCHED_L2_WORLD
    (u16)fxdata::OFFSET_PLAYER_WRETCHED_LVL3_WORLD,  // SP_WRETCHED_L3_WORLD
    (u16)fxdata::OFFSET_PLAYER_HERETIC_LVL1_WORLD,   // SP_HERETIC_L1_WORLD
    (u16)fxdata::OFFSET_PLAYER_HERETIC_LVL2_WORLD,   // SP_HERETIC_L2_WORLD
    (u16)fxdata::OFFSET_PLAYER_HERETIC_LVL3_WORLD,   // SP_HERETIC_L3_WORLD
};

// LZ77-decode cache. Shared between bosses (one alive at a time) and
// logos (one rendered at a time). Sized for the largest LZ77 sprite —
// Lucifer at 46x44 = 46 cols * 6 pages = 276 B. Logos are smaller
// (246 B for LOGO_TITLE, 242 B for LOGO_SECOND_DEATH) and fit too.
//
// History: logos previously decoded into a 256 B stack-local buffer in
// draw_logo_lz77_inverse. After the BOSS_CACHE landed (276 B in .bss),
// stack headroom shrunk to ~424 B and the second-death screen's
// stack-buffer allocation collided with .bss, freezing the device.
// Single shared .bss cache solves both: stack stays small, and the
// cache is reused since boss render and logo render never co-occur
// (PLAYING-only vs TITLE/SECOND_DEATH-only).
constexpr u16 LZ77_CACHE_SIZE = 276;
u8 LZ77_CACHE[LZ77_CACHE_SIZE];
u8 cached_id = 0xFF;

}  // namespace

u8 width(u8 id) {
  return pgm_read_byte(&WIDTHS[id]);
}
u8 height(u8 id) {
  return pgm_read_byte(&HEIGHTS[id]);
}
const u8* data(u8 id) {
  // AVR pointers are 16-bit (word-addressable flash). Reading the pointer
  // table gives us a flash address, which is exactly what the sprite
  // renderers expect — they read the sprite bytes via pgm_read_byte.
  // Returns nullptr for LZ77 sprites (logos + 9 bosses) AND for sprites
  // stored in FX flash (player anim frames); callers must dispatch to
  // the right path:
  //   - data() non-null   → PROGMEM, render via draw_progmem_sprite
  //   - lz77_data() ditto → LZ77, decode-then-render
  //   - fx_offset() < ~   → FX flash, draw_fx_sprite
  return (const u8*)pgm_read_ptr(&DATA[id]);
}

u32 fx_offset(u8 id) {
  // FX-bound ids dispatch into one of the per-range tables. Tables hold
  // u16 offsets (data image < 64 KB); we widen to u32 to match
  // data_flash::read. Out-of-range ids return FX_OFFSET_NONE so the
  // renderer falls back to PROGMEM (data() non-null) or LZ77 (logos).
  if (id >= BOSS_CHARON && id <= BOSS_LUCIFER) {
    return (u32)pgm_read_word(&FX_OFFSETS_BOSS[id - BOSS_CHARON]);
  }
  if (id >= PEN_L1_IDLE_F0 && id <= PEN_L1_ATTACK_F1) {
    return (u32)pgm_read_word(&FX_OFFSETS_PEN_ANIM[id - PEN_L1_IDLE_F0]);
  }
  if (id >= SP_CLASS_FIRST && id <= SP_HERETIC_L3_WORLD) {
    return (u32)pgm_read_word(&FX_OFFSETS_PLAYER[id - SP_CLASS_FIRST]);
  }
  // Unburdened (pre-class wraith) — two size tiers, no range table.
  if (id == SP_UNBURDENED) {
    return fxdata::OFFSET_PLAYER_UNBURDENED;
  }
  if (id == SP_UNBURDENED_WORLD) {
    return fxdata::OFFSET_PLAYER_UNBURDENED_WORLD;
  }
  if (id >= UB_IDLE_F0 && id <= UB_ATTACK_F1) {
    return (u32)pgm_read_word(&FX_OFFSETS_UB_ANIM[id - UB_IDLE_F0]);
  }
  return FX_OFFSET_NONE;
}
// LZ77 path: only LOGO_TITLE remains as a PROGMEM-resident LZ77 stream.
// LOGO_SECOND_DEATH migrated to FX flash 2026-04-26 — its bytes flow
// through lz77::decode_from_fx instead of lz77::decode. Bosses also live
// on FX (raw, no LZ77 wrapper) and dispatch via fx_offset() above.
const u8* lz77_data(u8 id) {
  if (id == LOGO_TITLE) return logo_title_LZ77;
  return nullptr;
}
bool is_lz77(u8 id) {
  return id == LOGO_TITLE || id == LOGO_SECOND_DEATH;
}

// Lazy LZ77 decode into the shared cache. Both surviving LZ77 sprites
// (LOGO_TITLE PROGMEM, LOGO_SECOND_DEATH FX) decode into LZ77_CACHE on
// first read, and the cache hits on subsequent reads of the same id.
// The two paths split only on byte-source: PROGMEM via lz77::decode,
// FX via lz77::decode_from_fx.
const u8* ram_data(u8 id) {
  if (!is_lz77(id)) return nullptr;
  if (cached_id != id) {
    if (id == LOGO_TITLE) {
      lz77::decode(logo_title_LZ77, LZ77_CACHE);
    } else if (id == LOGO_SECOND_DEATH) {
      lz77::decode_from_fx(fxdata::OFFSET_SECOND_DEATH_LZ77, LZ77_CACHE);
    }
    cached_id = id;
  }
  return LZ77_CACHE;
}

}  // namespace sprites
