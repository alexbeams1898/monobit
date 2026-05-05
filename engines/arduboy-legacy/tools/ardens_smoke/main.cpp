// Ardens-headless smoke test for Arduboy builds.
//
// Boots the linked .hex against an FX data.bin, runs the simulated
// CPU through one of several built-in scenarios, and exits nonzero if
// any of Ardens' autobreak signals fired during the run:
//
//   AB_OOB_PC      — program counter walked outside flash
//   AB_OOB_DEREF   — ld/lds/st/sts to address >= RAMEND+1 (0x0B00)
//   AB_NULL_DEREF  — same, address == 0
//   AB_OOB_EEPROM  — EEAR addr >= 0x400 on a read or program op
//   AB_STACK_OVERFLOW — SP descended past `stack_check`
//   AB_OOB_IJMP    — ijmp / icall to a target outside flash
//   AB_NULL_REL_DEREF, AB_UNKNOWN_INSTR, AB_SPI_WCOL, AB_FX_BUSY — other classes
//
// Why this matters: Arduboy bugs that cause one of these signals are
// often invisible in source review — they're caused by structural
// linker issues, miscompiled inline asm, ISR/main-thread register
// collisions, or buffer overflows. Bug 2 from
// docs/known-bugs/scene-paging-boot-recovery.md is the canonical
// example: the chip crashed at boot because .data was clobbered by a
// scene swap, and that crash manifests as AB_OOB_DEREF firing inside
// `draw_title` AFTER a scene swap and reset. The static linker-
// layout gate (scripts/check_paging_layout.py) catches that specific
// regression at link time. This runtime harness catches everything
// the static gate can't see and exercises post-reset paths the
// static gate can't reach.
//
// Scenarios:
//   --scenario=cold  — boot, idle 5 s. Catches init-path autobreaks.
//   --scenario=swap  — boot, press A (advance through title to the
//                      next scene), idle 3 s. Catches mid-swap and
//                      first-scene-swap autobreaks.
//   --scenario=swap-reset — boot, press A, advance, reset, idle 5 s.
//                      Catches Bug 2 / Bug 1 class regressions: the
//                      bank holds a non-TITLE scene's bytes at the
//                      moment of reset; boot 2's set_active must
//                      page TITLE back in cleanly.
//
// Build: see CMakeLists.txt next to this file. Pulls absim from the
// pinned `tools/ardens` submodule.
// Run: ardens_smoke <rpg.hex> <data.bin> [--scenario=NAME] [--seconds=N]

#include <absim.hpp>

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <memory>
#include <string>

