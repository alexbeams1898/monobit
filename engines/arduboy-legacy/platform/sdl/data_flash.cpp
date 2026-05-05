// SDL implementation of engine/data_flash.h — file-backed mock of the
// Arduboy FX 16 MB SPI flash. Reads come from `data.bin` next to the
// executable; if missing, reads return 0xFF (matching the natural
// erased state of NOR flash, so the dev-time experience matches a
// stock Arduboy without an FX cart attached).
//
// The save region (engine/data_flash.h save_*) is a separate file
// `save.bin` next to the exe. It is created on first write and grown
// to SAVE_SIZE on demand; missing-file reads return 0xFF (the chip's
// erased-state convention).
//
// The build pipeline is responsible for producing data.bin alongside
// the SDL binary. save.bin is player data and lives across builds.

#include "data_flash.h"

#include <SDL.h>
#include <cstdio>
#include <cstring>

namespace data_flash {

namespace {

// Lazy-open the file on first read so missing-file is silent (not a
// startup hard-fail). Matches the stock-Arduboy "no FX present" path
// behaviorally — reads return 0xFF, game still runs.
//
// File is resolved relative to the executable's directory (via
// SDL_GetBasePath), not the current working directory — the build
// pipeline copies data.bin next to the exe, and CWD on launch can
// vary (make rule, debugger, double-click).
FILE* fp            = nullptr;
bool open_attempted = false;

// Save-region file. Opened r+b on first save_read after any write
// has happened; created on first save_write_page or save_erase_sector.
// Path resolved alongside the exe like data.bin, so dev iteration on
// the build doesn't move the player's saves.
FILE* save_fp           = nullptr;
char save_path[512]     = {0};
bool save_path_resolved = false;

void resolve_save_path() {
  if (save_path_resolved) return;
  save_path_resolved = true;
  char* base         = SDL_GetBasePath();
  if (!base) {
    std::snprintf(save_path, sizeof(save_path), "save.bin");
  } else {
    std::snprintf(save_path, sizeof(save_path), "%ssave.bin", base);
    SDL_free(base);
  }
}

// Open save.bin for read+write, creating it if missing. Returns null
// on filesystem failure. Caller checks fp before use.
FILE* save_open_or_create() {
  if (save_fp) return save_fp;
  resolve_save_path();
  // r+b first (don't truncate existing data); fall back to w+b which
  // creates the file if it didn't exist.
  save_fp = std::fopen(save_path, "r+b");
  if (!save_fp) {
    save_fp = std::fopen(save_path, "w+b");
  }
  return save_fp;
}

void ensure_open() {
  if (open_attempted) return;
  open_attempted = true;
  char* base     = SDL_GetBasePath();  // returns malloc'd path with trailing sep
  if (!base) {
    fp = std::fopen("data.bin", "rb");  // CWD fallback
  } else {
    char path[512];
    std::snprintf(path, sizeof(path), "%sdata.bin", base);
    fp = std::fopen(path, "rb");
    if (fp) {
      std::fprintf(stderr, "data_flash: opened %s\n", path);
      std::fflush(stderr);
    }
    SDL_free(base);
  }
}

}  // namespace

void read(u32 offset, void* dst, u16 n) {
  ensure_open();
  if (!fp) {
    std::memset(dst, 0xFF, n);
    return;
  }
  if (std::fseek(fp, (long)offset, SEEK_SET) != 0) {
    std::memset(dst, 0xFF, n);
    return;
  }
  size_t got = std::fread(dst, 1, n, fp);
  if (got < (size_t)n) {
    // Partial read past EOF: 0xFF-fill the tail, matching the W25Q128's
    // behavior on erased / past-image addresses.
    std::memset((u8*)dst + got, 0xFF, n - got);
  }
}

u8 read_byte(u32 offset) {
  u8 b = 0xFF;
  read(offset, &b, 1);
  return b;
}

// ---- Save region ----------------------------------------------------

void save_read(u16 save_offset, void* dst, u16 n) {
  u8* d = (u8*)dst;
  // Bound-check first: anything past SAVE_SIZE is 0xFF-fill (matches
  // the chip's "past EOF reads as erased flash" convention used
  // throughout this module).
  if (save_offset >= SAVE_SIZE) {
    std::memset(dst, 0xFF, n);
    return;
  }
  u16 in_bounds = n;
  if ((u32)save_offset + (u32)n > (u32)SAVE_SIZE) {
    in_bounds = (u16)(SAVE_SIZE - save_offset);
  }
  // Existing-file read; treat missing-file as all-erased. We do NOT
  // create the file just to read it — keeps the "fresh install has no
  // save" path aligned with the chip's erased-state read behavior.
  resolve_save_path();
  FILE* rfp = save_fp ? save_fp : std::fopen(save_path, "rb");
  if (!rfp) {
    std::memset(dst, 0xFF, n);
    return;
  }
  if (std::fseek(rfp, (long)save_offset, SEEK_SET) != 0) {
    std::memset(dst, 0xFF, n);
    if (rfp != save_fp) std::fclose(rfp);
    return;
  }
  size_t got = std::fread(d, 1, in_bounds, rfp);
  if (got < (size_t)in_bounds) {
    std::memset(d + got, 0xFF, in_bounds - got);
  }
  for (u16 i = in_bounds; i < n; ++i)
    d[i] = 0xFF;
  if (rfp != save_fp) std::fclose(rfp);
}

bool save_write_page(u16 save_offset, const void* src, u16 n) {
  if (n == 0) return true;
  // Same bounds rules as the Arduboy backend: must fit in SAVE_SIZE
  // and not span a 256 B page boundary. Enforced on PC so test code
  // exercises the same constraints as real hardware.
  if ((u32)save_offset + (u32)n > (u32)SAVE_SIZE) return false;
  const u16 page_off = (u16)(save_offset & 0xFF);
  if ((u32)page_off + (u32)n > 256) return false;

  FILE* wfp = save_open_or_create();
  if (!wfp) return false;

  // SDL backend doesn't enforce flash's "writes can only flip 1→0"
  // rule — that's an artifact of NOR flash physics, not file I/O. PC
  // tests still exercise the erase-before-write *protocol* via the
  // higher layers, just without the physical penalty for breaking it.
  if (std::fseek(wfp, (long)save_offset, SEEK_SET) != 0) return false;
  if (std::fwrite(src, 1, n, wfp) != (size_t)n) return false;
  std::fflush(wfp);
  return true;
}

bool save_erase_sector(u8 sector) {
  if (sector >= SECTOR_COUNT) return false;
  FILE* wfp = save_open_or_create();
  if (!wfp) return false;
  // Make sure the file is at least SAVE_SIZE bytes long so that
  // sector 1 has a place to live before it's been written. fseek
  // past EOF + fwrite extends the file in standard C; doing this
  // explicitly the first time keeps subsequent erases simple.
  if (std::fseek(wfp, 0, SEEK_END) == 0) {
    long cur = std::ftell(wfp);
    if (cur < (long)SAVE_SIZE) {
      u8 erased_byte = 0xFF;
      for (long pad = cur; pad < (long)SAVE_SIZE; ++pad) {
        if (std::fwrite(&erased_byte, 1, 1, wfp) != 1) return false;
      }
    }
  }
  // Erase one 4 KB sector to all 0xFF.
  const u16 base = (u16)((u16)sector * SECTOR_SIZE);
  if (std::fseek(wfp, (long)base, SEEK_SET) != 0) return false;
  u8 erased[256];
  std::memset(erased, 0xFF, sizeof(erased));
  for (u16 written = 0; written < SECTOR_SIZE; written = (u16)(written + 256)) {
    if (std::fwrite(erased, 1, 256, wfp) != 256) return false;
  }
  std::fflush(wfp);
  return true;
}

}  // namespace data_flash
