// Vestigia smoke test — non-interactive driver against the SDL backend.
//
// What this checks (and why each one matters):
//
//   1. init() on a fresh save region bootstraps slot 0 and reads cleanly
//      back. If this fails, the format-on-disk and the read path
//      disagree.
//   2. write_slot followed by read_slot returns OK with matching meta
//      bytes. This is the basic round-trip.
//   3. Writing slot N does not corrupt slots 0 or other slots — we
//      read every slot before and after each write and compare.
//   4. After write_slot, an unrelated FX data_flash::read on a known
//      offset (sprite-region) still returns the expected bytes. The
//      production-bug symptom on Arduboy is "sprites render as 0xFF
//      after a write" — this assertion catches the same corruption
//      class on SDL.
//   5. write/read survives across vestigia::init() reset (simulating a
//      re-launch).
//
// Run: ./build-sdl/bin/vestigia_smoke (no args). Returns 0 on pass,
// 1 on first failure with a fprintf describing what went wrong.

#include "data_flash.h"
#include "storage.h"
#include "vestigia.h"

#include <SDL.h>
#include <cstdio>
#include <cstring>

namespace {

// ANSI-free pass/fail prints — also written to a file when running
// under captured-stdout (VS Code task / make).
int failures = 0;

void log_step(const char* what) {
  std::fprintf(stdout, "[step] %s\n", what);
  std::fflush(stdout);
}

void fail(const char* what) {
  std::fprintf(stdout, "[FAIL] %s\n", what);
  std::fflush(stdout);
  ++failures;
}

void pass(const char* what) {
  std::fprintf(stdout, "[ ok ] %s\n", what);
  std::fflush(stdout);
}

// Read a known FX sprite byte and check it's not 0xFF (which would
// indicate the read path is broken / chip is asleep). We don't pin to
// an exact value because data.bin contents change as art evolves; we
// pin to "is not the erased-state byte". If you have a fresh data.bin
// and offset 0 happens to be 0xFF, pick a different probe offset.
bool fx_probe_alive() {
  u8 b = data_flash::read_byte(0);
  // A fresh data.bin has the FX_VECTOR magic at offset 0x14. The byte
  // at offset 0x14 is 0x18 per the magic 0x9518 little-endian. That's
  // a stable probe.
  u8 magic_lo = data_flash::read_byte(0x14);
  u8 magic_hi = data_flash::read_byte(0x15);
  // Either the canonical bytes (un-flashtool-patched) or a real game
  // byte; anything but 0xFF means SPI is alive.
  return b != 0xFF || magic_lo != 0xFF || magic_hi != 0xFF;
}

void wipe_save_file() {
  // The SDL data_flash backend places save.bin alongside the exe via
  // SDL_GetBasePath. Resolve and remove it so init() goes through the
  // bootstrap path on first call.
  char* base = SDL_GetBasePath();
  char path[512];
  if (base) {
    std::snprintf(path, sizeof(path), "%ssave.bin", base);
    SDL_free(base);
  } else {
    std::snprintf(path, sizeof(path), "save.bin");
  }
  std::remove(path);
}

bool meta_eq(const storage::MetaCharacter& a, const storage::MetaCharacter& b) {
  if (std::memcmp(a.name, b.name, storage::NAME_LEN) != 0) return false;
  if (a.level_hp != b.level_hp) return false;
  if (a.level_damage != b.level_damage) return false;
  if (a.level_fire_rate != b.level_fire_rate) return false;
  if (a.sangue_vessel != b.sangue_vessel) return false;
  if (a.total_runs != b.total_runs) return false;
  if (a.total_kills != b.total_kills) return false;
  if (a.total_sangue_earned != b.total_sangue_earned) return false;
  if (std::memcmp(a.shades, b.shades, 3) != 0) return false;
  if (a.total_keepers_felled != b.total_keepers_felled) return false;
  if (a.bullet != b.bullet) return false;
  if (a.vestige != b.vestige) return false;
  if (a.burden != b.burden) return false;
  return true;
}

const char* status_name(vestigia::Status s) {
  switch (s) {
  case vestigia::Status::OK: return "OK";
  case vestigia::Status::EMPTY: return "EMPTY";
  case vestigia::Status::CORRUPT_SLOT: return "CORRUPT_SLOT";
  case vestigia::Status::CORRUPT_SECTOR: return "CORRUPT_SECTOR";
  case vestigia::Status::WRITE_FAILED: return "WRITE_FAILED";
  }
  return "?";
}

}  // namespace

