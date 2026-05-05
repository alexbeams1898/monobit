// Arduboy FX W25Q128 SPI flash driver.
//
// Hardware: 16 MB SPI flash on the FX cart.
//   FX CS   -> PD1 (active low, idle high) — official Arduboy FX wiring
//   SCK     -> PB1 (shared with OLED)
//   MOSI    -> PB2 (shared with OLED)
//   MISO    -> PB3 (FX-only — OLED is write-only, so MISO is FX's alone)
//
// The SPI peripheral itself is shared with the SSD1306 (display.cpp). It
// is configured by display::init() at boot — master mode, mode 0,
// fosc/2 (8 MHz). This driver assumes that init has already happened
// and does NOT touch SPCR/SPSR. CS lines are the only thing that
// distinguishes whose transaction it is on the bus.
//
// CS coordination: OLED CS (PD6) and FX CS (PD1) must never be low
// simultaneously. The display driver bookends each command/flush with
// CS low/high, so as long as FX reads are not interleaved into the
// middle of a display flush, there is no collision. Concrete rule:
// only call data_flash::read() from update() / per-frame logic, never
// from the inside of a draw() that has already called display::flush()
// or from within an ISR. The audio ISR doesn't touch SPI at all.
//
// On a stock Arduboy (no FX cart attached), MISO floats high — every
// byte read reads back as 0xFF, which matches our "erased / not
// present" sentinel. So the same code path "works" on bare hardware:
// reads return 0xFF and the game runs as if the cart were empty.

#include "data_flash.h"

#include <avr/io.h>
#include <avr/pgmspace.h>
#include <util/delay.h>

