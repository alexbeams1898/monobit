// Persistent storage interface. Implementation per-platform.
//
// On Arduboy this is the chip's 1KB EEPROM. EEPROM is rated ~100k writes
// per cell, so write rules:
//   - Only on death / forfeit
//   - Only when the new value beats the stored value
// At maybe one death per minute of playtime, even sustained play uses a
// trivial fraction of cell life.

#pragma once

#include "types.h"

namespace storage {

// Snapshot of the best run ever achieved on this device. Saved together so
// the game-over screen can show "best run had wave N with these stats."
struct BestRun {
  u16 wave;      // highest wave reached
  u8 hp;         // player's max_hp at that wave
  u8 damage;     // player's damage stat at that wave
  u8 fire_rate;  // player's fire_rate stat at that wave
};

BestRun read_best_run();
void write_best_run(const BestRun& run);

// Vestige — the shape Hell impresses on the vessel. See docs/design/
// "The three classes — three shapes of becoming". Stored as u8 in
// MetaCharacter::vestige; values are stable across save versions
// (don't reorder). UNBURDENED at 0 makes fresh saves default to it
// without needing magic-byte reset.
//
// Name-collision note: the player-facing save system is also called
// VESTIGIA (see docs/design/ "The ledger and the vestigia"). The two
// uses do not mix at runtime — `MetaCharacter::vestige` is always the
// class; the save slots are a separate structure with its own type.
// The collision is intentional-after-the-fact; we chose not to rename
// either side. If you find yourself confused which is which, see the
// "Naming note (collision)" subsection in docs/design/.
constexpr u8 VESTIGE_UNBURDENED = 0;
constexpr u8 VESTIGE_PENITENT   = 1;
constexpr u8 VESTIGE_WRETCHED   = 2;
constexpr u8 VESTIGE_HERETIC    = 3;

// Persistent meta character — survives runs. Players keep one named
// character whose stats grow by spending sangue on permanent upgrades.
// Roguelite shape: each run is still roguelike (you can die wave 1) but
// the character gets stronger over time.
// Name is 6 chars to fit Dante-sized names (VIRGIL, UGOLINO truncated, etc.).
// Not null-terminated — walk exactly NAME_LEN on render. Pad with spaces at
// name entry so short names display cleanly.
constexpr u8 NAME_LEN = 6;

struct MetaCharacter {
  char name[NAME_LEN];      // 6 — set on first boot
  u8 level_hp;              // 1
  u8 level_damage;          // 1
  u8 level_fire_rate;       // 1
  u16 sangue_vessel;        // 2 — current spendable sangue
  u16 total_runs;           // 2
  u16 total_kills;          // 2 — shades only (keepers tracked separately)
  u32 total_sangue_earned;  // 4 — lifetime sangue gathered (Phlegethon)
  u8 total_keepers_felled;  // 1 — lifetime bosses (keepers) felled
  // Bestiary: bit per encountered enemy/boss. Bit index = sprite Id - 6 so
  // ENE_WRAITH (id 6) is bit 0, BOSS_LUCIFER (id 26) is bit 20. 3 bytes
  // (24 bits) holds 21 entries with room for 3 more.
  u8 shades[3];  // 3
  // Equipped bullet tier (0..4). The Pilgrim calls them all "bullets" —
  // see docs/design/ — but Hell knows them by their canto-coded names:
  //   0 STONE  (Wastrels, Canto VII)        — default
  //   1 ARROW  (Centaurs, Canto XII)
  //   2 HOOK   (Malebranche, Canto XXI)
  //   3 SHARD  (Ice Traitors, Canto XXXII)
  //   4 WIND   (Lust Spirits, Canto V)
  u8 bullet;  // 1
  // Vestige — the shape Hell impresses on the vessel. Three classes
  // plus the pre-class wraith state. Set on first boot to UNBURDENED;
  // flipped to one of the three by class-select after the wave-1
  // Charon defeat. See docs/design/ "The three classes".
  //   0 UNBURDENED — pre-class wraith, no shape yet
  //   1 PENITENT   — Vita-focused, fixates last (PILGRIM/BEARER/MANTLE)
  //   2 WRETCHED   — Furia-focused, never fixates (VAGRANT/STING/WIND)
  //   3 HERETIC    — Ira-focused, fixates first (APOSTATE/ZEALOT/TOMB)
  u8 vestige;  // 1
  // Burden level — 0 means unburdened (no shape installed yet); 1..3
  // are the three rungs of becoming within the chosen vestige. Read
  // alongside meta.vestige to resolve the level-name (PILGRIM, etc.)
  // and the in-game sprite frame set.
  u8 burden;  // 1
};  // 26 bytes

MetaCharacter read_meta();
void write_meta(const MetaCharacter& m);

// Reset the meta character (e.g. after user picks "RESET" in a future menu).
void wipe_meta();

// Size guards: EEPROM offsets in platform/arduboy/storage.cpp are hand-picked
// against the current struct layouts. If anyone adds a field, the in-memory
// struct grows but the EEPROM layout doesn't — producing silent mis-reads
// across versions. Break the build so the two must be updated in lockstep.
//
// AVR-only: on PC, struct padding differs (u16/u32 get padded to 4-byte
// alignment), and the PC storage backend reads byte-by-byte from a file
// anyway — it doesn't blit the whole struct. Enforcing the AVR sizes on
// PC would fail a useful check for no reason.
#ifdef __AVR__
static_assert(sizeof(BestRun) == 5, "BestRun size changed -- update EEPROM offsets");
static_assert(sizeof(MetaCharacter) == 26, "MetaCharacter size changed -- update EEPROM offsets");
#endif

}  // namespace storage