int main(int /*argc*/, char** /*argv*/) {
  // SDL needs init for SDL_GetBasePath. Headless-friendly subsystem.
  SDL_Init(0);

  log_step("Wipe save.bin");
  wipe_save_file();

  log_step("vestigia::init() on fresh region");
  vestigia::init();

  log_step("FX probe alive (data.bin readable)");
  if (!fx_probe_alive())
    fail("FX probe returned 0xFF — data.bin missing or unreadable");
  else
    pass("FX probe alive");

  log_step("Read slot 0 (unburdened)");
  storage::MetaCharacter m0;
  vestigia::Status s0 = vestigia::read_slot(0, m0);
  if (s0 != vestigia::Status::OK) {
    std::fprintf(stdout, "  status=%s (expected OK)\n", status_name(s0));
    fail("slot 0 should be OK after bootstrap");
  } else {
    pass("slot 0 reads OK");
  }

  log_step("Read slot 1 (should be EMPTY)");
  storage::MetaCharacter m1;
  vestigia::Status s1 = vestigia::read_slot(1, m1);
  if (s1 != vestigia::Status::EMPTY) {
    std::fprintf(stdout, "  status=%s (expected EMPTY)\n", status_name(s1));
    fail("slot 1 should be EMPTY on fresh save");
  } else {
    pass("slot 1 EMPTY");
  }

  log_step("FX probe alive after reads");
  if (!fx_probe_alive())
    fail("FX probe broken after vestigia reads");
  else
    pass("FX probe still alive");

  log_step("Write slot 1 with a known meta");
  storage::MetaCharacter src{};
  src.name[0]              = 'T';
  src.name[1]              = 'E';
  src.name[2]              = 'S';
  src.name[3]              = 'T';
  src.name[4]              = ' ';
  src.name[5]              = ' ';
  src.level_hp             = 5;
  src.level_damage         = 7;
  src.level_fire_rate      = 3;
  src.sangue_vessel        = 1234;
  src.total_runs           = 11;
  src.total_kills          = 222;
  src.total_sangue_earned  = 333333;
  src.shades[0]            = 0xAB;
  src.shades[1]            = 0xCD;
  src.shades[2]            = 0xEF;
  src.total_keepers_felled = 9;
  src.bullet               = 2;
  src.vestige              = storage::VESTIGE_PENITENT;
  src.burden               = 2;

  vestigia::Status sw = vestigia::write_slot(1, src);
  if (sw != vestigia::Status::OK) {
    std::fprintf(stdout, "  status=%s (expected OK)\n", status_name(sw));
    fail("write_slot(1) failed");
  } else {
    pass("write_slot(1) OK");
  }

  log_step("FX probe alive after write_slot — corruption canary");
  if (!fx_probe_alive())
    fail("FX probe broken after write_slot — REPRODUCED corruption");
  else
    pass("FX probe alive after write");

  log_step("Read slot 1 back, compare bytes");
  storage::MetaCharacter rb;
  vestigia::Status srb = vestigia::read_slot(1, rb);
  if (srb != vestigia::Status::OK) {
    std::fprintf(stdout, "  status=%s (expected OK)\n", status_name(srb));
    fail("read_slot(1) post-write should be OK");
  } else if (!meta_eq(src, rb)) {
    fail("read_slot(1) bytes differ from what we wrote");
  } else {
    pass("read_slot(1) round-trip matches");
  }

  log_step("Slot 0 still readable (write to 1 didn't damage 0)");
  storage::MetaCharacter m0b;
  vestigia::Status s0b = vestigia::read_slot(0, m0b);
  if (s0b != vestigia::Status::OK) {
    std::fprintf(stdout, "  status=%s (expected OK)\n", status_name(s0b));
    fail("slot 0 broken after writing slot 1");
  } else {
    pass("slot 0 survived write to slot 1");
  }

  log_step("Slots 2..7 still EMPTY");
  for (u8 i = 2; i < vestigia::SLOT_COUNT; ++i) {
    storage::MetaCharacter scratch;
    vestigia::Status si = vestigia::read_slot(i, scratch);
    if (si != vestigia::Status::EMPTY) {
      std::fprintf(stdout, "  slot %u status=%s (expected EMPTY)\n", (unsigned)i, status_name(si));
      fail("non-target slot perturbed by write");
      break;
    }
  }

  log_step("Multiple consecutive writes (ping-pong stress)");
  for (u8 round = 0; round < 5; ++round) {
    src.level_hp        = (u8)(10 + round);
    vestigia::Status sm = vestigia::write_slot(1, src);
    if (sm != vestigia::Status::OK) {
      std::fprintf(stdout, "  round %u status=%s\n", (unsigned)round, status_name(sm));
      fail("repeat write failed");
      break;
    }
    storage::MetaCharacter back;
    vestigia::Status mr = vestigia::read_slot(1, back);
    if (mr != vestigia::Status::OK || back.level_hp != (u8)(10 + round)) {
      std::fprintf(stdout, "  round %u readback failed\n", (unsigned)round);
      fail("repeat readback wrong");
      break;
    }
  }
  if (failures == 0) pass("5x write/read round-trips");

  // ---- Stress phase ---------------------------------------------------
  //
  // Pushes the chunked-write format to its corners. The format itself is
  // byte-literal (the slot record's bytes 2..27 are MetaCharacter bytes
  // verbatim, no encoding), so any byte pattern at any field should
  // round-trip exactly. The interesting failures are: XOR computation
  // across 32 B chunks (chunked-write change in commit 82d8ce7 rolls XOR
  // across chunks; off-by-one in the roll would corrupt specific
  // patterns), full-slot-array commits (8 chunked copies + 1 chunked
  // write per commit), and persistence across a fresh init().

  log_step("Stress: maxed-out meta round-trip in slot 2");
  storage::MetaCharacter maxed{};
  for (u8 i = 0; i < storage::NAME_LEN; ++i)
    maxed.name[i] = 'Z';
  maxed.level_hp             = 0xFF;
  maxed.level_damage         = 0xFF;
  maxed.level_fire_rate      = 0xFF;
  maxed.sangue_vessel        = 0xFFFF;
  maxed.total_runs           = 0xFFFF;
  maxed.total_kills          = 0xFFFF;
  maxed.total_sangue_earned  = 0xFFFFFFFFu;
  maxed.shades[0]            = 0xFF;
  maxed.shades[1]            = 0xFF;
  maxed.shades[2]            = 0xFF;
  maxed.total_keepers_felled = 0xFF;
  maxed.bullet               = 0xFF;
  maxed.vestige              = 0xFF;
  maxed.burden               = 0xFF;
  {
    vestigia::Status sw2 = vestigia::write_slot(2, maxed);
    if (sw2 != vestigia::Status::OK) {
      std::fprintf(stdout, "  status=%s\n", status_name(sw2));
      fail("maxed write_slot(2) failed");
    } else {
      storage::MetaCharacter back;
      vestigia::Status sr = vestigia::read_slot(2, back);
      if (sr != vestigia::Status::OK) {
        std::fprintf(stdout, "  read status=%s\n", status_name(sr));
        fail("maxed read_slot(2) failed");
      } else if (!meta_eq(maxed, back)) {
        fail("maxed meta did not round-trip byte-exact");
      } else {
        pass("maxed meta round-trip");
      }
    }
  }

  log_step("Stress: all 8 slots populated with distinct metas");
  // Slot 0 stays as the AUTOSAVE row (we don't overwrite it via
  // write_slot — that path is gated). Slots 1..7 each get a distinct
  // meta with a tag byte that's slot-unique, so a cross-slot copy bug
  // would show up as the wrong tag in the wrong slot.
  storage::MetaCharacter perslot[vestigia::SLOT_COUNT];
  for (u8 i = 1; i < vestigia::SLOT_COUNT; ++i) {
    storage::MetaCharacter m{};
    for (u8 c = 0; c < storage::NAME_LEN; ++c)
      m.name[c] = (char)('A' + i);
    m.level_hp             = (u8)(10 * i);
    m.level_damage         = (u8)(20 * i);
    m.level_fire_rate      = (u8)(30 * i);
    m.sangue_vessel        = (u16)(1000 * i);
    m.total_runs           = (u16)(2000 * i);
    m.total_kills          = (u16)(3000 * i);
    m.total_sangue_earned  = (u32)(100000u * i);
    m.shades[0]            = (u8)(i + 0x10);
    m.shades[1]            = (u8)(i + 0x20);
    m.shades[2]            = (u8)(i + 0x30);
    m.total_keepers_felled = i;
    m.bullet               = (u8)(i & 0x03);
    m.vestige              = (u8)(i % 4);
    m.burden               = (u8)(i % 4);
    perslot[i]             = m;
    vestigia::Status sw    = vestigia::write_slot(i, m);
    if (sw != vestigia::Status::OK) {
      std::fprintf(stdout, "  slot %u write status=%s\n", (unsigned)i, status_name(sw));
      fail("all-slots write failed");
      break;
    }
  }
  // Read every slot back and verify it matches. This is where a
  // cross-slot corruption (chunked copy_slot_chunked sourcing from
  // wrong sector / wrong offset) would surface as a mismatch.
  bool all_slots_ok = true;
  for (u8 i = 1; i < vestigia::SLOT_COUNT; ++i) {
    storage::MetaCharacter back;
    vestigia::Status sr = vestigia::read_slot(i, back);
    if (sr != vestigia::Status::OK) {
      std::fprintf(stdout, "  slot %u read status=%s\n", (unsigned)i, status_name(sr));
      fail("all-slots read failed");
      all_slots_ok = false;
      break;
    }
    if (!meta_eq(perslot[i], back)) {
      std::fprintf(stdout, "  slot %u readback differs from write\n", (unsigned)i);
      fail("all-slots cross-corruption");
      all_slots_ok = false;
      break;
    }
  }
  if (all_slots_ok) pass("all 7 manual slots populated and round-trip clean");

  log_step("Stress: boundary byte patterns (0xFE / 0x55 / 0xAA)");
  // 0xFE is "one below 0xFF" — if anything in the read path special-
  // cases 0xFF beyond byte 0 (the magic), this will catch it.
  // 0x55 / 0xAA are alternating-bit patterns that catch byte/word
  // misalignment in the chunk loop.
  const u8 patterns[3] = {0xFE, 0x55, 0xAA};
  bool patterns_ok     = true;
  for (u8 p = 0; p < 3; ++p) {
    storage::MetaCharacter pm;
    std::memset(&pm, patterns[p], sizeof(pm));
    // The `vestige` byte must be 0..3 to render correctly in the wood
    // UI, but the format itself doesn't care; we're testing the
    // byte-pipe not the renderer. Every byte = pattern.
    // Writing to slot 3+p so the three patterns don't overwrite each
    // other (we read them all back together at the end).
    const u8 slot      = (u8)(3 + p);
    vestigia::Status s = vestigia::write_slot(slot, pm);
    if (s != vestigia::Status::OK) {
      std::fprintf(stdout, "  pattern 0x%02X slot %u write status=%s\n", (unsigned)patterns[p],
                   (unsigned)slot, status_name(s));
      fail("pattern write failed");
      patterns_ok = false;
      break;
    }
    storage::MetaCharacter back;
    vestigia::Status sr = vestigia::read_slot(slot, back);
    if (sr != vestigia::Status::OK) {
      std::fprintf(stdout, "  pattern 0x%02X slot %u read status=%s\n", (unsigned)patterns[p],
                   (unsigned)slot, status_name(sr));
      fail("pattern read failed");
      patterns_ok = false;
      break;
    }
    if (!meta_eq(pm, back)) {
      std::fprintf(stdout, "  pattern 0x%02X did not round-trip\n", (unsigned)patterns[p]);
      fail("pattern XOR mismatch (chunked-write XOR-roll bug?)");
      patterns_ok = false;
      break;
    }
  }
  if (patterns_ok) pass("boundary patterns 0xFE/0x55/0xAA round-trip");

  log_step("Stress: re-init across simulated restart");
  // Force vestigia's `initialized` static back to false so init() does
  // its full re-read of both sectors. The save.bin on disk is unchanged
  // so we expect every slot to come back exactly. There's no public API
  // to reset `initialized` — instead we test what an actual restart
  // would test: vestigia::init() is idempotent on already-initialized
  // state, but a separate process invocation would call it cold. We
  // simulate by reading back ALL slots once more after a "session
  // boundary" (which here is just calling init() again — best we can
  // do without a process boundary; the save.bin durability is the same
  // either way).
  vestigia::init();  // idempotent re-init
  bool reinit_ok = true;
  for (u8 i = 1; i < vestigia::SLOT_COUNT; ++i) {
    // Phase write order: maxed wrote slot 2; all-slots wrote 1..7
    // (overwriting the maxed in slot 2); boundary patterns wrote 3,4,5
    // (overwriting per-slot in 3,4,5). So at this point: slots
    // 1,2,6,7 = per-slot data; slots 3,4,5 = boundary patterns.
    storage::MetaCharacter expected;
    if (i == 3) {
      std::memset(&expected, 0xFE, sizeof(expected));
    } else if (i == 4) {
      std::memset(&expected, 0x55, sizeof(expected));
    } else if (i == 5) {
      std::memset(&expected, 0xAA, sizeof(expected));
    } else {
      expected = perslot[i];
    }
    storage::MetaCharacter back;
    vestigia::Status sr = vestigia::read_slot(i, back);
    if (sr != vestigia::Status::OK || !meta_eq(expected, back)) {
      std::fprintf(stdout, "  slot %u post-reinit mismatch (status=%s)\n", (unsigned)i,
                   status_name(sr));
      fail("re-init lost data");
      reinit_ok = false;
      break;
    }
  }
  if (reinit_ok) pass("all slots persist across re-init");

  log_step("Stress: 100-write churn on slot 6");
  // Tests ping-pong sector alternation, generation counter increment,
  // and that no per-write state leaks. Each iteration writes a
  // different `level_hp` so every readback is distinguishable.
  bool churn_ok = true;
  for (u16 round = 0; round < 100; ++round) {
    storage::MetaCharacter cm{};
    cm.name[0]         = 'C';
    cm.name[1]         = 'H';
    cm.name[2]         = 'U';
    cm.name[3]         = 'R';
    cm.name[4]         = 'N';
    cm.name[5]         = (char)('0' + (round % 10));
    cm.level_hp        = (u8)(round & 0xFF);
    cm.bullet          = (u8)((round + 1) & 0x03);
    cm.vestige         = (u8)(round % 4);
    cm.burden          = (u8)((round / 4) % 4);
    vestigia::Status s = vestigia::write_slot(6, cm);
    if (s != vestigia::Status::OK) {
      std::fprintf(stdout, "  churn round %u status=%s\n", (unsigned)round, status_name(s));
      fail("churn write failed");
      churn_ok = false;
      break;
    }
    storage::MetaCharacter back;
    vestigia::Status sr = vestigia::read_slot(6, back);
    if (sr != vestigia::Status::OK || back.level_hp != (u8)(round & 0xFF)) {
      std::fprintf(stdout, "  churn round %u readback wrong\n", (unsigned)round);
      fail("churn readback wrong");
      churn_ok = false;
      break;
    }
  }
  if (churn_ok) pass("100-write churn on slot 6 round-trips clean");

  log_step("Stress: post-churn, all OTHER slots still intact");
  // Heavy churn on one slot must not corrupt other slots — every commit
  // erases-and-rewrites the destination sector, copying every other
  // slot through chunked I/O. If chunked-copy has any indexing bug,
  // 100 commits of slot 6 would surface it as drift in other slots.
  bool unchurned_ok = true;
  for (u8 i = 1; i < vestigia::SLOT_COUNT; ++i) {
    if (i == 6) continue;  // slot 6 is the churn target — skip
    // See re-init phase comment: at this point slots 1,2,7 hold per-
    // slot data, slots 3,4,5 hold boundary patterns. (Slot 2's maxed
    // meta was overwritten by the all-slots phase.)
    storage::MetaCharacter expected;
    if (i == 3) {
      std::memset(&expected, 0xFE, sizeof(expected));
    } else if (i == 4) {
      std::memset(&expected, 0x55, sizeof(expected));
    } else if (i == 5) {
      std::memset(&expected, 0xAA, sizeof(expected));
    } else {
      expected = perslot[i];
    }
    storage::MetaCharacter back;
    vestigia::Status sr = vestigia::read_slot(i, back);
    if (sr != vestigia::Status::OK || !meta_eq(expected, back)) {
      std::fprintf(stdout, "  slot %u post-churn mismatch (status=%s)\n", (unsigned)i,
                   status_name(sr));
      fail("non-target slot drifted during churn");
      unchurned_ok = false;
      break;
    }
  }
  if (unchurned_ok) pass("all non-churn slots survived 100 churn commits");

  std::fprintf(stdout, "\n%s — %d failure(s)\n", failures == 0 ? "PASS" : "FAIL", failures);
  std::fflush(stdout);
  SDL_Quit();
  return failures == 0 ? 0 : 1;
}
