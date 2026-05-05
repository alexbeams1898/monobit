// File-backed "EEPROM" simulation. The Arduboy's EEPROM layout is emulated
// 1:1 in a local binary file named "mono.sav" in the working directory.
// Reads/writes are direct byte offsets matching platform/arduboy/storage.cpp.

#include "storage.h"

#include <cstdio>
#include <cstring>

namespace storage {

namespace {

constexpr const char* SAV_PATH = "mono.sav";
constexpr int EEPROM_SIZE      = 1024;  // match ATmega32u4 EEPROM

// EEPROM offsets (must match platform/arduboy/storage.cpp exactly).
constexpr u16 ADDR_WAVE      = 0;
constexpr u16 ADDR_HP        = 2;
constexpr u16 ADDR_DAMAGE    = 3;
constexpr u16 ADDR_FIRE_RATE = 4;
constexpr u16 ADDR_BR_MAGIC  = 5;
constexpr u16 BR_MAGIC_VAL   = 0x4253;  // 'BR'

constexpr u16 ADDR_META_NAME       = 16;
constexpr u16 ADDR_META_LEVEL_HP   = 22;
constexpr u16 ADDR_META_LEVEL_DMG  = 23;
constexpr u16 ADDR_META_LEVEL_RATE = 24;
constexpr u16 ADDR_META_SANGUE     = 25;
constexpr u16 ADDR_META_RUNS       = 27;
constexpr u16 ADDR_META_KILLS      = 29;
constexpr u16 ADDR_META_TOTAL_SG   = 31;
constexpr u16 ADDR_META_SHADES     = 35;
constexpr u16 ADDR_META_KEEPERS    = 38;
constexpr u16 ADDR_META_BULLET     = 39;
constexpr u16 ADDR_META_VESTIGE    = 40;  // u8 — added in v4
constexpr u16 ADDR_META_BURDEN     = 41;  // u8 — added in v4
constexpr u16 ADDR_META_MAGIC      = 42;
constexpr u16 META_MAGIC_VAL = 0x4D46;  // 'MF' — bumped from 'ME' for v4 (vestige + burden added)

u8 cells[EEPROM_SIZE];
bool loaded = false;

void load_once() {
  if (loaded) return;
  std::memset(cells, 0xFF, EEPROM_SIZE);  // match AVR erased-cell default
  FILE* f = std::fopen(SAV_PATH, "rb");
  if (f) {
    std::fread(cells, 1, EEPROM_SIZE, f);
    std::fclose(f);
  }
  loaded = true;
}

void persist() {
  FILE* f = std::fopen(SAV_PATH, "wb");
  if (!f) return;
  std::fwrite(cells, 1, EEPROM_SIZE, f);
  std::fclose(f);
}

u8 r8(u16 a) {
  return cells[a];
}
u16 r16(u16 a) {
  return (u16)(cells[a] | ((u16)cells[a + 1] << 8));
}
u32 r32(u16 a) {
  return (u32)cells[a] | ((u32)cells[a + 1] << 8) | ((u32)cells[a + 2] << 16) |
         ((u32)cells[a + 3] << 24);
}
void w8(u16 a, u8 v) {
  cells[a] = v;
}
void w16(u16 a, u16 v) {
  cells[a]     = (u8)(v & 0xFF);
  cells[a + 1] = (u8)(v >> 8);
}
void w32(u16 a, u32 v) {
  cells[a]     = (u8)(v & 0xFF);
  cells[a + 1] = (u8)((v >> 8) & 0xFF);
  cells[a + 2] = (u8)((v >> 16) & 0xFF);
  cells[a + 3] = (u8)((v >> 24) & 0xFF);
}

}  // namespace

BestRun read_best_run() {
  load_once();
  BestRun b{};
  if (r16(ADDR_BR_MAGIC) != BR_MAGIC_VAL) return b;
  b.wave      = r16(ADDR_WAVE);
  b.hp        = r8(ADDR_HP);
  b.damage    = r8(ADDR_DAMAGE);
  b.fire_rate = r8(ADDR_FIRE_RATE);
  return b;
}

void write_best_run(const BestRun& b) {
  load_once();
  w16(ADDR_WAVE, b.wave);
  w8(ADDR_HP, b.hp);
  w8(ADDR_DAMAGE, b.damage);
  w8(ADDR_FIRE_RATE, b.fire_rate);
  w16(ADDR_BR_MAGIC, BR_MAGIC_VAL);
  persist();
}

MetaCharacter read_meta() {
  load_once();
  MetaCharacter m{};
  if (r16(ADDR_META_MAGIC) != META_MAGIC_VAL) return m;
  for (u8 i = 0; i < NAME_LEN; ++i)
    m.name[i] = (char)cells[ADDR_META_NAME + i];
  m.level_hp            = r8(ADDR_META_LEVEL_HP);
  m.level_damage        = r8(ADDR_META_LEVEL_DMG);
  m.level_fire_rate     = r8(ADDR_META_LEVEL_RATE);
  m.sangue_vessel       = r16(ADDR_META_SANGUE);
  m.total_runs          = r16(ADDR_META_RUNS);
  m.total_kills         = r16(ADDR_META_KILLS);
  m.total_sangue_earned = r32(ADDR_META_TOTAL_SG);
  for (u8 i = 0; i < 3; ++i)
    m.shades[i] = cells[ADDR_META_SHADES + i];
  m.total_keepers_felled = r8(ADDR_META_KEEPERS);
  m.bullet               = r8(ADDR_META_BULLET);
  m.vestige              = r8(ADDR_META_VESTIGE);
  m.burden               = r8(ADDR_META_BURDEN);
  return m;
}

void write_meta(const MetaCharacter& m) {
  load_once();
  for (u8 i = 0; i < NAME_LEN; ++i)
    cells[ADDR_META_NAME + i] = (u8)m.name[i];
  w8(ADDR_META_LEVEL_HP, m.level_hp);
  w8(ADDR_META_LEVEL_DMG, m.level_damage);
  w8(ADDR_META_LEVEL_RATE, m.level_fire_rate);
  w16(ADDR_META_SANGUE, m.sangue_vessel);
  w16(ADDR_META_RUNS, m.total_runs);
  w16(ADDR_META_KILLS, m.total_kills);
  w32(ADDR_META_TOTAL_SG, m.total_sangue_earned);
  for (u8 i = 0; i < 3; ++i)
    cells[ADDR_META_SHADES + i] = m.shades[i];
  w8(ADDR_META_KEEPERS, m.total_keepers_felled);
  w8(ADDR_META_BULLET, m.bullet);
  w8(ADDR_META_VESTIGE, m.vestige);
  w8(ADDR_META_BURDEN, m.burden);
  w16(ADDR_META_MAGIC, META_MAGIC_VAL);
  persist();
}

void wipe_meta() {
  load_once();
  for (u16 a = ADDR_META_NAME; a < ADDR_META_MAGIC + 2; ++a)
    cells[a] = 0xFF;
  persist();
}

}  // namespace storage
