// EEPROM-backed storage on the ATmega32u4 (1KB EEPROM).
//
// Layout:
//   0..1   best-run wave (u16)
//   2      best-run max_hp     (u8)
//   3      best-run damage     (u8)
//   4      best-run fire_rate  (u8)
//   5..6   magic 0x4253 ('BR') for best-run
//
//   16..21 meta name[6] (chars, 6-char pilgrim name)
//   22     meta level_hp           (u8)
//   23     meta level_damage       (u8)
//   24     meta level_fire_rate    (u8)
//   25..26 meta sangue_vessel       (u16)
//   27..28 meta total_runs          (u16)
//   29..30 meta total_kills         (u16) — shades only
//   31..34 meta total_sangue_earned (u32)
//   35..37 meta shades[3]        (u8x3)
//   38     meta total_keepers_felled (u8) — added v2; magic bumped to invalidate
//                                            old saves (safer than silent reread)
//   39     meta bullet              (u8) — added v3; equipped bullet tier (0..4)
//   40     meta vestige             (u8) — added v4; pre-class state + 3 classes
//   41     meta burden              (u8) — added v4; level within vestige (0..3)
//   42..43 magic 0x4D46 ('MF') for meta v4
//
// Records are spaced apart (best-run at 0..6, meta starting at 16) so future
// fields don't collide.

#include "storage.h"

#include <avr/eeprom.h>
#include <string.h>

