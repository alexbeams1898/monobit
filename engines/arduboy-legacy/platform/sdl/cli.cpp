// CLI parser: argv -> game::DebugCfg. PC-only dev tooling.
//
// Supported flags (all optional):
//   --state=<NAME>        jump to a State enum value (e.g. MAIN_MENU, PLAYING)
//   --wave=<N>            set current wave (implies PLAYING if unset)
//   --spawn=<SPRITE>      spawn a shade by sprite name (forces PLAYING)
//   --stats=hp,dmg,fire   set all three stat levels
//   --sangue=<N>          set sangue_vessel (u16)
//   --bullet=<0-4>        set equipped bullet tier
//   --wipe                reset meta character (writes storage)
//   --help                print usage and exit
//
// Sprite-name lookup is intentionally narrow — only the shade roster
// (ENE_WRAITH .. BOSS_LUCIFER). Not every sprite id is spawnable in
// a way that makes sense for testing.

#include "cli.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace cli {

namespace {

struct NameEntry {
  const char* name;
  u8 value;
};

// Mirror of game.cpp's State enum. Hand-kept — when a new state lands,
// add it here. The parser errors out cleanly on unknown names so a
// typo'd flag fails fast rather than silently becoming state 0.
const NameEntry STATE_NAMES[] = {
    {"TITLE", 0},        {"NAME_ENTRY", 1},    {"MAIN_MENU", 2},       {"UPGRADE_MENU", 3},
    {"STATS_SCREEN", 4}, {"SHADES_SCREEN", 5}, {"NUMERALS_SCREEN", 6}, {"LEXICON_SCREEN", 7},
    {"TEXT_SCREEN", 8},  {"TUTORIAL", 9},      {"GATE_CARD", 10},      {"CIRCLE_CARD", 11},
    {"PLAYING", 12},     {"GUIDE_SCREEN", 13}, {"PAUSED", 14},         {"SECOND_DEATH", 15},
};

// Spawnable sprite ids — the shade roster (ENE_* + BOSS_*). Numeric
// values match sprites.h. Using names keeps the CLI human-readable.
const NameEntry SPRITE_NAMES[] = {
    {"ENE_WRAITH", 6},    {"ENE_IMP", 7},        {"ENE_GORGON", 8},       {"ENE_HARPY", 9},
    {"ENE_CERBERUS", 10}, {"ENE_MINOTAUR", 11},  {"ENE_LUST_SPIRIT", 12}, {"ENE_GLUTTON", 13},
    {"ENE_HOARDER", 14},  {"ENE_WRATHFUL", 15},  {"ENE_HERETIC", 16},     {"ENE_TRAITOR", 17},
    {"BOSS_MINOS", 18},   {"BOSS_CERBERUS", 19}, {"BOSS_PLUTO", 20},      {"BOSS_PHLEGYAS", 21},
    {"BOSS_DIS", 22},     {"BOSS_MINOTAUR", 23}, {"BOSS_GERYON", 24},     {"BOSS_GIANTS", 25},
    {"BOSS_LUCIFER", 26},
};

bool lookup(const NameEntry* table, int n, const char* name, u8& out) {
  for (int i = 0; i < n; ++i) {
    if (std::strcmp(table[i].name, name) == 0) {
      out = table[i].value;
      return true;
    }
  }
  return false;
}

const char* prefix_match(const char* arg, const char* flag) {
  // Returns pointer past "--flag=" if arg starts with it, else nullptr.
  const int flen = (int)std::strlen(flag);
  if (std::strncmp(arg, flag, flen) == 0 && arg[flen] == '=') {
    return arg + flen + 1;
  }
  return nullptr;
}

void print_usage() {
  std::fprintf(stderr,
               "mono-sdl — Dantean 1-bit ARPG (PC build)\n"
               "\n"
               "Usage: mono-sdl [flags]\n"
               "\n"
               "Debug/test overrides (transient — do not persist to save):\n"
               "  --state=<NAME>        jump to a State (e.g. MAIN_MENU, PLAYING, SECOND_DEATH)\n"
               "  --wave=<N>            set current wave number\n"
               "  --spawn=<SPRITE>      spawn a shade by sprite name (forces PLAYING)\n"
               "  --stats=hp,dmg,fire   set all three stat levels at once\n"
               "  --sangue=<N>          set sangue_vessel (spendable now)\n"
               "  --total-sangue=<N>    set total_sangue_earned (lifetime, RECKONING)\n"
               "  --runs=<N>            set total_runs (lifetime)\n"
               "  --kills=<N>           set total_kills (lifetime shades felled)\n"
               "  --keepers=<N>         set total_keepers_felled (lifetime bosses)\n"
               "  --bullet=<0-4>        set equipped bullet tier\n"
               "  --wipe                reset meta character (WRITES storage)\n"
               "  --no-enemies          suppress all enemy/boss spawns (sprite test)\n"
               "  --record-audio=<PATH> dump SDL speaker output to a 16-bit mono\n"
               "                        44.1kHz WAV file. Recording continues until\n"
               "                        the process exits — close the window with\n"
               "                        Esc / X to flush the file header.\n"
               "  --help                print this help and exit\n"
               "\n"
               "Example:\n"
               "  mono-sdl --state=PLAYING --wave=7 --stats=15,15,15\n"
               "  mono-sdl --spawn=BOSS_LUCIFER --sangue=999\n");
}

}  // namespace