namespace {

// Default seconds budget per scenario. Sized so cold boot has room to
// settle (vestigia load, sprite init, audio engine boot, frame loop)
// and scripted scenarios can step through input + the matching
// ~400 ms scene-swap-with-cover beat without rushing.
constexpr double DEFAULT_SECONDS = 5.0;

// One simulated millisecond per advance() tick. Smaller granularity
// catches autobreaks sooner without much overhead.
constexpr uint64_t TICK_PS = 1'000'000'000ull;

// Arduboy button-pin layout (active-low, default = released).
constexpr uint8_t PINB_RELEASED  = 0x10;
constexpr uint8_t PINE_RELEASED  = 0x40;
constexpr uint8_t PINF_RELEASED  = 0xF0;
constexpr uint8_t PINE_A_PRESSED = 0x00;  // bit 6 cleared

const char* kAutobreakName(int idx) {
  switch (idx) {
  case absim::AB_BREAK: return "BREAK";
  case absim::AB_STACK_OVERFLOW: return "STACK_OVERFLOW";
  case absim::AB_NULL_DEREF: return "NULL_DEREF";
  case absim::AB_NULL_REL_DEREF: return "NULL_REL_DEREF";
  case absim::AB_OOB_DEREF: return "OOB_DEREF";
  case absim::AB_OOB_EEPROM: return "OOB_EEPROM";
  case absim::AB_OOB_IJMP: return "OOB_IJMP";
  case absim::AB_OOB_PC: return "OOB_PC";
  case absim::AB_UNKNOWN_INSTR: return "UNKNOWN_INSTR";
  case absim::AB_SPI_WCOL: return "SPI_WCOL";
  case absim::AB_FX_BUSY: return "FX_BUSY";
  default: return "UNKNOWN";
  }
}

// Load the FX data .bin via Ardens' own load_file path. We pass the
// path with its .bin extension so load_file routes through load_bin
// + reload_fx, which places the data at the expected high offset
// near the end of the simulated W25Q128 (the Arduboy FX driver reads
// from there). Writing to FX offset 0 directly would put the bytes
// at the wrong address and `data_flash::read()` would return zeros.
bool load_fx(absim::arduboy_t& a, const char* path) {
  std::ifstream f(path, std::ios::in | std::ios::binary);
  if (!f) {
    std::fprintf(stderr, "ardens_smoke: failed to open FX data: %s\n", path);
    return false;
  }
  std::string err = a.load_file(path, f);
  if (!err.empty()) {
    std::fprintf(stderr, "ardens_smoke: FX load error: %s\n", err.c_str());
    return false;
  }
  return true;
}

void set_buttons_default(absim::arduboy_t& a) {
  a.cpu.PINB() = PINB_RELEASED;
  a.cpu.PINE() = PINE_RELEASED;
  a.cpu.PINF() = PINF_RELEASED;
}

// Run for `ms` simulated milliseconds, polling for autobreak each
// tick. Returns true if no autobreak fired, false otherwise.
bool advance_ms(absim::arduboy_t& a, uint64_t ms) {
  for (uint64_t i = 0; i < ms; ++i) {
    a.advance(TICK_PS);
    a.cpu.sound_buffer.clear();
    if (a.cpu.autobreaks.any()) return false;
  }
  return true;
}

// Press the A button for `down_ms`, release for `up_ms`. Re-asserts
// pin state every tick so the simulated GPIO doesn't get rewritten
// by other peripheral activity.
bool press_a(absim::arduboy_t& a, uint64_t down_ms, uint64_t up_ms) {
  for (uint64_t i = 0; i < down_ms; ++i) {
    a.cpu.PINE() = PINE_A_PRESSED;
    a.advance(TICK_PS);
    a.cpu.sound_buffer.clear();
    if (a.cpu.autobreaks.any()) return false;
  }
  for (uint64_t i = 0; i < up_ms; ++i) {
    a.cpu.PINE() = PINE_RELEASED;
    a.advance(TICK_PS);
    a.cpu.sound_buffer.clear();
    if (a.cpu.autobreaks.any()) return false;
  }
  return true;
}

void report_failure(absim::arduboy_t& a, const char* scenario, uint64_t simulated_ms) {
  std::fprintf(stderr, "ardens_smoke: FAIL [%s] after %.3f s simulated\n", scenario,
               (double)simulated_ms / 1000.0);
  for (int i = 0; i < absim::AB_NUM; ++i) {
    if (a.cpu.autobreaks.test(i)) {
      std::fprintf(stderr, "  autobreak fired: %s (PC=0x%04X, SP=0x%04X)\n", kAutobreakName(i),
                   a.cpu.pc * 2u, (unsigned)((a.cpu.data[0x3e] << 8) | a.cpu.data[0x3d]));
    }
  }
}

int run_cold(absim::arduboy_t& a, double seconds) {
  set_buttons_default(a);
  a.reset();
  uint64_t total_ms = (uint64_t)(seconds * 1000.0);
  if (!advance_ms(a, total_ms)) {
    report_failure(a, "cold", total_ms);
    return 1;
  }
  std::fprintf(stdout, "ardens_smoke: OK [cold] (%.3f s simulated, no autobreak)\n", seconds);
  return 0;
}

int run_swap(absim::arduboy_t& a, double seconds) {
  set_buttons_default(a);
  a.reset();
  // Let title settle (skip splash + idle for a beat).
  if (!advance_ms(a, 1500)) {
    report_failure(a, "swap", 1500);
    return 1;
  }
  // A-press to advance off title.
  if (!press_a(a, 100, 100)) {
    report_failure(a, "swap", 1700);
    return 1;
  }
  // Cover animation + page_in (~400 ms) + uncover settles in ~1.5 s.
  uint64_t remaining = (uint64_t)(seconds * 1000.0) - 1700;
  if (!advance_ms(a, remaining)) {
    report_failure(a, "swap", 1700 + remaining);
    return 1;
  }
  std::fprintf(stdout, "ardens_smoke: OK [swap] (%.3f s simulated, no autobreak)\n", seconds);
  return 0;
}

int run_swap_reset(absim::arduboy_t& a, double seconds, uint16_t bank_addr) {
  // Phase 1: cold boot to title, A-press to swap.
  set_buttons_default(a);
  a.reset();
  // Generous title-settle window (3 s) so the press-A prompt blink has
  // started and the input poller has run plenty of frames before the
  // press. Earlier 1.5 s was too short for the FX-flash data load on
  // first boot.
  if (!advance_ms(a, 3000)) {
    report_failure(a, "swap-reset", 3000);
    return 1;
  }
  // Bank base = `__scene_bank_start`. The OVERLAY layout means all
  // four scenes share this VMA; whichever scene is currently paged in
  // is the one whose bytes live here. We sample before/after the A
  // press as a sanity check that an actual SPM-driven swap took place
  // — without one, this scenario can't exercise Bug 1 / 2. Caller
  // passes the address from `avr-nm` of the linked ELF so we don't
  // tie the harness to a specific layout.
  uint8_t pre_swap_lo = a.cpu.prog[bank_addr];
  uint8_t pre_swap_hi = a.cpu.prog[bank_addr + 1];

  // Hold A long enough that input::pressed sees the transition AND
  // the scene transition kicks off. ~500 ms = 30 frames @ 60 fps.
  if (!press_a(a, 500, 200)) {
    report_failure(a, "swap-reset", 3700);
    return 1;
  }
  // Let the swap finish (cover + page_in + uncover ~1500 ms total).
  if (!advance_ms(a, 2000)) {
    report_failure(a, "swap-reset", 5700);
    return 1;
  }

  uint8_t post_swap_lo = a.cpu.prog[bank_addr];
  uint8_t post_swap_hi = a.cpu.prog[bank_addr + 1];
  if (post_swap_lo == pre_swap_lo && post_swap_hi == pre_swap_hi) {
    std::fprintf(stderr,
                 "ardens_smoke: WARNING [swap-reset]: bank base unchanged "
                 "after A press (pre=0x%02X%02X). The smoke didn't drive a "
                 "scene swap — Bug 1 / Bug 2 paths are NOT being exercised. "
                 "Check that input::pressed(A) is reaching update_title_scene "
                 "and that title accepts A immediately.\n",
                 pre_swap_hi, pre_swap_lo);
  }

  // Phase 2: reset and re-boot. Flash content (whatever was last
  // SPM-written into the bank) is preserved across the reset, so this
  // exposes Bug 1 (boot must page_in TITLE on first set_active) and
  // Bug 2 (.data's flash source must survive the swap intact).
  a.reset();
  set_buttons_default(a);
  uint64_t elapsed   = 5700;
  uint64_t total_ms  = (uint64_t)(seconds * 1000.0);
  uint64_t remaining = (total_ms > elapsed) ? total_ms - elapsed : 1000;
  if (!advance_ms(a, remaining)) {
    report_failure(a, "swap-reset", elapsed + remaining);
    return 1;
  }

  // Positive recovery check. After the post-reset run, Bug 1's fix
  // should have re-paged TITLE into the bank — bank base bytes should
  // match what we saw at the start of phase 1 (TITLE's first
  // instruction). If they still match the post-swap bytes, page_in
  // didn't run (or didn't write); that's a Bug-1-class regression
  // that "no autobreak fired in N seconds" doesn't catch on its own
  // because wild execution from corrupt bytes can drift into a
  // benign idle loop. Same check rules out Bug 2 / 2.5: if .data's
  // flash source got clobbered by SPM page-write, current_id loads
  // as garbage on boot, the boot-path branch in set_active is
  // skipped, and bank stays as the previous scene's bytes.
  uint8_t recovered_lo = a.cpu.prog[bank_addr];
  uint8_t recovered_hi = a.cpu.prog[bank_addr + 1];
  if (recovered_lo != pre_swap_lo || recovered_hi != pre_swap_hi) {
    std::fprintf(stderr,
                 "ardens_smoke: FAIL [swap-reset]: bank base after post-reset "
                 "run is 0x%02X%02X, expected 0x%02X%02X (TITLE's first "
                 "instruction, the value before the swap). Bug 1 fix "
                 "(boot-path always page_in) and/or Bug 2 fix (.data LMA "
                 "outside the SPM-rewritable bank, page-aligned) is not "
                 "actually working in this build. Ardens would crash with "
                 "OOB-deref soon after on the original repro path.\n",
                 recovered_hi, recovered_lo, pre_swap_hi, pre_swap_lo);
    return 1;
  }
  std::fprintf(stdout,
               "ardens_smoke: OK [swap-reset] (%.3f s simulated, no autobreak, "
               "post-reset bank recovered correctly)\n",
               seconds);
  return 0;
}

}  // namespace