namespace storage {

namespace {
constexpr u16 ADDR_WAVE      = 0;
constexpr u16 ADDR_HP        = 2;
constexpr u16 ADDR_DAMAGE    = 3;
constexpr u16 ADDR_FIRE_RATE = 4;
constexpr u16 ADDR_BR_MAGIC  = 5;
constexpr u16 BR_MAGIC_VAL   = 0x4253;  // 'BR'

constexpr u16 ADDR_META_NAME         = 16;
constexpr u16 ADDR_META_LEVEL_HP     = 22;
constexpr u16 ADDR_META_LEVEL_DMG    = 23;
constexpr u16 ADDR_META_LEVEL_FR     = 24;
constexpr u16 ADDR_META_VESSEL       = 25;
constexpr u16 ADDR_META_TOTAL_RUNS   = 27;
constexpr u16 ADDR_META_TOTAL_KILLS  = 29;
constexpr u16 ADDR_META_TOTAL_SANGUE = 31;
constexpr u16 ADDR_META_SHADES       = 35;  // 3 bytes
constexpr u16 ADDR_META_KEEPERS      = 38;  // u8 — added in v2
constexpr u16 ADDR_META_BULLET       = 39;  // u8 — added in v3
constexpr u16 ADDR_META_VESTIGE      = 40;  // u8 — added in v4
constexpr u16 ADDR_META_BURDEN       = 41;  // u8 — added in v4
constexpr u16 ADDR_META_MAGIC        = 42;  // moved when v4 added vestige+burden
constexpr u16 META_MAGIC_VAL = 0x4D46;  // 'MF' — bumped from 'ME' for v4 (vestige + burden added)
constexpr u16 META_MAGIC_VAL_V3 = 0x4D45;  // 'ME' — v3 magic, kept for migration
}  // namespace

BestRun read_best_run() {
  u16 magic = eeprom_read_word((const u16*)ADDR_BR_MAGIC);
  if (magic != BR_MAGIC_VAL) return BestRun{0, 0, 0, 0};
  BestRun r;
  r.wave      = eeprom_read_word((const u16*)ADDR_WAVE);
  r.hp        = eeprom_read_byte((const u8*)ADDR_HP);
  r.damage    = eeprom_read_byte((const u8*)ADDR_DAMAGE);
  r.fire_rate = eeprom_read_byte((const u8*)ADDR_FIRE_RATE);
  return r;
}

void write_best_run(const BestRun& r) {
  eeprom_update_word((u16*)ADDR_WAVE, r.wave);
  eeprom_update_byte((u8*)ADDR_HP, r.hp);
  eeprom_update_byte((u8*)ADDR_DAMAGE, r.damage);
  eeprom_update_byte((u8*)ADDR_FIRE_RATE, r.fire_rate);
  eeprom_update_word((u16*)ADDR_BR_MAGIC, BR_MAGIC_VAL);
}

MetaCharacter read_meta() {
  u16 magic = eeprom_read_word((const u16*)ADDR_META_MAGIC);
  // v3 → v4 migration: the magic byte moved from ADDR 40 to 42 when
  // we added vestige+burden. If the v4 slot is wrong but the v3 slot
  // (40, the byte we now use for vestige) holds the v3 magic, treat
  // this as a v3 save — read the v3 fields, default vestige+burden
  // to 0 (UNBURDENED), and proceed. The next write_meta will rewrite
  // at the v4 layout. Avoids wiping pre-v4 saves on first v4 launch.
  bool is_v3 = false;
  if (magic != META_MAGIC_VAL) {
    u16 v3_magic = eeprom_read_word((const u16*)40);  // old ADDR_META_MAGIC
    if (v3_magic == META_MAGIC_VAL_V3) {
      is_v3 = true;
    } else {
      // Uninitialized — return all zeros (game treats no-name as "needs entry").
      MetaCharacter m;
      memset(&m, 0, sizeof(m));
      return m;
    }
  }
  MetaCharacter m;
  for (u8 i = 0; i < NAME_LEN; ++i) {
    m.name[i] = (char)eeprom_read_byte((const u8*)(ADDR_META_NAME + i));
  }
  m.level_hp            = eeprom_read_byte((const u8*)ADDR_META_LEVEL_HP);
  m.level_damage        = eeprom_read_byte((const u8*)ADDR_META_LEVEL_DMG);
  m.level_fire_rate     = eeprom_read_byte((const u8*)ADDR_META_LEVEL_FR);
  m.sangue_vessel       = eeprom_read_word((const u16*)ADDR_META_VESSEL);
  m.total_runs          = eeprom_read_word((const u16*)ADDR_META_TOTAL_RUNS);
  m.total_kills         = eeprom_read_word((const u16*)ADDR_META_TOTAL_KILLS);
  m.total_sangue_earned = eeprom_read_dword((const u32*)ADDR_META_TOTAL_SANGUE);
  for (u8 i = 0; i < 3; ++i) {
    m.shades[i] = eeprom_read_byte((const u8*)(ADDR_META_SHADES + i));
  }
  m.total_keepers_felled = eeprom_read_byte((const u8*)ADDR_META_KEEPERS);
  m.bullet               = eeprom_read_byte((const u8*)ADDR_META_BULLET);
  if (is_v3) {
    // v3 didn't have vestige/burden; default to UNBURDENED + 0.
    m.vestige = VESTIGE_UNBURDENED;
    m.burden  = 0;
  } else {
    m.vestige = eeprom_read_byte((const u8*)ADDR_META_VESTIGE);
    m.burden  = eeprom_read_byte((const u8*)ADDR_META_BURDEN);
  }
  return m;
}

void write_meta(const MetaCharacter& m) {
  for (u8 i = 0; i < NAME_LEN; ++i) {
    eeprom_update_byte((u8*)(ADDR_META_NAME + i), (u8)m.name[i]);
  }
  eeprom_update_byte((u8*)ADDR_META_LEVEL_HP, m.level_hp);
  eeprom_update_byte((u8*)ADDR_META_LEVEL_DMG, m.level_damage);
  eeprom_update_byte((u8*)ADDR_META_LEVEL_FR, m.level_fire_rate);
  eeprom_update_word((u16*)ADDR_META_VESSEL, m.sangue_vessel);
  eeprom_update_word((u16*)ADDR_META_TOTAL_RUNS, m.total_runs);
  eeprom_update_word((u16*)ADDR_META_TOTAL_KILLS, m.total_kills);
  eeprom_update_dword((u32*)ADDR_META_TOTAL_SANGUE, m.total_sangue_earned);
  for (u8 i = 0; i < 3; ++i) {
    eeprom_update_byte((u8*)(ADDR_META_SHADES + i), m.shades[i]);
  }
  eeprom_update_byte((u8*)ADDR_META_KEEPERS, m.total_keepers_felled);
  eeprom_update_byte((u8*)ADDR_META_BULLET, m.bullet);
  eeprom_update_byte((u8*)ADDR_META_VESTIGE, m.vestige);
  eeprom_update_byte((u8*)ADDR_META_BURDEN, m.burden);
  eeprom_update_word((u16*)ADDR_META_MAGIC, META_MAGIC_VAL);
}

void wipe_meta() {
  // Zero magic — next read returns blank meta and triggers name entry again.
  eeprom_update_word((u16*)ADDR_META_MAGIC, 0);
}

}  // namespace storage