bool parse(int argc, char** argv, game::DebugCfg& out) {
  for (int i = 1; i < argc; ++i) {
    const char* a = argv[i];

    if (std::strcmp(a, "--help") == 0 || std::strcmp(a, "-h") == 0) {
      print_usage();
      return false;
    }
    if (std::strcmp(a, "--wipe") == 0) {
      out.wipe = true;
      continue;
    }
    if (std::strcmp(a, "--no-enemies") == 0) {
      out.no_enemies = true;
      continue;
    }

    if (const char* v = prefix_match(a, "--state")) {
      if (!lookup(STATE_NAMES, (int)(sizeof(STATE_NAMES) / sizeof(*STATE_NAMES)), v, out.state)) {
        std::fprintf(stderr, "error: unknown --state value '%s'\n", v);
        return false;
      }
      out.has_state = true;
      continue;
    }

    if (const char* v = prefix_match(a, "--wave")) {
      out.wave     = (u16)std::atoi(v);
      out.has_wave = true;
      continue;
    }

    if (const char* v = prefix_match(a, "--spawn")) {
      if (!lookup(SPRITE_NAMES, (int)(sizeof(SPRITE_NAMES) / sizeof(*SPRITE_NAMES)), v,
                  out.spawn_sprite_id)) {
        std::fprintf(stderr, "error: unknown --spawn sprite '%s'\n", v);
        return false;
      }
      out.has_spawn = true;
      continue;
    }

    if (const char* v = prefix_match(a, "--stats")) {
      int hp, dmg, fire;
      if (std::sscanf(v, "%d,%d,%d", &hp, &dmg, &fire) != 3) {
        std::fprintf(stderr, "error: --stats expects hp,dmg,fire (got '%s')\n", v);
        return false;
      }
      out.level_hp        = (u8)hp;
      out.level_damage    = (u8)dmg;
      out.level_fire_rate = (u8)fire;
      out.has_stats       = true;
      continue;
    }

    if (const char* v = prefix_match(a, "--sangue")) {
      out.sangue_vessel = (u16)std::atoi(v);
      out.has_sangue    = true;
      continue;
    }

    if (const char* v = prefix_match(a, "--total-sangue")) {
      out.total_sangue     = (u32)std::strtoul(v, nullptr, 10);
      out.has_total_sangue = true;
      continue;
    }

    if (const char* v = prefix_match(a, "--runs")) {
      out.total_runs = (u16)std::atoi(v);
      out.has_runs   = true;
      continue;
    }

    if (const char* v = prefix_match(a, "--kills")) {
      out.total_kills = (u16)std::atoi(v);
      out.has_kills   = true;
      continue;
    }

    if (const char* v = prefix_match(a, "--keepers")) {
      out.total_keepers = (u8)std::atoi(v);
      out.has_keepers   = true;
      continue;
    }

    if (const char* v = prefix_match(a, "--bullet")) {
      int t = std::atoi(v);
      if (t < 0 || t > 4) {
        std::fprintf(stderr, "error: --bullet must be 0..4 (got %d)\n", t);
        return false;
      }
      out.bullet_tier = (u8)t;
      out.has_bullet  = true;
      continue;
    }

    std::fprintf(stderr, "error: unknown argument '%s'\n", a);
    print_usage();
    return false;
  }

  // Validation: --spawn requires --state=PLAYING (or implies it). If the
  // caller explicitly picked a non-PLAYING state with --spawn, that's a
  // contradiction we should refuse rather than silently resolve.
  if (out.has_spawn) {
    if (out.has_state && out.state != 14 /* PLAYING */) {
      std::fprintf(stderr, "error: --spawn requires --state=PLAYING (or no --state)\n");
      return false;
    }
    out.has_state = true;
    out.state     = 14;  // PLAYING
  }
  // --wave alone implies PLAYING too.
  if (out.has_wave && !out.has_state) {
    out.has_state = true;
    out.state     = 14;
  }
  return true;
}

}  // namespace cli
