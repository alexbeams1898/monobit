// Vestigia implementation. See vestigia.h for the API and lore frame.
//
// On-disk layout (v1):
//
//   Save region: 8 KB total, split into two 4 KB ping-pong sectors.
//
//     Sector A: save_offset 0x0000..0x0FFF
//     Sector B: save_offset 0x1000..0x1FFF
//
//   Each sector contains:
//
//     [0x000..0x01F]  Header (32 B)
//     [0x020..0x81F]  Slot records (8 × 256 B)
//     [0x820..0xFFF]  Reserved (zero-or-erased, future use)
//
//   Header (32 B):
//
//     0..3   magic       'V','S','T','G'
//     4      version     u8 (currently 1)
//     5      slot_count  u8 (8)
//     6..7   slot_size   u16 LE (256)
//     8..11  generation  u32 LE (write counter; higher == newer)
//     12..14 reserved    3 B (0x00, room for future fields)
//     15     header_xor  XOR of bytes 0..14
//     16..31 padding     (0xFF / chip-erased state — does not enter
//                         the XOR; reserved for a future header
//                         extension that won't break v1 readers)
//
//   Slot record (256 B), v1:
//
//     0      magic       'V' (0x56)  — distinguishes from 0xFF erased
//     1      flags       bit 0 = OCCUPIED
//     2..27  meta        MetaCharacter v4 (26 B; see storage.h)
//     28..254 reserved   pad to 254 B (0x00 today; future fields)
//     255    record_xor  XOR of bytes 0..254
//
//   Slot magic at byte 0 = 'V' is what tells a reader "this slot was
//   written" vs "this byte position is erased flash" — 0xFF is never
//   a valid slot magic.
//
//   Generation counter: monotonic 32-bit, increments by one on every
//   commit. Picks the newer sector at boot. Wraparound at 2^32 is
//   ~8000 years at one write per minute; not a concern.
//
//   Ping-pong commit: every write reads current → modifies in RAM →
//   erases the OTHER sector → writes the modified copy with gen+1 →
//   verifies. If power dies mid-write, the original sector is still
//   intact with the prior generation; boot picks it.
//
//   First boot detection: both sectors have magic != 'VSTG' (chip is
//   all 0xFF post-erase, or has random garbage post-flash). init()
//   notices and bootstraps: erases sector A, writes the unburdened
//   slot 0 with a blank MetaCharacter, generation=1. Sector B stays
//   erased until the second commit.

#include "vestigia.h"

#include "audio.h"
#include "data_flash.h"
#include "dbg.h"

// dbg::trace tag map for the slot-save bisect (2026-04-30 vestigia
// PC-out-of-bounds investigation). Emit one record per decision point;
// read the timeline back via tools/dbg_trace/parse.py with these
// annotations. Tags chosen in the 0x10..0x2F range so they don't
// collide with any other ad-hoc tracing.
//
//   0x10  write_slot entry,        value = slot index
//   0x11  init entry,              value = 0 (no payload)
//   0x12  read_header(A) returned, value = 0/1 (false/true)
//   0x13  read_header(B) returned, value = 0/1 (false/true)
//   0x14  bootstrap_fresh entry,   value = 0
//   0x15  bootstrap erase OK,      value = 0
//   0x16  bootstrap header write,  value = 0/1 (false/true)
//   0x17  bootstrap slot write,    value = 0/1 (false/true)
//   0x18  bootstrap done,          value = 0
//   0x19  commit_slot_change entry,value = changed_slot
//   0x1A  commit dest_off,         value = dest sector offset (0 or 0x1000)
//   0x1B  commit erase OK,         value = 0
//   0x1C  commit header write,     value = 0/1
//   0x1D  commit slot loop iter,   value = i (0..7)
//   0x1E  commit slot read magic,  value = first byte of slot record
//   0x1F  commit slot write OK,    value = 0/1
//   0x20  commit done,             value = 0 (would-be Status::OK)
//   0x21  write_slot exit,         value = (u16)Status code