int main(int argc, char** argv) {
  const char* hex_path = nullptr;
  const char* fx_path  = nullptr;
  std::string scenario = "cold";
  double seconds       = DEFAULT_SECONDS;
  // Bank base. swap-reset needs this to sample the bytes at
  // __scene_bank_start. Caller (Makefile) extracts it from the linked
  // ELF via avr-nm so the harness isn't pinned to a specific layout.
  // 0 means "not set" — swap-reset will skip the sanity check.
  uint16_t bank_addr = 0;

  for (int i = 1; i < argc; ++i) {
    const char* a = argv[i];
    if (std::strncmp(a, "--scenario=", 11) == 0) {
      scenario = a + 11;
    } else if (std::strncmp(a, "--seconds=", 10) == 0) {
      seconds = std::atof(a + 10);
      if (seconds <= 0.0) {
        std::fprintf(stderr, "ardens_smoke: bad --seconds value\n");
        return 2;
      }
    } else if (std::strncmp(a, "--bank-addr=", 12) == 0) {
      bank_addr = (uint16_t)std::strtoul(a + 12, nullptr, 0);
    } else if (a[0] == '-') {
      std::fprintf(stderr, "ardens_smoke: unknown flag: %s\n", a);
      return 2;
    } else if (!hex_path) {
      hex_path = a;
    } else if (!fx_path) {
      fx_path = a;
    } else {
      std::fprintf(stderr, "ardens_smoke: extra positional arg: %s\n", a);
      return 2;
    }
  }

  if (!hex_path || !fx_path) {
    std::fprintf(stderr, "usage: ardens_smoke <rpg.hex> <data.bin>\n"
                         "  [--scenario=cold|swap|swap-reset]\n"
                         "  [--seconds=N]\n"
                         "  [--bank-addr=0xNNNN]  (required for swap-reset)\n"
                         "  exit 0 = no autobreak fired within budget\n"
                         "  exit 1 = an autobreak fired (a real bug)\n"
                         "  exit 2 = setup error (bad args / missing files)\n");
    return 2;
  }

  auto arduboy = std::make_unique<absim::arduboy_t>();

  std::ifstream hex(hex_path);
  if (!hex) {
    std::fprintf(stderr, "ardens_smoke: failed to open hex: %s\n", hex_path);
    return 2;
  }
  std::string err = arduboy->load_file(hex_path, hex);
  if (!err.empty()) {
    std::fprintf(stderr, "ardens_smoke: hex load error: %s\n", err.c_str());
    return 2;
  }
  if (!load_fx(*arduboy, fx_path)) return 2;

  // Enable every autobreak class except BREAK (which fires only on
  // explicit `break` instructions, not bugs).
  arduboy->cpu.enabled_autobreaks.set();
  arduboy->cpu.enabled_autobreaks.reset(absim::AB_BREAK);

  if (scenario == "cold") return run_cold(*arduboy, seconds);
  if (scenario == "swap") return run_swap(*arduboy, seconds);
  if (scenario == "swap-reset") {
    if (bank_addr == 0) {
      std::fprintf(stderr, "ardens_smoke: swap-reset requires --bank-addr=0xNNNN "
                           "(extract from linked ELF via "
                           "`avr-nm rpg.elf | grep __scene_bank_start`).\n");
      return 2;
    }
    return run_swap_reset(*arduboy, seconds, bank_addr);
  }

  std::fprintf(stderr, "ardens_smoke: unknown scenario: %s\n", scenario.c_str());
  return 2;
}