namespace {

// W25Q128 command opcodes.
constexpr u8 CMD_READ = 0x03;               // Standard READ. fosc/2 (8 MHz) is
                                            // well under the W25Q's 50 MHz
                                            // Standard READ ceiling.
constexpr u8 CMD_RELEASE_POWERDOWN = 0xAB;  // Wake the chip out of deep
                                            // power-down. The W25Q128 powers
                                            // up in DPD mode and ignores
                                            // every command except 0xAB
                                            // until released. Without this,
                                            // every READ returns 0xFF
                                            // (MISO floats high in DPD).
constexpr u8 CMD_READ_STATUS = 0x05;        // Read status register 1; bit 0
                                            // is WIP (write-in-progress).
                                            // Polled after every write/erase
                                            // until clear before the next
                                            // SPI transaction is safe.
constexpr u8 CMD_WRITE_ENABLE = 0x06;       // Set the WEL (write-enable
                                            // latch) bit. Required before
                                            // every page-program and
                                            // sector-erase; the chip clears
                                            // WEL automatically after each.
constexpr u8 CMD_PAGE_PROGRAM = 0x02;       // Write up to 256 B within a
                                            // single 256 B chip page. Bytes
                                            // can only flip 1→0 — caller
                                            // must have erased the
                                            // destination sector first.
constexpr u8 CMD_SECTOR_ERASE = 0x20;       // Erase one 4 KB sector to all
                                            // 0xFF. ~50 ms typical, 400 ms
                                            // max. CPU keeps running; any
                                            // following SPI command is
                                            // ignored until WIP clears.

// Save region: chip-absolute [SAVE_BASE, SAVE_BASE + SAVE_SIZE). Fixed
// location, independent of where data.bin landed (program_data_page).
// The top 8 KB of the chip is reserved for two 4 KB ping-pong sectors
// (vestigia + ledger), extending the Arduboy fxsave convention.
// Sector 0 lives at SAVE_BASE, sector 1 at SAVE_BASE + 4096. Build
// pipeline pads data.bin to 16 MB - 8 KB to leave room.
constexpr u32 SAVE_BASE  = 0x00FFE000UL;  // 16 MB - 8 KB
constexpr u16 PAGE_BYTES = 256;

// programDataPage placeholder mechanism. The canonical Arduboy FX flash
// tool patches the user's hex at upload time to set the page where this
// game's data was placed on the cart. The patch sits in interrupt
// vector 5's slot (PROGMEM 0x14-0x17), which Arduboy doesn't otherwise
// use:
//
//   PROGMEM 0x14, 0x15:  0x18 0x95   (RETI opcode, harmless if executed)
//   PROGMEM 0x16, 0x17:  page_high, page_low  (big-endian)
//
// Magic value 0x9518 is the AVR RETI instruction — chosen so that even
// if the CPU ever did jump to vector 5, the byte sequence is a no-op
// return. Stored little-endian in flash, so bytes are 0x18 0x95.
//
// At runtime we read these bytes once and convert to a chip byte offset
// (page * 256). If the magic isn't there (un-patched hex, e.g. running
// in Ardens with no flash tool involvement), we fall back to a default
// that matches Ardens' fxdata placement formula for our specific image
// size: see the FX_DATA_BASE_FALLBACK comment below.
constexpr u16 PROGMEM_VECTOR_KEY  = 0x14;
constexpr u16 PROGMEM_VECTOR_PAGE = 0x16;
constexpr u16 FX_VECTOR_KEY_VALUE = 0x9518;

// Fallback chip byte offset when the hex hasn't been patched. Ardens
// places non-flashcart .bin files at:
//   fxdata_offset = 16 MB - fxsave_aligned(N KB) - fxdata_aligned(256)
// Our data.bin is padded to 16 MB - 8 KB (two ping-pong save sectors),
// so:
//   fxdata_offset = 16 MB - 8 KB - (16 MB - 8 KB) = 8 KB = 0x2000
// page = 0x2000 / 256 = 0x20.
constexpr u16 FX_PAGE_FALLBACK = 0x20;

inline void fx_cs_low() {
  PORTD &= ~(1 << 1);
}
inline void fx_cs_high() {
  PORTD |= (1 << 1);
}

// One SPI byte. Display driver uses the same pattern; we duplicate it
// here rather than depending on display.cpp's internal helper, so the
// FX driver doesn't need a friend declaration / public spi_write API.
inline u8 spi_xfer(u8 b) {
  SPDR = b;
  while (!(SPSR & (1 << SPIF))) {}
  return SPDR;
}

// First-call init state. cs_configured covers PD1 + the W25Q wake;
// program_data_page caches the resolved chip-base page (from the
// patched magic vector or the fallback).
bool cs_configured    = false;
u16 program_data_page = 0;

void resolve_program_data_page() {
  // Read the 16-bit magic at PROGMEM 0x14 (little-endian in flash).
  // pgm_read_word returns a host-order u16, so we compare directly to
  // FX_VECTOR_KEY_VALUE.
  const u16 key = pgm_read_word(PROGMEM_VECTOR_KEY);
  if (key == FX_VECTOR_KEY_VALUE) {
    // Patched: page is stored big-endian at 0x16/0x17.
    const u8 hi       = pgm_read_byte(PROGMEM_VECTOR_PAGE);
    const u8 lo       = pgm_read_byte(PROGMEM_VECTOR_PAGE + 1);
    program_data_page = ((u16)hi << 8) | lo;
  } else {
    // Un-patched: assume Ardens convention. On real hardware without
    // running the FX flash tool this would point at the wrong place;
    // see docs/fx-flash.md for the migration path.
    program_data_page = FX_PAGE_FALLBACK;
  }
}

// Configure PD1 + wake the chip + resolve program_data_page once on
// first use. Idempotent: subsequent calls are cheap (single bit-read).
// Can't put this in a constructor or a dedicated init() — engine code
// includes data_flash.h without knowing it's an Arduboy build, and
// there's no platform-specific init pass the engine drives. First-call
// init keeps the surface area portable.
void ensure_cs_configured() {
  if (cs_configured) return;
  // PD1 as output, idle high (CS deasserted).
  PORTD |= (1 << 1);
  DDRD |= (1 << 1);

  // Wake the chip from Deep Power-Down. Required once after boot;
  // until this lands the chip ignores all other commands and MISO
  // floats high (every READ returns 0xFF). tRES1 (release-to-ready)
  // is 3 µs on the W25Q128 — meet it with a 5 µs delay so the next
  // READ sees a responsive chip.
  fx_cs_low();
  spi_xfer(CMD_RELEASE_POWERDOWN);
  fx_cs_high();
  _delay_us(5);

  resolve_program_data_page();
  cs_configured = true;
}

// One bus-level read at a chip-absolute address. Bus is held LOW for
// the duration; caller is responsible for ensure_cs_configured().
// 32bit-ok: abs spans the full 24-bit chip address space.
void chip_read_abs(u32 abs, void* dst, u16 n) {
  u8* d = (u8*)dst;
  fx_cs_low();
  spi_xfer(CMD_READ);
  spi_xfer((u8)((abs >> 16) & 0xFF));
  spi_xfer((u8)((abs >> 8) & 0xFF));
  spi_xfer((u8)(abs & 0xFF));
  for (u16 i = 0; i < n; ++i) {
    d[i] = spi_xfer(0x00);
  }
  fx_cs_high();
}

// Send a single-opcode command (no address, no data). Used for
// CMD_WRITE_ENABLE; the chip latches WEL on the rising edge of CS.
void send_cmd1(u8 opcode) {
  fx_cs_low();
  spi_xfer(opcode);
  fx_cs_high();
}

// Poll the W25Q status register until the WIP (write-in-progress) bit
// clears. After page-program (~700 µs) or sector-erase (~50 ms typical,
// 400 ms max), the chip ignores every command except CMD_READ_STATUS
// until WIP drops. This is a busy loop — the audio ISR keeps firing
// underneath, but per-frame audio::tick() does NOT (caller hasn't
// returned yet). Vestigia writes happen on the wood where a brief
// audio stall is acceptable; mid-combat writes are forbidden by
// design (see docs/design/ "ledger and the vestigia").
void wait_wip_clear() {
  fx_cs_low();
  spi_xfer(CMD_READ_STATUS);
  for (;;) {
    const u8 status = spi_xfer(0x00);
    if ((status & 0x01) == 0) break;  // WIP cleared
  }
  fx_cs_high();
}

}  // namespace