namespace vestigia {

namespace {

// Header layout offsets within a sector.
constexpr u16 SECTOR_BYTES = 4096;
constexpr u16 HEADER_BYTES = 32;
constexpr u16 SLOT_BASE    = HEADER_BYTES;  // first slot starts at 32

// Two-sector ping-pong: sector A at 0x0000, sector B at 0x1000.
constexpr u16 SECTOR_A_OFF = 0;
constexpr u16 SECTOR_B_OFF = SECTOR_BYTES;

// Header field offsets (bytes 0..15 are XOR-covered; 16..31 are
// reserved padding outside the XOR).
constexpr u16 HDR_MAGIC      = 0;   // 4 B
constexpr u16 HDR_VERSION    = 4;   // 1 B
constexpr u16 HDR_SLOT_COUNT = 5;   // 1 B
constexpr u16 HDR_SLOT_SIZE  = 6;   // 2 B
constexpr u16 HDR_GENERATION = 8;   // 4 B
constexpr u16 HDR_RESERVED   = 12;  // 3 B
constexpr u16 HDR_XOR        = 15;  // 1 B

// Slot record offsets.
constexpr u16 REC_MAGIC = 0;
constexpr u16 REC_FLAGS = 1;
constexpr u16 REC_META  = 2;
constexpr u16 REC_XOR   = 255;

// Slot flag bits.
constexpr u8 FLAG_OCCUPIED = 0x01;

// Magic constants.
constexpr u8 MAGIC_HDR_0 = 'V';
constexpr u8 MAGIC_HDR_1 = 'S';
constexpr u8 MAGIC_HDR_2 = 'T';
constexpr u8 MAGIC_HDR_3 = 'G';
constexpr u8 MAGIC_REC   = 'V';  // 0x56 — never 0xFF, distinguishes from erased

// One-page scratch for slot/header read+modify+write. Smaller than a
// full sector — we don't need the whole sector in RAM at once for
// per-slot operations because each slot lives in one chip page.
// Sized to one chip page exactly so the buffer doubles as a write
// staging area for save_write_page.
constexpr u16 PAGE_BYTES = 256;

// Cached state from the most recent successful read/init. The wood
// UI consults `current_sector` to know which sector reads should
// target; `slot_cache[]` lets the slot list render without re-reading
// the chip on every frame.
//
// SlotCache holds {occupied, vestige, burden} per slot — 3 bytes ×
// 8 slots = 24 B in .bss. Populated by refresh_slot_cache after every
// write/init. The vestige+burden fields let the UI render each row's
// class+burden icon and label without per-frame FX reads (which would
// be ~8 × 256 B SPI per redraw — too slow). Stale only between a
// successful slot write and the next refresh_slot_cache call; both
// happen back-to-back inside write_slot, so observable staleness is
// effectively zero.
struct SlotCache {
  bool occupied;
  u8 vestige;
  u8 burden;
};

u16 current_sector               = SECTOR_A_OFF;  // refined by init()
u32 current_gen                  = 0;
bool initialized                 = false;
SlotCache slot_cache[SLOT_COUNT] = {};

// XOR a byte range. Used for header and slot checksums.
u8 xor_bytes(const u8* p, u16 n) {
  u8 acc = 0;
  for (u16 i = 0; i < n; ++i)
    acc = (u8)(acc ^ p[i]);
  return acc;
}

// Encode/decode helpers — keep on-disk layout endian-stable. AVR is
// little-endian and we explicitly match that on disk too, so PC reads
// of an Arduboy save produce the same values.
u16 read_u16_le(const u8* p) {
  return (u16)((u16)p[0] | ((u16)p[1] << 8));
}
u32 read_u32_le(const u8* p) {
  return (u32)p[0] | ((u32)p[1] << 8) | ((u32)p[2] << 16) | ((u32)p[3] << 24);
}
void write_u16_le(u8* p, u16 v) {
  p[0] = (u8)(v & 0xFF);
  p[1] = (u8)((v >> 8) & 0xFF);
}
void write_u32_le(u8* p, u32 v) {
  p[0] = (u8)(v & 0xFF);
  p[1] = (u8)((v >> 8) & 0xFF);
  p[2] = (u8)((v >> 16) & 0xFF);
  p[3] = (u8)((v >> 24) & 0xFF);
}

// Validate a sector's header. Returns the generation counter on
// success, or false if the header is invalid (bad magic, bad version,
// bad XOR). The header lives in the first 32 B of the sector.
bool read_header(u16 sector_off, u32& gen_out) {
  u8 hdr[HEADER_BYTES];
  data_flash::save_read(sector_off, hdr, HEADER_BYTES);
  if (hdr[HDR_MAGIC + 0] != MAGIC_HDR_0) return false;
  if (hdr[HDR_MAGIC + 1] != MAGIC_HDR_1) return false;
  if (hdr[HDR_MAGIC + 2] != MAGIC_HDR_2) return false;
  if (hdr[HDR_MAGIC + 3] != MAGIC_HDR_3) return false;
  if (hdr[HDR_VERSION] != FORMAT_VERSION) return false;
  if (hdr[HDR_SLOT_COUNT] != SLOT_COUNT) return false;
  if (read_u16_le(&hdr[HDR_SLOT_SIZE]) != SLOT_SIZE) return false;
  // Header XOR covers bytes 0..14 (everything before the XOR byte
  // itself). Bytes 16..31 are padding outside the checksum so a
  // future v2 can repurpose them without retro-invalidating v1.
  const u8 want = xor_bytes(hdr, HDR_XOR);
  if (hdr[HDR_XOR] != want) return false;
  gen_out = read_u32_le(&hdr[HDR_GENERATION]);
  return true;
}

// Build a header into `buf` (32 B). Used before write-back to the
// non-current sector.
void build_header(u8* buf, u32 gen) {
  for (u16 i = 0; i < HEADER_BYTES; ++i)
    buf[i] = 0;
  buf[HDR_MAGIC + 0]  = MAGIC_HDR_0;
  buf[HDR_MAGIC + 1]  = MAGIC_HDR_1;
  buf[HDR_MAGIC + 2]  = MAGIC_HDR_2;
  buf[HDR_MAGIC + 3]  = MAGIC_HDR_3;
  buf[HDR_VERSION]    = FORMAT_VERSION;
  buf[HDR_SLOT_COUNT] = SLOT_COUNT;
  write_u16_le(&buf[HDR_SLOT_SIZE], SLOT_SIZE);
  write_u32_le(&buf[HDR_GENERATION], gen);
  // HDR_RESERVED stays 0; padding past HDR_XOR stays 0 too (they
  // don't enter the XOR but we keep the rest of the header zeroed
  // for cleanliness).
  buf[HDR_XOR] = xor_bytes(buf, HDR_XOR);
}

// Read a slot record from `sector_off` in 32 B chunks. Validates
// magic + checksum without holding the full 256 B record on the
// stack. The full-record version was the deepest contributor to the
// stack peak that overflowed audio_phase_inc; this version peaks at
// 32 B + parse state instead.
//
// XOR is rolled across chunks (covers bytes 0..254). The meta fields
// all live in bytes 0..27 (within chunk 0), so we only deserialize
// from chunk 0 — but we still have to read every chunk to roll the
// XOR all the way to byte 254 before comparing against byte 255.
Status read_slot_from(u16 sector_off, u8 slot, storage::MetaCharacter& out) {
  if (slot >= SLOT_COUNT) return Status::CORRUPT_SLOT;
  const u16 base      = (u16)(sector_off + SLOT_BASE + slot * SLOT_SIZE);
  constexpr u16 CHUNK = 32;
  u8 chunk[CHUNK];
  u8 xor_acc = 0;
  u8 magic = 0, flags = 0, stored_xor = 0;

  for (u16 off = 0; off < SLOT_SIZE; off += CHUNK) {
    data_flash::save_read((u16)(base + off), chunk, CHUNK);

    if (off == 0) {
      // First chunk: byte 0 is magic, byte 1 is flags, bytes 2..27 are
      // meta. Rest of the chunk (28..31) is reserved/zero.
      magic = chunk[REC_MAGIC];
      flags = chunk[REC_FLAGS];
      // Deserialize MetaCharacter v4. v1 disk layout mirrors
      // MetaCharacter v4 exactly. If the live struct grows, this site
      // is the v1→v2 migration boundary.
      const u8* m = &chunk[REC_META];
      for (u8 i = 0; i < storage::NAME_LEN; ++i)
        out.name[i] = (char)m[i];
      out.level_hp             = m[6];
      out.level_damage         = m[7];
      out.level_fire_rate      = m[8];
      out.sangue_vessel        = read_u16_le(&m[9]);
      out.total_runs           = read_u16_le(&m[11]);
      out.total_kills          = read_u16_le(&m[13]);
      out.total_sangue_earned  = read_u32_le(&m[15]);
      out.shades[0]            = m[19];
      out.shades[1]            = m[20];
      out.shades[2]            = m[21];
      out.total_keepers_felled = m[22];
      out.bullet               = m[23];
      out.vestige              = m[24];
      out.burden               = m[25];
    }

    // Roll XOR across all chunks. The last chunk (offset 224..255)
    // contains the stored XOR at byte 255 (= chunk index 31). Save
    // it and stop the XOR roll at byte 254 to match build_slot's
    // xor_bytes(buf, REC_XOR=255) range.
    if (off + CHUNK >= SLOT_SIZE) {
      // Last chunk: roll XOR across bytes 0..30 of this chunk (which
      // map to slot offsets 224..254), then capture stored XOR.
      for (u8 i = 0; i < CHUNK - 1; ++i)
        xor_acc = (u8)(xor_acc ^ chunk[i]);
      stored_xor = chunk[CHUNK - 1];
    } else {
      for (u8 i = 0; i < CHUNK; ++i)
        xor_acc = (u8)(xor_acc ^ chunk[i]);
    }
  }

  // Erased slot: byte 0 is 0xFF. Post-erase state is empty, not corrupt.
  if (magic == 0xFF) return Status::EMPTY;
  if (magic != MAGIC_REC) return Status::CORRUPT_SLOT;
  // OCCUPIED bit must be set on a written slot. Reserved for future
  // tombstone semantics; today, any magic=='V' record also has OCCUPIED.
  if ((flags & FLAG_OCCUPIED) == 0) return Status::EMPTY;
  if (stored_xor != xor_acc) return Status::CORRUPT_SLOT;
  return Status::OK;
}

// Build the first 32 B chunk of a slot record from a MetaCharacter.
// The first chunk contains: magic (byte 0), flags (byte 1), and the
// MetaCharacter (bytes 2..27). Bytes 28..31 are reserved (zero).
//
// Slot record bytes 32..254 are all reserved/zero in v1, so they
// don't need a build helper — chunked writers just emit a zeroed
// 32 B buffer for those offsets.
void build_first_chunk(u8* chunk32, const storage::MetaCharacter& m) {
  for (u8 i = 0; i < 32; ++i)
    chunk32[i] = 0;
  chunk32[REC_MAGIC] = MAGIC_REC;
  chunk32[REC_FLAGS] = FLAG_OCCUPIED;
  u8* p              = &chunk32[REC_META];
  for (u8 i = 0; i < storage::NAME_LEN; ++i)
    p[i] = (u8)m.name[i];
  p[6] = m.level_hp;
  p[7] = m.level_damage;
  p[8] = m.level_fire_rate;
  write_u16_le(&p[9], m.sangue_vessel);
  write_u16_le(&p[11], m.total_runs);
  write_u16_le(&p[13], m.total_kills);
  write_u32_le(&p[15], m.total_sangue_earned);
  p[19] = m.shades[0];
  p[20] = m.shades[1];
  p[21] = m.shades[2];
  p[22] = m.total_keepers_felled;
  p[23] = m.bullet;
  p[24] = m.vestige;
  p[25] = m.burden;
  // Bytes 26..27 are storage::MetaCharacter overflow (currently unused,
  // bytes 24..25 are vestige/burden). Bytes 28..31 stay 0 (reserved).
}

// Slot-record XOR for the changed-slot write path. The XOR is over
// bytes 0..254 of the 256 B slot record. Since bytes 28..254 are all
// zero in v1, we only need to XOR the first 28 bytes (which is the
// first chunk minus its padding tail). This avoids materializing the
// full 256 B slot just to compute the checksum.
u8 compute_built_slot_xor(const u8* first_chunk32) {
  u8 acc = 0;
  for (u8 i = 0; i < 28; ++i)
    acc = (u8)(acc ^ first_chunk32[i]);
  // Bytes 28..254 are zero — XORing zero is a no-op. Skip straight to
  // returning the accumulator; byte 255 (the XOR slot) is what we are
  // computing, so it's not part of the input.
  return acc;
}

// Refresh `slot_cache[]` by reading every slot from the current
// sector. Called after init() and after every write so the UI has a
// fresh view. Stores occupied + vestige + burden per slot; the wood
// UI reads from this rather than from FX flash on every redraw.
void refresh_slot_cache() {
  storage::MetaCharacter scratch;
  for (u8 i = 0; i < SLOT_COUNT; ++i) {
    Status s               = read_slot_from(current_sector, i, scratch);
    slot_cache[i].occupied = (s == Status::OK);
    slot_cache[i].vestige  = (s == Status::OK) ? scratch.vestige : 0;
    slot_cache[i].burden   = (s == Status::OK) ? scratch.burden : 0;
  }
}

// sector index (0 or 1) for a given save_offset of a sector base.
// SECTOR_A_OFF (0x0000) → 0; SECTOR_B_OFF (0x1000) → 1.
u8 sector_index_of(u16 sector_off) {
  return (sector_off == SECTOR_A_OFF) ? 0 : 1;
}

// Write a freshly-built slot from a MetaCharacter into `dest_off`'s
// slot index `i`, in 8 × 32 B chunks. Peak stack contribution is one
// 32 B chunk + a few bytes of state, vs. 256 B for the legacy
// full-record buffer. Each chunk writes as one save_write_page call.
//
// Layout being written:
//   chunk 0 (offset 0..31):    magic + flags + meta + 4 zero bytes
//   chunks 1..6 (32..223):     all zero
//   chunk 7 (224..255):        zero except byte 31 (slot offset 255 = XOR)
__attribute__((noinline)) bool write_built_slot_chunked(u16 sector_off, u8 i,
                                                        const storage::MetaCharacter& m) {
  const u16 slot_off  = (u16)(sector_off + SLOT_BASE + i * SLOT_SIZE);
  constexpr u16 CHUNK = 32;
  u8 chunk[CHUNK];

  // Chunk 0: build it once, also use it to compute the XOR.
  build_first_chunk(chunk, m);
  const u8 xor_byte = compute_built_slot_xor(chunk);
  if (!data_flash::save_write_page(slot_off, chunk, CHUNK)) return false;

  // Chunks 1..6: all-zero. Reuse the chunk buffer.
  for (u8 i = 0; i < CHUNK; ++i)
    chunk[i] = 0;
  for (u8 c = 1; c <= 6; ++c) {
    if (!data_flash::save_write_page((u16)(slot_off + c * CHUNK), chunk, CHUNK)) return false;
  }

  // Chunk 7 (224..255): zeros except byte 31 = the XOR.
  chunk[CHUNK - 1] = xor_byte;
  if (!data_flash::save_write_page((u16)(slot_off + 7 * CHUNK), chunk, CHUNK)) return false;
  return true;
}

// Copy an unchanged slot from `src_sector_off` to `dest_sector_off` in
// 8 × 32 B chunks. If the source slot is erased (first byte of first
// chunk == 0xFF), returns true with `was_empty` set; the caller skips
// writing entirely so the destination's post-erase 0xFF state is
// preserved (matches the legacy "if (buf[REC_MAGIC] == 0xFF) continue"
// behavior).
__attribute__((noinline)) bool copy_slot_chunked(u16 src_sector_off, u16 dest_sector_off, u8 i,
                                                 bool& was_empty) {
  const u16 src_off   = (u16)(src_sector_off + SLOT_BASE + i * SLOT_SIZE);
  const u16 dest_off  = (u16)(dest_sector_off + SLOT_BASE + i * SLOT_SIZE);
  constexpr u16 CHUNK = 32;
  u8 chunk[CHUNK];

  was_empty = false;
  for (u16 off = 0; off < SLOT_SIZE; off += CHUNK) {
    data_flash::save_read((u16)(src_off + off), chunk, CHUNK);
    if (off == 0 && chunk[REC_MAGIC] == 0xFF) {
      was_empty = true;
      return true;  // skip the rest; dest stays 0xFF from the erase
    }
    if (!data_flash::save_write_page((u16)(dest_off + off), chunk, CHUNK)) return false;
  }
  return true;
}

// Bootstrap: write sector A with the unburdened slot 0 and the rest
// erased (all 0xFF — already the post-erase state). Used when both
// sectors are unreadable (first boot, fresh chip, total corruption
// recovery). Returns true on success.
//
// Uses the same chunked write path as commit_slot_change so peak
// stack stays at ~32 B for the slot write, not 256 B.
bool bootstrap_fresh() {
  dbg::trace(0x14);  // bootstrap_fresh entry
  // Silence the speaker for the ~50-400 ms erase. The audio ISR keeps
  // firing through the busy-wait inside save_erase_sector, but
  // audio::tick() doesn't run, so voices hold their last phase
  // increments and the speaker pin oscillates a continuous tone (a
  // "fart" noise). silence_pins drops voice phase increments to zero
  // without disturbing song/SFX state — they resume on next tick().
  // Same pattern as platform_scene_page_in for SPM swaps.
  audio::silence_pins();
  if (!data_flash::save_erase_sector(0)) return false;
  dbg::trace(0x15);  // bootstrap erase OK

  // Header.
  {
    u8 hdr[HEADER_BYTES];
    build_header(hdr, /*gen=*/1);
    const bool hdr_ok = data_flash::save_write_page(SECTOR_A_OFF, hdr, HEADER_BYTES);
    dbg::trace(0x16, hdr_ok ? 1 : 0);
    if (!hdr_ok) return false;
  }

  // Slot 0 = unburdened. Other slots stay 0xFF from the erase.
  storage::MetaCharacter blank{};  // zero-init
  blank.vestige      = storage::VESTIGE_UNBURDENED;
  const bool slot_ok = write_built_slot_chunked(SECTOR_A_OFF, SLOT_UNBURDENED, blank);
  dbg::trace(0x17, slot_ok ? 1 : 0);
  if (!slot_ok) return false;

  current_sector = SECTOR_A_OFF;
  current_gen    = 1;
  dbg::trace(0x18);  // bootstrap done
  return true;
}

// Commit one slot change to the OTHER ping-pong sector. Erases the
// destination, writes new header, copies every other slot from the
// current sector, writes the new MetaCharacter at `changed_slot`,
// then promotes the destination to current.
//
// Stack budget: peak local is one 32 B chunk inside the chunked
// helpers (write_built_slot_chunked / copy_slot_chunked), not a full
// 256 B record. The legacy 256 B buf overflowed audio_phase_inc on
// the deepest call frame; chunked writes free ~224 B of headroom.
Status commit_slot_change(u8 changed_slot, const storage::MetaCharacter& new_meta) {
  dbg::trace(0x19, changed_slot);  // commit_slot_change entry
  const u16 dest_off = (current_sector == SECTOR_A_OFF) ? SECTOR_B_OFF : SECTOR_A_OFF;
  const u8 dest_idx  = sector_index_of(dest_off);
  dbg::trace(0x1A, dest_off);  // commit dest_off

  // Silence audio for the erase + page-program busy-waits (~150 ms
  // worst case). See bootstrap_fresh for the rationale.
  audio::silence_pins();
  if (!data_flash::save_erase_sector(dest_idx)) return Status::WRITE_FAILED;
  dbg::trace(0x1B);  // commit erase OK

  // Header (32 B). Stays in its own scoped block so the buffer's
  // lifetime ends before the slot loop's chunk buffers come alive —
  // GCC may reuse the stack slot, but scoping makes the intent clear.
  {
    u8 hdr[HEADER_BYTES];
    build_header(hdr, current_gen + 1);
    const bool hdr_ok = data_flash::save_write_page(dest_off, hdr, HEADER_BYTES);
    dbg::trace(0x1C, hdr_ok ? 1 : 0);
    if (!hdr_ok) return Status::WRITE_FAILED;
  }

  // Slots. The chunked helpers each carry a 32 B buffer; only one is
  // alive at a time (sequential calls, no nesting).
  for (u8 i = 0; i < SLOT_COUNT; ++i) {
    dbg::trace(0x1D, i);  // commit slot loop iter
    bool slot_ok;
    if (i == changed_slot) {
      slot_ok = write_built_slot_chunked(dest_off, i, new_meta);
    } else {
      bool was_empty = false;
      slot_ok        = copy_slot_chunked(current_sector, dest_off, i, was_empty);
      if (was_empty) {
        dbg::trace(0x1E, 0xFF);  // empty source — dest stays 0xFF
        continue;
      }
    }
    dbg::trace(0x1F, slot_ok ? 1 : 0);
    if (!slot_ok) return Status::WRITE_FAILED;
  }

  current_sector = dest_off;
  current_gen += 1;
  dbg::trace(0x20);  // commit done
  return Status::OK;
}

}  // namespace

// ---- Public API -----------------------------------------------------

void init() {
  // dbg::trace(0x11);  // init entry — disabled for bisect
  if (initialized) {
    refresh_slot_cache();
    return;
  }
  initialized = true;
  u32 gen_a = 0, gen_b = 0;
  const bool a_ok = read_header(SECTOR_A_OFF, gen_a);
  dbg::trace(0x12, a_ok ? 1 : 0);
  const bool b_ok = read_header(SECTOR_B_OFF, gen_b);
  dbg::trace(0x13, b_ok ? 1 : 0);

  if (!a_ok && !b_ok) {
    // First boot or total corruption: bootstrap fresh.
    if (!bootstrap_fresh()) {
      // Hardware fault — couldn't even erase. Leave initialized=true
      // so subsequent reads return CORRUPT_SECTOR; the caller surfaces
      // the doctrine-flavored "Hell's ledger is silent" UX.
      current_sector = SECTOR_A_OFF;
      current_gen    = 0;
      for (u8 i = 0; i < SLOT_COUNT; ++i) {
        slot_cache[i].occupied = false;
        slot_cache[i].vestige  = 0;
        slot_cache[i].burden   = 0;
      }
      return;
    }
  } else if (a_ok && (!b_ok || gen_a >= gen_b)) {
    current_sector = SECTOR_A_OFF;
    current_gen    = gen_a;
  } else {
    current_sector = SECTOR_B_OFF;
    current_gen    = gen_b;
  }
  refresh_slot_cache();
}

Status read_slot(u8 slot, storage::MetaCharacter& out) {
  if (!initialized) init();
  if (slot >= SLOT_COUNT) return Status::CORRUPT_SLOT;
  return read_slot_from(current_sector, slot, out);
}

Status write_slot(u8 slot, const storage::MetaCharacter& m) {
  dbg::trace(0x10, slot);  // write_slot entry
  if (!initialized) init();
  if (slot >= SLOT_COUNT) return Status::WRITE_FAILED;
  if (slot == SLOT_UNBURDENED) return Status::WRITE_FAILED;
  Status s = commit_slot_change(slot, m);
  dbg::trace(0x21, (u16)s);  // write_slot exit
  if (s == Status::OK) refresh_slot_cache();
  return s;
}

Status write_autosave(const storage::MetaCharacter& m) {
  if (!initialized) init();
  // Bypasses write_slot's slot-0 gate (which exists to prevent the
  // player from manually overwriting AUTOSAVE from the slot list).
  // Wood-arrival code is the sole legitimate caller.
  Status s = commit_slot_change(SLOT_UNBURDENED, m);
  if (s == Status::OK) refresh_slot_cache();
  return s;
}

bool slot_occupied(u8 s) {
  if (s >= SLOT_COUNT) return false;
  return slot_cache[s].occupied;
}

u8 slot_vestige(u8 s) {
  if (s >= SLOT_COUNT) return 0;
  return slot_cache[s].vestige;
}

u8 slot_burden(u8 s) {
  if (s >= SLOT_COUNT) return 0;
  return slot_cache[s].burden;
}

}  // namespace vestigia