namespace data_flash {

// 32bit-ok: u32 offset is the FX 16 MB address space; can't fit in u16.
void read(u32 offset, void* dst, u16 n) {
  ensure_cs_configured();  // resolves program_data_page on first call

  // Chip absolute address = (page * 256) + caller's relative offset.
  // page is the FX flash tool's patched value, or the fallback for
  // Ardens-style placement when un-patched.
  // 32bit-ok: page << 8 in u16 space loses the top byte (page can be
  // up to 0xFFFF → byte addr up to 0xFFFF00), and the result has to
  // span the full 24-bit chip address space anyway.
  const u32 abs_offset = ((u32)program_data_page << 8) + offset;
  chip_read_abs(abs_offset, dst, n);

  // W25Q tCSH min is 30 ns at 3.3 V — already met by the next
  // instruction's fetch latency. No software delay needed before the
  // next FX transaction (or before the OLED reclaims the bus).
}

// 32bit-ok: u32 offset is the FX 16 MB address space; can't fit in u16.
u8 read_byte(u32 offset) {
  u8 b;
  read(offset, &b, 1);
  return b;
}

// ---- Save region ----------------------------------------------------

void save_read(u16 save_offset, void* dst, u16 n) {
  ensure_cs_configured();
  u8* d = (u8*)dst;
  // Bound-check: anything past SAVE_SIZE 0xFF-fills (consistent with
  // the rest of this module's "past EOF reads as erased flash" rule).
  if (save_offset >= SAVE_SIZE) {
    for (u16 i = 0; i < n; ++i)
      d[i] = 0xFF;
    return;
  }
  u16 in_bounds = n;
  if ((u32)save_offset + (u32)n > (u32)SAVE_SIZE) {
    in_bounds = (u16)(SAVE_SIZE - save_offset);
  }
  chip_read_abs(SAVE_BASE + (u32)save_offset, d, in_bounds);
  // Tail past SAVE_SIZE: fill with erased-state byte.
  for (u16 i = in_bounds; i < n; ++i)
    d[i] = 0xFF;
}

bool save_write_page(u16 save_offset, const void* src, u16 n) {
  ensure_cs_configured();
  if (n == 0) return true;
  // Bounds: must fit within SAVE_SIZE and not span a 256 B page boundary.
  if ((u32)save_offset + (u32)n > (u32)SAVE_SIZE) return false;
  const u16 page_off = (u16)(save_offset & (PAGE_BYTES - 1));
  if ((u32)page_off + (u32)n > (u32)PAGE_BYTES) return false;

  const u32 abs = SAVE_BASE + (u32)save_offset;

  // 1. Latch WEL. Required before every page-program; the chip clears
  //    WEL automatically when the program completes.
  send_cmd1(CMD_WRITE_ENABLE);

  // 2. Issue the page-program command + 24-bit address + payload.
  //    The chip starts the actual flash program on the rising edge of
  //    CS — bytes are buffered internally during the SPI transfer.
  fx_cs_low();
  spi_xfer(CMD_PAGE_PROGRAM);
  spi_xfer((u8)((abs >> 16) & 0xFF));
  spi_xfer((u8)((abs >> 8) & 0xFF));
  spi_xfer((u8)(abs & 0xFF));
  const u8* s = (const u8*)src;
  for (u16 i = 0; i < n; ++i) {
    spi_xfer(s[i]);
  }
  fx_cs_high();

  // 3. Wait for the program to complete (~700 µs typical). The chip
  //    ignores every command except CMD_READ_STATUS until WIP clears.
  wait_wip_clear();
  return true;
}

bool save_erase_sector(u8 sector) {
  if (sector >= SECTOR_COUNT) return false;
  ensure_cs_configured();
  send_cmd1(CMD_WRITE_ENABLE);

  // Per-sector base on the chip. sector 0 = SAVE_BASE, sector 1 =
  // SAVE_BASE + SECTOR_SIZE. The chip's CMD_SECTOR_ERASE walks the
  // 4 KB containing whatever address we pass; any address within the
  // sector's 4 KB range picks the same sector.
  const u32 sector_base = SAVE_BASE + (u32)sector * SECTOR_SIZE;

  fx_cs_low();
  spi_xfer(CMD_SECTOR_ERASE);
  spi_xfer((u8)((sector_base >> 16) & 0xFF));
  spi_xfer((u8)((sector_base >> 8) & 0xFF));
  spi_xfer((u8)(sector_base & 0xFF));
  fx_cs_high();

  // ~50 ms typical, 400 ms worst-case. Audio ISR keeps the speaker
  // alive but per-frame audio::tick() doesn't run during this; the
  // caller is on the wood, where the brief stall is acceptable. (See
  // wait_wip_clear above.)
  wait_wip_clear();

  // Spot-check: bytes at sector start, middle, and end should now read
  // 0xFF. Cheaper than reading the full 4 KB and catches the common
  // failure (erase didn't take).
  u8 probe;
  chip_read_abs(sector_base, &probe, 1);
  if (probe != 0xFF) return false;
  chip_read_abs(sector_base + SECTOR_SIZE / 2, &probe, 1);
  if (probe != 0xFF) return false;
  chip_read_abs(sector_base + SECTOR_SIZE - 1, &probe, 1);
  if (probe != 0xFF) return false;
  return true;
}

}  // namespace data_flash
