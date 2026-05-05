// The bullet-hell ARPG, MVP slice.
//
// State machine: PLAYING -> PAUSED (B button) -> PLAYING
//                PLAYING -> SECOND_DEATH (HP hits 0 or "End Run" from pause)
//                SECOND_DEATH -> PLAYING (A button)
//
// Per the project's entity-symmetry rule, every active thing in the world
// goes through one update loop and one draw loop, dispatched on `kind`.
// The pause/SECOND_DEATH states freeze the world by short-circuiting update
// before that loop runs.

#include "game.h"

#include "audio.h"
// #include "backgrounds.h"  // Disabled: 3 KB flash savings; see backgrounds.cpp.disabled
#include "clock.h"
#include "data_flash.h"
#include "data_offsets.h"
#include "direction.h"
#include "entity.h"
#include "fixed.h"
#include "font.h"
#include "framebuffer.h"
#include "images.h"
#include "input.h"
#include "lz77.h"
#include "progmem.h"
#include "scene.h"  // VTable, SCENE_FN, SCENE_DATA
#include "sprites.h"
#include "storage.h"
#include "tilemap.h"
#include "types.h"
#include "vestigia.h"

namespace {

// PROGMEM literals - bare-literal calls to font::draw_text leak the
// string into .data (RAM). One symbol per unique literal, declared
// here so all the call sites share the same backing bytes.
// Naming: LIT_<text>, with C=":" M="," B="!" D="." for punctuation
// (so "VITA" and "VITA:" do not collide).
const char LIT_ACPRESS_ON[] PROGMEM              = "A:PRESS ON";
const char LIT_ACPRESS_ON_BCTURN_BACK[] PROGMEM  = "A:PRESS ON  B:TURN BACK";
const char LIT_AVOW_THY_NAME[] PROGMEM           = "AVOW THY NAME";
const char LIT_BCTURN_BACK[] PROGMEM             = "B:TURN BACK";
const char LIT_BULLET[] PROGMEM                  = "BULLET";
const char LIT_DEEPEST[] PROGMEM                 = "DEEPEST";
const char LIT_FULL[] PROGMEM                    = "FULL";
const char LIT_FURIA[] PROGMEM                   = "FURIA";
const char LIT_IRA[] PROGMEM                     = "IRA";
const char LIT_NIHIL[] PROGMEM                   = "NIHIL";
const char LIT_OFFERINGS[] PROGMEM               = "OFFERINGS";
const char LIT_PILGRIMAGES[] PROGMEM             = "PILGRIMAGES";
const char LIT_PRICEC[] PROGMEM                  = "PRICE:";
const char LIT_RANK[] PROGMEM                    = "RANK";
const char LIT_RECKONING[] PROGMEM               = "RECKONING";
const char LIT_SANGUEC[] PROGMEM                 = "SANGUE:";
const char LIT_SOMMA_SANGUE[] PROGMEM            = "SOMMA SANGUE";
const char LIT_SOMMA_SHADES[] PROGMEM            = "SOMMA SHADES";
const char LIT_SOMMA_KEEPERS[] PROGMEM           = "SOMMA KEEPERS";
const char LIT_THE_ROAD_AHEAD_IS_DARKC[] PROGMEM = "THE ROAD AHEAD IS DARK:";
const char LIT_VITA[] PROGMEM                    = "VITA";
const char LIT_VITAC[] PROGMEM                   = "VITA:";
// Class names (nominative; sprite IDs index by class*3 + level - 1).
const char LIT_PENITENT[] PROGMEM = "PENITENT";
const char LIT_WRETCHED[] PROGMEM = "WRETCHED";
const char LIT_HERETIC[] PROGMEM  = "HERETIC";
// Per-class level names — see docs/design/ "The three classes"
// section for the arc each ladder traces. PENITENT walks Inf I→VII→XXIII;
// HERETIC stays in Canto X; WRETCHED runs III→V.
const char LIT_PILGRIM[] PROGMEM    = "PILGRIM";
const char LIT_BEARER[] PROGMEM     = "BEARER";
const char LIT_MANTLE[] PROGMEM     = "MANTLE";
const char LIT_APOSTATE[] PROGMEM   = "APOSTATE";
const char LIT_ZEALOT[] PROGMEM     = "ZEALOT";
const char LIT_TOMB[] PROGMEM       = "TOMB";
const char LIT_VAGRANT[] PROGMEM    = "VAGRANT";
const char LIT_STING[] PROGMEM      = "STING";
const char LIT_WIND[] PROGMEM       = "WIND";
const char LIT_BURDEN[] PROGMEM     = "BURDEN";
const char LIT_UNBURDENED[] PROGMEM = "UNBURDENED";
// "NIHIL" already exists below as LIT_NIHIL — used for the unburdened's
// burden row. Penitent's lore equivalent of "the unmade soul."

// Load a PROGMEM C-string at `pgm` (a flash address) into `out` (RAM).
// Stops at NUL or (cap - 1), always writes the terminator. Shared helper
// for any screen that renders strings out of PROGMEM (tutorial, TEXT, ...).
void copy_pgm_str(const char* pgm, char* out, u8 cap) {
  u8 i = 0;
  while (i < (u8)(cap - 1)) {
    char c = (char)pgm_read_byte(&pgm[i]);
    out[i] = c;
    if (c == 0) return;
    ++i;
  }
  out[cap - 1] = 0;
}

// ---------------------------------------------------------------- state ---

// State machine, top-level navigation:
//   TITLE -> A -> MAIN_MENU
//   MAIN_MENU -> NEW GAME -> PLAYING
//                UPGRADE   -> UPGRADE_MENU
//                STATS     -> STATS_SCREEN
//                NAME      -> NAME_ENTRY (only when no name saved)
//   PLAYING <-> PAUSED, PLAYING -> SECOND_DEATH
//   SECOND_DEATH -> RETRY -> PLAYING
//                MENU  -> MAIN_MENU
enum State : u8 {
  TITLE,
  NAME_ENTRY,
  MAIN_MENU,
  UPGRADE_MENU,
  STATS_SCREEN,
  SHADES_SCREEN,
  NUMERALS_SCREEN,  // GRIMOIRE → NUMERALS: list of Roman symbols, A opens detail
  LEXICON_SCREEN,   // GRIMOIRE → LEXICON: list of sin-words, A opens gloss
  TEXT_SCREEN,
  TUTORIAL,     // one-shot intro shown to brand-new pilgrims
  GATE_CARD,    // pre-run "gates of hell" transition before circle 1
  CIRCLE_CARD,  // brief title card shown when entering each circle
  PLAYING,
  GUIDE_SCREEN,  // pre-boss interlude: the Guide offers RELIC / OFFERINGS / CHALICE
  PAUSED,
  SECOND_DEATH,
  VESTIGIA_SCREEN,  // wood → VESTIGIA: list of saved traces (slot 0..N-1)
};

// Generic short-card timer (CIRCLE_CARD): counts down each frame, A skips.
// 90 frames = 1.5 s at 60 Hz.
u8 card_timer;
constexpr u8 CARD_FRAMES = 90;

// GATE_CARD runs a staged timeline instead of a flat countdown. Stage
// boundaries (in elapsed frames since GATE_CARD entry):
//   [0,   60)  fade the gate in via Bayer-mask reveal (light mode)
//   [60,  120) silent hold — gate stands, no inscription yet (1s of dread)
//   [120, 200) hold the gate; pilgrim's name flashes at top
//   [200, 260) + ABANDON appears (flash blink) at bottom-left
//   [260, 320) + ALL at bottom-center
//   [320, 380) + HOPE at bottom-right
//   [380, 385) FLASH white (5 frames — the descent punch)
//   [385, 445) HOLD dark gate (60 frames — "thou art in Hell now")
//   [445, 505) dither fade out, dark mode (60 frames)
//   >= 505     hand off to CIRCLE_CARD
// Press A any time -> skip to hand-off.
u16 gate_t;
constexpr u16 GATE_FADE_IN_END   = 60;
constexpr u16 GATE_SILENT_END    = GATE_FADE_IN_END + 60;    // 120
constexpr u16 GATE_NAME_END      = GATE_SILENT_END + 80;     // 200
constexpr u16 GATE_ABANDON_END   = GATE_NAME_END + 60;       // 260
constexpr u16 GATE_ALL_END       = GATE_ABANDON_END + 60;    // 320
constexpr u16 GATE_HOPE_END      = GATE_ALL_END + 60;        // 380
constexpr u16 GATE_FLASH_END     = GATE_HOPE_END + 5;        // 385
constexpr u16 GATE_DARK_HOLD_END = GATE_FLASH_END + 60;      // 445
constexpr u16 GATE_FADE_OUT_END  = GATE_DARK_HOLD_END + 60;  // 505

// Last circle the player saw a CIRCLE_CARD for. 0xFF = "no card shown yet
// this run." On every transition back to PLAYING we compare against the
// upcoming wave's circle and fire a card iff it differs.
u8 last_card_circle = 0xFF;

// True when UPGRADE_MENU was entered from GUIDE_SCREEN (so B returns to the
// dialog) rather than from MAIN_MENU (where B returns to main). Without
// this flag the mid-run upgrade path would bounce back to the title.
u8 upgrade_from_guide;

// GUIDE_SCREEN menu row counts depend on availability. Up to 3 options:
//   row 0..N-1 map to action indices; we use a small lookup so hidden
//   options (RELIC when full HP / broke) don't reserve a visible slot.
enum GuideAction : u8 {
  GA_RELIC     = 0,
  GA_OFFERINGS = 1,
  GA_CHALICE   = 2,
};
constexpr u16 RELIC_COST = 5;  // flat cost of a full heal at the relic

State state;
u16 wave;

// No persistent background state — backgrounds::render() streams straight
// from PROGMEM into fb::buffer each frame. CPU-for-RAM trade; on this
// chip RAM is the scarcer resource.
storage::BestRun best;        // best-run snapshot, EEPROM-persistent
storage::MetaCharacter meta;  // persistent character (loaded at boot, written on changes)
u8 wave_cooldown;
u8 spawn_pause_done;
u8 menu_index;          // 0..menu_count-1 for whichever menu is active
u16 run_sangue_earned;  // sangue collected this run (for lifetime stat); vessel updates live
u16 run_kills;          // kills this run; commits to meta total_kills on death
u8 run_bosses_felled;   // bosses felled this run; shown on the SECOND DEATH screen

// Name entry state (cursor position 0..2, current letter at each position).
u8 name_cursor;
char name_buf[storage::NAME_LEN];

// Upgrade-menu cursor (0=HP, 1=DMG, 2=FR).
u8 upgrade_cursor;

// Bestiary cursor (0..17) — selects which entry is highlighted in the list.
u8 shades_cursor;

// Reckoning state. The screen has three pages — page 0 = "this Pilgrim"
// (portrait + vestige + burden-name + VITA/IRA/FURIA), page 1 = full-
// screen portrait beat, page 2 = lifetime tally. Left/Right d-pad
// cycles between them. Page 0 reads meta.vestige + meta.burden
// directly — no preview cycle. The DEV_BURDEN compile-time flag (see
// Makefile) pre-fills meta on boot for testing without going through
// class-select.
u8 reckoning_page;                       // 0 = stats card, 1 = portrait beat, 2 = lifetime tally
u8 reckoning_anim_frame;                 // 0..1 — idle f0 ↔ f1 toggle on page 0
u8 reckoning_anim_timer;                 // ticks remaining on the current idle frame
constexpr u8 RECKONING_IDLE_TICKS = 30;  // matches PLAYING ANIM_IDLE cadence

// Sub-view flag: 0 = scrollable list, 1 = single-beast detail page.
// Kept as a u8 instead of a full top-level state since the navigation model
// (press A to enter detail, B to go back to list) is identical to how other
// screens would nest — no point burning a real state slot.
// SHADES sub-view: tree of three orthogonal pages, modeled as a single
// enum so invalid combinations (e.g. portrait without detail) are
// unrepresentable. Replaces the older two-flag scheme that left state
// stale across menu round-trips and caused a black-screen bug when
// shades_portrait was 1 while shades_detail was 0 on re-entry.
enum ShadesView : u8 {
  SHADES_LIST     = 0,  // scroll the list of names
  SHADES_DETAIL   = 1,  // selected entry's page (icon/portrait + stats)
  SHADES_PORTRAIT = 2,  // boss-only: full-frame portrait, reached from detail
};
u8 shades_view;

// First boss in the shades index. Minions occupy idx 0..11 (12 entries),
// bosses occupy idx 12..20 (9 entries, with LUCIFER at idx 20).
constexpr u8 SHADES_FIRST_BOSS = 12;

// NUMERALS / LEXICON encyclopedia state. Same shape as shades_cursor /
// shades_detail: scroll a list of named entries; A opens a detail page;
// B returns to the list (or back to GRIMOIRE when at the list level).
// Counts declared here so the input handlers (above the data tables) see
// them; the actual data lives further down with the other text content.
constexpr u8 NUMERALS_COUNT = 7;
constexpr u8 LEXICON_COUNT  = 8;
u8 numerals_cursor;
u8 lexicon_cursor;

// Pause-menu sub-state: 0 = normal pause, 1 = "Dost thou turn back?"
// confirm dialog shown after user picks UNTO THE WOOD. Avoids a whole
// new state for a two-option yes/no prompt.
u8 pause_confirming;

// Tutorial slideshow index: 0 = CONTROLS slide, 1 = THE DESCENT slide.
// Reuses the TEXT body renderers so no new asset strings are required;
// only the footer changes per slide.
u8 tutorial_slide;

// GRIMOIRE screen (state name TEXT_SCREEN for legacy reasons). Three-level
// navigation:
//   text_section == 0xFF -> top-level list (COMMANDS / LORE / NUMERALS /
//                                           LEXICON / SHADES)
//   text_section == 0xFE -> LORE sub-list (DESCENT / GUIDE / SANGUE / VIRTU)
//   text_section == 0..7 -> body page open for that section
// Section ids:
//   0 = COMMANDS, 1 = (retired LEXICON slot — empty), 2 = DESCENT,
//   3 = GUIDE, 4 = SANGUE, 5 = VIRTU, 6 = NUMERALS (Roman reference),
//   7 = LEXICON (Italian/Latin gloss).
// SHADES is a state-jump to SHADES_SCREEN; it has no section id.
// text_cursor is reused across all three list levels (reset when opening
// a sub-list so the cursor lands on row 0).
constexpr u8 TEXT_TOP_SECTIONS     = 5;  // COMMANDS, LORE, NUMERALS, LEXICON, SHADES
constexpr u8 TEXT_ABOUT_SECTIONS   = 4;  // DESCENT, GUIDE, SANGUE, VIRTU
constexpr u8 TEXT_SECTION_ABOUT    = 0xFE;
constexpr u8 TEXT_SECTION_LIST     = 0xFF;
constexpr u8 TEXT_SECTION_NUMERALS = 6;
constexpr u8 TEXT_SECTION_LEXICON  = 7;
u8 text_cursor;
u8 text_section;

// VESTIGIA_SCREEN cursor: which slot row is highlighted. 0..N-1 where
// N = vestigia::SLOT_COUNT. The screen is a scrollable list (more
// slots than fit on one 64-px screen at the chosen row height); the
// cursor moves through all rows linearly, with the visible window
// scrolling to keep it on-screen.
u8 vestigia_cursor;

// VESTIGIA_SCREEN popup state. When `vestigia_popup` is non-zero, an
// inverse-rendered popup box overlays the slot list with 2-3 options.
// Mode encoding (selected by which slot the cursor was on at A-press):
//   0 — no popup (slot list active)
//   1 — slot 0 (AUTOSAVE):    LOAD / CANCEL
//   2 — empty manual slot:    SAVE / CANCEL
//   3 — occupied manual slot: LOAD / OVERWRITE / CANCEL
u8 vestigia_popup        = 0;
u8 vestigia_popup_cursor = 0;

// First sprite id that's a shades entry (ENE_WRAITH).
constexpr u8 SHADES_FIRST_ID = sprites::ENE_WRAITH;
// 12 minions (ids 6..17) + 9 bosses (ids 18..26) = 21 entries. The sangue
// coin sprites at 27..30 are NOT beasts, so don't derive this from
// sprites::COUNT. Bitmap is 3 bytes (24 bits) — fits 21 with room to grow.
constexpr u8 SHADES_COUNT = 21;

// Mark sprite_id as seen in the meta shades bitfield. Called when an
// enemy/boss is spawned. Cheap: bit-twiddle + EEPROM write only when bit
// flips (to spare write cycles).
// Promoted to CORE: called from PLAY scene's spawn_enemy_at / spawn_boss
// (write side, marks newly-encountered enemies) AND from MAIN_MENU's
// draw_shades_list (read side, paints the "seen" badge). Cross-bank
// caller pair forces it into CORE per docs/scene-paging.md.
void shades_mark_seen(u8 sprite_id) {
  if (sprite_id < SHADES_FIRST_ID || sprite_id >= sprites::COUNT) return;
  u8 idx    = sprite_id - SHADES_FIRST_ID;
  u8 byte_i = idx >> 3;
  u8 bit_i  = idx & 7;
  u8 mask   = (u8)(1 << bit_i);
  if (!(meta.shades[byte_i] & mask)) {
    meta.shades[byte_i] |= mask;
    storage::write_meta(meta);
  }
}

// Bestiary names live in PROGMEM (otherwise we'd burn ~250 bytes RAM).
// Per-shade name + description, individual PROGMEM strings. Pointer table
// indexes by sprite-id-order (ene_wraith..boss_lucifer = 6..26). Switching
// from fixed-stride 2D arrays (12-wide name, 32-wide desc) to per-string
// PROGMEM saves ~150 B of flash by dropping null padding on shorter entries.
const char SH_N00[] PROGMEM                          = "WRAITH";
const char SH_N01[] PROGMEM                          = "LUST SPIRIT";
const char SH_N02[] PROGMEM                          = "WORM";
const char SH_N03[] PROGMEM                          = "SWINE";
const char SH_N04[] PROGMEM                          = "JOUSTER";
const char SH_N05[] PROGMEM                          = "PROFLIGATE";
const char SH_N06[] PROGMEM                          = "BRAWLER";
const char SH_N07[] PROGMEM                          = "TOMB SHADE";
const char SH_N08[] PROGMEM                          = "HARPY";
const char SH_N09[] PROGMEM                          = "CENTAUR";
const char SH_N10[] PROGMEM                          = "MALEBRANCHE";
const char SH_N11[] PROGMEM                          = "ICE TRAITOR";
const char SH_N12[] PROGMEM                          = "CHARON";
const char SH_N13[] PROGMEM                          = "MINOS";
const char SH_N14[] PROGMEM                          = "CERBERUS";
const char SH_N15[] PROGMEM                          = "PLUTUS";
const char SH_N16[] PROGMEM                          = "PHLEGYAS";
const char SH_N17[] PROGMEM                          = "MEDUSA";
const char SH_N18[] PROGMEM                          = "MINOTAUR";
const char SH_N19[] PROGMEM                          = "GERYON";
const char SH_N20[] PROGMEM                          = "LUCIFER";
const char* const SHADES_NAMES[SHADES_COUNT] PROGMEM = {
    SH_N00, SH_N01, SH_N02, SH_N03, SH_N04, SH_N05, SH_N06, SH_N07, SH_N08, SH_N09, SH_N10,
    SH_N11, SH_N12, SH_N13, SH_N14, SH_N15, SH_N16, SH_N17, SH_N18, SH_N19, SH_N20,
};

// Description buffer is sized for the longest entry; callers pass at least
// this many bytes.
constexpr u8 SHADES_DESC_LEN                         = 32;
const char SH_D00[] PROGMEM                          = "VIRTUOUS, UNBAPTIZED";
const char SH_D01[] PROGMEM                          = "ENDLESS WINDS";
const char SH_D02[] PROGMEM                          = "COLD FILTHY RAIN";
const char SH_D03[] PROGMEM                          = "GLUTTONOUS MUCK";
const char SH_D04[] PROGMEM                          = "HOARDS GOLD FOREVER";
const char SH_D05[] PROGMEM                          = "WASTED ALL IN LIFE";
const char SH_D06[] PROGMEM                          = "BLACK MARSH";
const char SH_D07[] PROGMEM                          = "BURNING GRAVES";
const char SH_D08[] PROGMEM                          = "WOOD OF SELF-SLAYERS";
const char SH_D09[] PROGMEM                          = "BOWMAN OF PHLEGETHON";
const char SH_D10[] PROGMEM                          = "DEMON OF THE PITCH";
const char SH_D11[] PROGMEM                          = "FROZEN IN COCYTUS";
const char SH_D12[] PROGMEM                          = "FERRYMAN OF THE DAMNED";
const char SH_D13[] PROGMEM                          = "JUDGE OF LOST SOULS";
const char SH_D14[] PROGMEM                          = "THREE-HEADED HOUND OF HELL";
const char SH_D15[] PROGMEM                          = "KEEPER OF STOLEN GOLD";
const char SH_D16[] PROGMEM                          = "BOATMAN OF THE STYX";
const char SH_D17[] PROGMEM                          = "GAZE THAT TURNS TO STONE";
const char SH_D18[] PROGMEM                          = "BEAST OF THE RED RIVER";
const char SH_D19[] PROGMEM                          = "MONSTER WITH HONEST FACE";
const char SH_D20[] PROGMEM                          = "EMPEROR OF DOLOROUS REALM";
const char* const SHADES_DESCS[SHADES_COUNT] PROGMEM = {
    SH_D00, SH_D01, SH_D02, SH_D03, SH_D04, SH_D05, SH_D06, SH_D07, SH_D08, SH_D09, SH_D10,
    SH_D11, SH_D12, SH_D13, SH_D14, SH_D15, SH_D16, SH_D17, SH_D18, SH_D19, SH_D20,
};

// Read a shades description; buffer must be >= SHADES_DESC_LEN.
SCENE_FN(MAIN_MENU) void shades_desc_to_ram(u8 idx, char* out) {
  const char* p = (const char*)pgm_read_ptr(&SHADES_DESCS[idx]);
  for (u8 i = 0; i < SHADES_DESC_LEN - 1; ++i) {
    out[i] = (char)pgm_read_byte(&p[i]);
    if (out[i] == 0) return;
  }
  out[SHADES_DESC_LEN - 1] = 0;
}

ent::Entity* player;

// --------------------------------------------------------------- tuning ---
//
// All starting stats live here so they're easy to tune. Per the design doc,
// the same three stats (hp / fire_rate / damage) drive everything for both
// player and enemies — only the *values* differ.

// Movement speeds: fixed-point pixels/frame. 1.0 = 1 px/frame at 60 Hz = 60 px/sec.
constexpr fx PLAYER_SPEED      = fx_div(3, 4);  // 0.75 px/frame ≈ 45 px/sec
constexpr fx ENEMY_SPEED       = fx_div(1, 5);  // 0.2 px/frame ≈ 12 px/sec
constexpr fx PLAYER_BULLET_SPD = fx_div(2, 1);  // 2.0 px/frame
constexpr fx ENEMY_BULLET_SPD  = fx_div(3, 4);  // 0.75 px/frame (1/2 of old; very dodgeable)

// Stats. fire_rate is shots per 2 seconds (so rate=1 = one shot every 2 sec,
// rate=2 = 1 shot/sec, rate=6 = 3 shots/sec, rate=12 = 6 shots/sec).
// This finer unit lets enemies fire slowly without needing fractional rates.
// Base stats — what a level-0 (no upgrades) character gets.
constexpr u8 PLAYER_START_HP        = 6;
constexpr u8 PLAYER_START_FIRE_RATE = 6;  // 3 shots/sec
constexpr u8 PLAYER_START_DAMAGE    = 1;

// ---- SFX library --------------------------------------------------------
// All effects are 5-byte sfxr-style descriptors (start freq, slope, duration).
// Tuned by ear; easy to retune without recompiling much.
//
// PROGMEM so the 35 B of SFX tables don't sit in RAM. play() takes a
// PROGMEM pointer (`&SFX_*`) and copies the descriptor into its
// RAM-resident active-state slot.
const audio::Sfx SFX_SHOOT PROGMEM      = {1200, -40, 4};  // short upward chirp ending high
const audio::Sfx SFX_HIT_ENEMY PROGMEM  = {800, -30, 5};   // soft downward zap
const audio::Sfx SFX_KILL_ENEMY PROGMEM = {600, -25, 8};   // descending thunk
const audio::Sfx SFX_PLAYER_HIT PROGMEM = {200, 20, 10};   // harsh rising buzz
const audio::Sfx SFX_DEATH PROGMEM      = {400, -15, 30};  // long descending wail
const audio::Sfx SFX_MENU PROGMEM       = {700, -120, 3};  // muted tok — bootfall on stone
const audio::Sfx SFX_PICKUP PROGMEM     = {1500, 60, 6};   // bright chime up

// Per-level stat boosts. With cap=10, max stats become:
//   HP        = 6 + 10*1 = 16
//   FIRE_RATE = 6 + 10*1 = 16  (~8 shots/sec when holding A)
//   DAMAGE    = 1 + 10*1 = 11
// Furia (fire_rate) also drives run speed — see player_speed_for_fire_rate
// at the use site. Cadence and motion scale together so leveling furia
// feels like a single "go faster" knob.
constexpr u8 PER_LEVEL_HP        = 1;
constexpr u8 PER_LEVEL_FIRE_RATE = 1;
constexpr u8 PER_LEVEL_DAMAGE    = 1;
constexpr u8 MAX_UPGRADE_LEVEL   = 10;

// ---- Player animation state machine -------------------------------------
// The animation strips are drawn right-facing only; the renderer mirrors
// horizontally when the player faces left. Three states from penitent
// L1's saved strips in art/animations/:
//   IDLE   — no input. 2 frames, slow bob.
//   WALK   — d-pad held. 4-frame leg cycle.
//   ATTACK — A pressed/held. 2 frames, snappy. Returns to prior state.
// Frame durations are in update ticks (60/sec). Tunable per-state below.
enum PlayerAnimState : u8 { ANIM_IDLE = 0, ANIM_WALK = 1, ANIM_ATTACK = 2 };

// Per-state frame table. Each entry pairs a sprite_id with a duration
// (in 60Hz update ticks). Lengths are state-specific:
//   IDLE   = 2 frames * 30 ticks each = 1 sec per cycle (calm)
//   WALK   = 4 frames * 6 ticks each  = ~0.4 sec per cycle (walking pace)
//   ATTACK = 2 frames * 4 ticks each  = ~0.13 sec total (snappy)
struct AnimFrame {
  u8 sprite_id;
  u8 duration;
};

// Per-vestige animation frame tables. The renderer dispatches via
// meta.vestige at draw time; states (idle/walk/attack) and frame
// counts are uniform across vestiges so the consumer code doesn't
// branch per-vestige — only the table pointer changes.
//
// PENITENT L1 — Vita-focused. Hood + staff.
const AnimFrame ANIM_PEN_IDLE_FRAMES[2] PROGMEM = {
    {sprites::PEN_L1_IDLE_F0, 30},
    {sprites::PEN_L1_IDLE_F1, 30},
};
const AnimFrame ANIM_PEN_WALK_FRAMES[4] PROGMEM = {
    {sprites::PEN_L1_WALK_F0, 10},
    {sprites::PEN_L1_WALK_F1, 10},
    {sprites::PEN_L1_WALK_F2, 10},
    {sprites::PEN_L1_WALK_F3, 10},
};
const AnimFrame ANIM_PEN_ATTACK_FRAMES[2] PROGMEM = {
    {sprites::PEN_L1_ATTACK_F0, 6},
    {sprites::PEN_L1_ATTACK_F1, 6},
};

// UNBURDENED — pre-class wraith. Same state shape as the classes
// (idle x2, walk x4, attack x2). Only the sprite_ids differ.
const AnimFrame ANIM_UB_IDLE_FRAMES[2] PROGMEM = {
    {sprites::UB_IDLE_F0, 30},
    {sprites::UB_IDLE_F1, 30},
};
const AnimFrame ANIM_UB_WALK_FRAMES[4] PROGMEM = {
    {sprites::UB_WALK_F0, 10},
    {sprites::UB_WALK_F1, 10},
    {sprites::UB_WALK_F2, 10},
    {sprites::UB_WALK_F3, 10},
};
const AnimFrame ANIM_UB_ATTACK_FRAMES[2] PROGMEM = {
    {sprites::UB_ATTACK_F0, 6},
    {sprites::UB_ATTACK_F1, 6},
};

// State tables — pointer + length. Indexed by PlayerAnimState. Stored in
// PROGMEM so the (ptr, length) pairs themselves don't sit in RAM.
struct AnimDef {
  const AnimFrame* frames;
  u8 length;
};
// Per-vestige state tables. Indexed first by PlayerAnimState (idle=0
// / walk=1 / attack=2), with a second table-of-tables per vestige
// chosen at lookup time. WRETCHED + HERETIC fall back to PENITENT
// frames until their anim sets are authored — they'll have their own
// AnimDef tables here once the strips ship.
const AnimDef ANIM_DEFS_PENITENT[3] PROGMEM = {
    {ANIM_PEN_IDLE_FRAMES, 2},
    {ANIM_PEN_WALK_FRAMES, 4},
    {ANIM_PEN_ATTACK_FRAMES, 2},
};
const AnimDef ANIM_DEFS_UNBURDENED[3] PROGMEM = {
    {ANIM_UB_IDLE_FRAMES, 2},
    {ANIM_UB_WALK_FRAMES, 4},
    {ANIM_UB_ATTACK_FRAMES, 2},
};

// Returns the active AnimDef table for the player's current vestige.
// Wretched + Heretic temporarily share Penitent's table until their
// anim strips are authored — visually wrong but functionally fine
// (will be a TODO for "wire <vestige>'s anims" once the strips ship).
inline const AnimDef* active_anim_defs() {
  if (meta.vestige == storage::VESTIGE_UNBURDENED) return ANIM_DEFS_UNBURDENED;
  return ANIM_DEFS_PENITENT;  // PENITENT (canonical) + WRETCHED/HERETIC fallback
}

// Live state. Per-player only — these are 4 B globals rather than 4 B *
// POOL_SIZE = 96 B per-entity fields. Updated in update_player(),
// consumed in the player render path.
u8 player_anim_state       = ANIM_IDLE;
u8 player_anim_frame       = 0;
u8 player_anim_timer       = 0;  // ticks remaining on current frame
u8 player_anim_facing_left = 0;  // 1 = facing left (mirror render)

// Last-state-before-attack so ATTACK returns to the right state when
// done (e.g. fire while walking → attack → resume walking).
u8 player_anim_prev_state = ANIM_IDLE;

inline u8 anim_def_length(u8 state) {
  return pgm_read_byte(&active_anim_defs()[state].length);
}

// Read the AnimFrame* stored in PROGMEM. pgm_read_ptr handles the AVR
// 16-bit-flash-address vs PC 64-bit-pointer split correctly — see
// engine/progmem.h. Reading via pgm_read_word would silently truncate
// host pointers on PC.
inline const AnimFrame* anim_def_frames_ptr(u8 state) {
  return (const AnimFrame*)pgm_read_ptr(&active_anim_defs()[state].frames);
}

inline u8 anim_frame_sprite_id(u8 state, u8 frame_idx) {
  const AnimFrame* base = anim_def_frames_ptr(state);
  return pgm_read_byte(&base[frame_idx].sprite_id);
}

inline u8 anim_frame_duration(u8 state, u8 frame_idx) {
  const AnimFrame* base = anim_def_frames_ptr(state);
  return pgm_read_byte(&base[frame_idx].duration);
}

// Set the current animation state and reset frame/timer to start. Call
// this when input changes the player's intent (idle ↔ walk ↔ attack).
// No-op if already in the requested state — preserves the in-flight
// frame so toggling between states from the same input doesn't reset.
SCENE_FN(PLAY) void set_player_anim(u8 new_state) {
  if (player_anim_state == new_state) return;
  // Remember WALK/IDLE so ATTACK can return to it when the attack ends.
  if (new_state == ANIM_ATTACK) {
    player_anim_prev_state = player_anim_state;
  }
  player_anim_state = new_state;
  player_anim_frame = 0;
  player_anim_timer = anim_frame_duration(new_state, 0);
}

// Advance the animation timer by one tick. When the timer hits 0, move
// to the next frame; if we wrap around the end of the strip and we're
// in ATTACK, return to the prior state (IDLE/WALK).
SCENE_FN(PLAY) void tick_player_animation() {
  if (player_anim_timer > 0) --player_anim_timer;
  if (player_anim_timer == 0) {
    u8 len        = anim_def_length(player_anim_state);
    u8 next_frame = (u8)(player_anim_frame + 1);
    if (next_frame >= len) {
      // Strip ended. ATTACK is one-shot: return to prior state.
      if (player_anim_state == ANIM_ATTACK) {
        // Re-enter prior state cleanly (force state change so frame/timer reset).
        u8 prev = player_anim_prev_state;
        if (prev == ANIM_ATTACK) prev = ANIM_IDLE;
        player_anim_state = prev;
        player_anim_frame = 0;
        player_anim_timer = anim_frame_duration(player_anim_state, 0);
        return;
      }
      // IDLE/WALK loop forever.
      next_frame = 0;
    }
    player_anim_frame = next_frame;
    player_anim_timer = anim_frame_duration(player_anim_state, next_frame);
  }
}

// Upgrade cost: quadratic in current level. cost = 10 * (level + 1)^2.
// HP costs 10 / 40 / 90 / 160 / 250 / ... per tier.
inline u16 upgrade_cost(u8 current_level) {
  u16 n = (u16)(current_level + 1);
  return (u16)(10 * n * n);
}

// Bullet ladder. The Pilgrim calls them all "bullets" — see docs/design/ —
// but Hell knows them by their canto-coded names. Tier 0 (STONE) is the
// default; higher tiers unlock by spending sangue in OFFERINGS.
constexpr u8 BULLET_TIER_COUNT                            = 5;
const u8 BULLET_SPRITE_BY_TIER[BULLET_TIER_COUNT] PROGMEM = {
    sprites::DART_STONE,  // 0 — Wastrels, Canto VII
    sprites::DART_ARROW,  // 1 — Centaurs of Phlegethon, Canto XII
    sprites::DART_HOOK,   // 2 — Malebranche, Canto XXI
    sprites::DART_SHARD,  // 3 — Ice traitors of Cocytus, Canto XXXII
    sprites::DART_WIND,   // 4 — Lust spirits, Canto V
};
const char BUL_NAME_0[] PROGMEM                           = "STONE";
const char BUL_NAME_1[] PROGMEM                           = "ARROW";
const char BUL_NAME_2[] PROGMEM                           = "HOOK";
const char BUL_NAME_3[] PROGMEM                           = "SHARD";
const char BUL_NAME_4[] PROGMEM                           = "WIND";
const char* const BULLET_NAMES[BULLET_TIER_COUNT] PROGMEM = {
    BUL_NAME_0, BUL_NAME_1, BUL_NAME_2, BUL_NAME_3, BUL_NAME_4,
};

// Bullet unlock cost: linear in tier. Tier 1 (ARROW) costs 50 sangue, each
// next tier doubles. Cheaper than stat upgrades early so the Pilgrim can
// taste a different bullet quickly; expensive at the top so WIND is the
// late-game ambition. cost = 50 * 2^(tier-1) for tier >= 1; tier 0 is free.
inline u16 bullet_cost(u8 next_tier) {
  if (next_tier == 0) return 0;  // STONE is the default; never priced
  u16 c = 50;
  for (u8 i = 1; i < next_tier; ++i)
    c = (u16)(c * 2);
  return c;  // 50 / 100 / 200 / 400 for tiers 1..4
}
constexpr u8 ENEMY_BASE_HP        = 2;  // >1 so pip bars show; 2 hits to kill
constexpr u8 ENEMY_BASE_FIRE_RATE = 1;  // 1 shot per 2 sec per enemy
constexpr u8 ENEMY_BASE_DAMAGE    = 1;

// Hard ceiling on enemy bullets in flight at once. Enemy shots that would
// exceed this cap simply don't spawn that frame — the enemy "shoots" but
// its bullet is suppressed. Keeps screen density manageable as enemy count
// scales with wave.
constexpr u8 MAX_ENEMY_BULLETS = 4;

// Ichor pickup tuning. Pure pickup behavior — drops sit where the enemy
// died and the player has to physically walk over them. No magnet. Lifetime
// is generous so a player who survives the wave can still loot afterward.
constexpr u8 SANGUE_LIFETIME_FRAMES = 240;  // ~4 sec before despawn

// Wave spacing.
constexpr u8 WAVE_SPAWN_DELAY = 60;

// Shop tuning. Heal costs scale with how much you're missing.
constexpr u16 HEAL_ONE_COST = 5;  // sangue for +1 HP

// fire_rate (shots per 2 sec) -> frames between shots. Higher = faster.
inline u8 fire_cooldown(u8 fire_rate) {
  if (fire_rate == 0) return 255;  // "never" (used for non-shooters)
  u16 c = (u16)120 / fire_rate;
  if (c > 255) c = 255;
  if (c < 2) c = 2;  // hard cap: 60 shots/sec
  return (u8)c;
}

// Furia also governs footspeed. Scaling (with PLAYER_SPEED = 0.75 px/frame):
//   level  0 (fire_rate=6)  ->  1.0×   PLAYER_SPEED  (0.75 px/frame)
//   level 10 (fire_rate=16) -> ~1.5×   PLAYER_SPEED  (1.125 px/frame)
// Bonus contribution = PLAYER_SPEED * (fire_rate - 6) / 20. We pin the
// minimum at fire_rate >= 6 (the player's floor) so enemies whose
// fire_rate happens to exceed 6 don't accidentally outrun the pilgrim.
// Enemy movement uses its own constants — this helper is player-only.
inline fx player_speed_for_fire_rate(u8 fire_rate) {
  if (fire_rate <= 6) return PLAYER_SPEED;
  u16 bonus_num = (u16)(fire_rate - 6);
  // (PLAYER_SPEED * bonus_num) / 20. PLAYER_SPEED ~192, bonus_num <= 10
  // (fire_rate caps at 16 per MAX_UPGRADE_LEVEL=10 + base 6). Max product
  // 1920 fits i16 — no i32 widening needed, which spares us a libgcc
  // pull-in on the divide.
  fx bonus = (fx)(((i16)PLAYER_SPEED * (i16)bonus_num) / 20);
  return (fx)(PLAYER_SPEED + bonus);
}

// All entities use PROGMEM-table sprites. Sizes mirror sprites::{widths,heights}
// for code clarity at use sites — single source of truth is the sprites table.
// As of the demon/human swap: player is the smaller humanoid, enemies are
// the bigger demons.
constexpr u8 PLAYER_SPRITE_W = 5;
constexpr u8 PLAYER_SPRITE_H = 8;
constexpr u8 ENEMY_SPRITE_W  = 8;
constexpr u8 ENEMY_SPRITE_H  = 12;
constexpr u8 BULLET_W        = 2;
constexpr u8 BULLET_H        = 2;

// --------------------------------------------------------------- helpers --

bool overlap(i16 ax, i16 ay, u8 aw, u8 ah, i16 bx, i16 by, u8 bw, u8 bh) {
  return ax < bx + (i16)bw && ax + (i16)aw > bx && ay < by + (i16)bh && ay + (i16)ah > by;
}

// Axis-aligned bounding box for collision math. All four fields packed in 6
// bytes (i16 + i16 + u8 + u8) so it returns by-value cheaply on AVR.
struct BBox {
  i16 x;
  i16 y;
  u8 w;
  u8 h;
};

// Width/height as drawn for collision. Reads directly from the sprite table
// so adding a new sprite of any size needs no game.cpp changes.
u8 entity_w(const ent::Entity& e) {
  if (e.sprite_id < sprites::COUNT) return sprites::width(e.sprite_id);
  return 1;
}
u8 entity_h(const ent::Entity& e) {
  // The HEIGHTS table now publishes visible pixel extents directly, so
  // BULLET (2 px) and the darts (3 px) need no special-case overrides
  // here — see sprites.cpp HEIGHTS comment.
  if (e.sprite_id < sprites::COUNT) return sprites::height(e.sprite_id);
  return 8;
}

// Bundled bbox fetch. `noinline` is deliberate: each call site otherwise
// inlines four PROGMEM reads + bounds checks (~30 B per site), and
// resolve_collisions has 8 such sites. Forcing one shared callee trades
// per-call overhead for a much smaller code footprint.
SCENE_FN(PLAY) BBox entity_bbox(const ent::Entity& e) {
  BBox b;
  b.x = fx_to_px(e.x);
  b.y = fx_to_px(e.y);
  b.w = entity_w(e);
  b.h = entity_h(e);
  return b;
}

// Promoted to CORE: called from update_gate_scene (via begin_play) at
// the GATE_CARD->PLAY transition, and from start_run (also CORE). The
// GATE-side caller would otherwise be a cross-bank call.
void spawn_player() {
  player = ent::spawn(ent::PLAYER);
  if (!player) return;
  player->x = fx_px(60);
  player->y = fx_px(32);
  // Stats = base + (purchased levels * per-level boost). Pulls from the
  // persistent meta character so a leveled-up character starts each new
  // run with their accumulated power.
  u8 hp             = (u8)(PLAYER_START_HP + meta.level_hp * PER_LEVEL_HP);
  u8 fr             = (u8)(PLAYER_START_FIRE_RATE + meta.level_fire_rate * PER_LEVEL_FIRE_RATE);
  u8 dmg            = (u8)(PLAYER_START_DAMAGE + meta.level_damage * PER_LEVEL_DAMAGE);
  player->hp        = hp;
  player->max_hp    = hp;
  player->fire_rate = fr;
  player->damage    = dmg;
  player->sprite_id = sprites::PLAYER;
  // Initial facing: east. Stored as the direction's unit vector so the
  // facing-pixel renderer + shoot logic can both read it directly.
  const dir::Vec east   = dir::read(dir::E);
  ent::player_facing_dx = east.dx;
  ent::player_facing_dy = east.dy;
}

// Circle index from wave number: 0 = limbo, 1 = lust, ..., 8 = treachery.
// Three waves per circle → boss every 3rd wave (per Canto XIII.17 "secondo
// giro" — Dante's explicit 3-round subdivision of Circle VII, which we
// extend thematically to every circle).
inline u8 circle_for_wave(u16 w) {
  u8 c = (u8)((w - 1) / 3);
  if (c > 8) c = 8;
  return c;
}

// 1..3: "round" inside the current circle. Wave 1 -> round 1, wave 3 -> 3.
inline u8 round_for_wave(u16 w) {
  return (u8)(((w - 1) % 3) + 1);
}

// Write a Roman numeral (1..9) into `out`. Returns chars written (max 3).
// All nine Inferno circles fit inside "IX"; we don't need the full Roman
// algorithm. Buffer must have space for 4 bytes.
//
// The numeral strings + the pointer table both live in PROGMEM so nothing
// from this helper burns RAM. Reads go through the progmem shim.
u8 format_roman(u8 v, char* out) {
  static const char R1[] PROGMEM            = "I";
  static const char R2[] PROGMEM            = "II";
  static const char R3[] PROGMEM            = "III";
  static const char R4[] PROGMEM            = "IV";
  static const char R5[] PROGMEM            = "V";
  static const char R6[] PROGMEM            = "VI";
  static const char R7[] PROGMEM            = "VII";
  static const char R8[] PROGMEM            = "VIII";
  static const char R9[] PROGMEM            = "IX";
  static const char* const romans[] PROGMEM = {R1, R2, R3, R4, R5, R6, R7, R8, R9};
  if (v < 1 || v > 9) {
    out[0] = 0;
    return 0;
  }
  const char* s = (const char*)pgm_read_ptr(&romans[v - 1]);
  u8 n          = 0;
  for (;;) {
    char c = (char)pgm_read_byte(&s[n]);
    if (!c) break;
    out[n++] = c;
  }
  out[n] = 0;
  return n;
}

// Roman numerals 1..3999. Standard subtractive notation; values above 3999
// require the vinculum (overline) which we can't render at this scale, so
// the caller must clamp first. Returns chars written (excluding NUL).
// Buffer must be >= 16 (longest value: 3888 = "MMMDCCCLXXXVIII" = 15 chars).
//
// Lookup tables in PROGMEM so the 26 bytes of constants don't burn .data.
const u16 ROMAN_VALS[13] PROGMEM     = {1000, 900, 500, 400, 100, 90, 50, 40, 10, 9, 5, 4, 1};
const char ROMAN_SYMS[13][3] PROGMEM = {
    {'M', 0, 0},   {'C', 'M', 0}, {'D', 0, 0},   {'C', 'D', 0}, {'C', 0, 0},
    {'X', 'C', 0}, {'L', 0, 0},   {'X', 'L', 0}, {'X', 0, 0},   {'I', 'X', 0},
    {'V', 0, 0},   {'I', 'V', 0}, {'I', 0, 0},
};
u8 format_roman_u16(u16 v, char* out) {
  if (v == 0 || v > 3999) {
    out[0] = 0;
    return 0;
  }
  u8 n = 0;
  for (u8 i = 0; i < 13; ++i) {
    const u16 step = pgm_read_word(&ROMAN_VALS[i]);
    while (v >= step) {
      v = (u16)(v - step);
      for (u8 k = 0; k < 3; ++k) {
        const char c = (char)pgm_read_byte(&ROMAN_SYMS[i][k]);
        if (!c) break;
        out[n++] = c;
      }
    }
  }
  out[n] = 0;
  return n;
}

// Formats wave into "<Roman>.<round>/3" into a tiny buffer. Used by the
// in-game HUD. Returns pixel width of the resulting string (for layout).
// Buffer must be >= 8 bytes.
u8 format_descent_short(u16 w, char* out) {
  // Format: "<CIRCLE>·<ROUND>" — middle-dot separator, both Roman.
  // Total-rounds omitted on purpose: Hell does not announce its rhythm
  // to the Pilgrim. The player learns round III = keeper by surviving
  // the first circle. Mystery is registered; explanation is the
  // Grimoire's job when it unlocks.
  u8 c     = (u8)(circle_for_wave(w) + 1);  // 1..9 for display
  u8 r     = round_for_wave(w);
  u8 n     = format_roman(c, out);
  out[n++] = '*';  // '*' renders as middle-dot (·) — single ornament
                   // shared with the SELVA OSCURA hub frieze.
  n += format_roman(r, out + n);
  out[n] = 0;
  return n;
}

// Forward decl: shared string helpers live in a later block (next to the
// menu renderer that originally defined strlen_). Hoisting just the decls
// avoids reflowing the file.
u8 strlen_(const char* s);
u8 strcpy_(char* out, const char* s);

// Forward decl: Roman renderer is defined alongside format_roman_u16 lower
// in the file; draw_hud (above) and other early renderers use it.
void draw_roman(i16 x_right, i16 y, u16 value);

// Spelled-out form for static screens: "CIRCLE III, ROUND II". Buffer >= 22.
// Both circle and round render as Roman numerals — ceremonial-display
// register, period-correct for a Dantean game's lore screens.
u8 format_descent_long(u16 w, char* out) {
  u8 c = (u8)(circle_for_wave(w) + 1);
  u8 r = round_for_wave(w);
  u8 n = strcpy_(out, "CIRCLE ");
  n += format_roman(c, out + n);
  n += strcpy_(out + n, ", ROUND ");
  n += format_roman(r, out + n);
  out[n] = 0;
  return n;
}

// Per-circle base enemy sprite. Indexed by circle (0..8). Read via
// pgm_read_byte — the `constexpr` goes away because that keyword implies
// a compile-time-addressable value, which fights PROGMEM on AVR.
const u8 BASE_ENEMY_BY_CIRCLE[9] PROGMEM = {
    sprites::ENE_WRAITH,       // 0  Limbo
    sprites::ENE_LUST_SPIRIT,  // 1  Lust
    sprites::ENE_WORM,         // 2  Gluttony
    sprites::ENE_JOUSTER,      // 3  Greed
    sprites::ENE_BRAWLER,      // 4  Wrath
    sprites::ENE_TOMB_SHADE,   // 5  Heresy
    sprites::ENE_HARPY,        // 6  Violence
    sprites::ENE_MALEBRANCHE,  // 7  Fraud
    sprites::ENE_ICE_TRAITOR,  // 8  Treachery
};

// Per-circle boss sprite, hp.
const u8 BOSS_BY_CIRCLE[9] PROGMEM = {
    sprites::BOSS_CHARON,   sprites::BOSS_MINOS,    sprites::BOSS_CERBERUS,
    sprites::BOSS_PLUTUS,   sprites::BOSS_PHLEGYAS, sprites::BOSS_MEDUSA,
    sprites::BOSS_MINOTAUR, sprites::BOSS_GERYON,   sprites::BOSS_LUCIFER,
};
const u8 BOSS_HP_BY_CIRCLE[9] PROGMEM = {
    8, 10, 12, 14, 16, 18, 22, 26, 40,  // scaling tier; lucifer hardest
};

// Boss damage and fire_rate are uniform across all bosses today — HP is the
// only per-circle scaling. Named constants so the shades detail page can
// report the same numbers spawn_boss() uses.
constexpr u8 BOSS_DAMAGE    = 1;
constexpr u8 BOSS_FIRE_RATE = 2;

SCENE_FN(PLAY) void spawn_enemy_at(i16 px, i16 py) {
  ent::Entity* e = ent::spawn(ent::ENEMY);
  if (!e) return;
  e->x         = fx_px(px);
  e->y         = fx_px(py);
  e->hp        = ENEMY_BASE_HP;
  e->max_hp    = ENEMY_BASE_HP;
  e->fire_rate = ENEMY_BASE_FIRE_RATE;
  e->damage    = ENEMY_BASE_DAMAGE;
  e->sprite_id = pgm_read_byte(&BASE_ENEMY_BY_CIRCLE[circle_for_wave(wave)]);
  u8 cd        = fire_cooldown(e->fire_rate);
  e->timer     = (u8)((px ^ (py << 1)) % cd);
  shades_mark_seen(e->sprite_id);
}

// Pick the sangue pickup-tier sprite for a given drop value.
// Mapping (Charon's-obol theme): 1-4 dot, 5-9 coin, 10-24 stack,
// 25-99 pentacoin, 100+ flaming coin.
inline u8 sangue_sprite_for_value(u8 v) {
  if (v < 5) return sprites::SANGUE_DROP;
  if (v < 10) return sprites::SANGUE_RIVULET;
  if (v < 25) return sprites::SANGUE_RIVER;
  if (v < 100) return sprites::SANGUE_FLOOD;
  return sprites::SANGUE_DELUGE;
}

// Spawn a sangue pickup at the given pixel coordinates with `count` worth
// of value carried. We encode the value in `damage` (unused on pickups)
// and the lifetime in `timer`. The sprite picked communicates the tier
// at a glance.
SCENE_FN(PLAY) void spawn_sangue(i16 px, i16 py, u8 value) {
  ent::Entity* e = ent::spawn(ent::PICKUP);
  if (!e) return;
  e->x         = fx_px(px);
  e->y         = fx_px(py);
  e->damage    = value;
  e->sprite_id = sangue_sprite_for_value(value);
  e->timer     = SANGUE_LIFETIME_FRAMES > 255 ? 255 : (u8)SANGUE_LIFETIME_FRAMES;
}

// Spawn a single boss enemy at center-top. Bosses use a different sprite
// and bigger HP/damage but go through the same Entity update path —
// per the entity-symmetry rule, "boss" is just data on an enemy.
SCENE_FN(PLAY) void spawn_boss(u8 sprite_id, u8 hp) {
  ent::Entity* e = ent::spawn(ent::ENEMY);
  if (!e) return;
  // Center horizontally, top of the playfield (just below HUD).
  u8 sw        = sprites::width(sprite_id);
  e->x         = fx_px((i16)((fb::WIDTH - sw) / 2));
  e->y         = fx_px(10);
  e->hp        = hp;
  e->max_hp    = hp;
  e->fire_rate = BOSS_FIRE_RATE;
  e->damage    = BOSS_DAMAGE;
  e->sprite_id = sprite_id;
  u8 cd        = fire_cooldown(e->fire_rate);
  e->timer     = (u8)(cd / 2);
  shades_mark_seen(sprite_id);
}

SCENE_FN(PLAY) void spawn_wave(u16 wave_num) {
#ifdef ENABLE_DEBUG_OVERRIDES
  if (game::debug_no_enemies()) return;
#endif
  // Every 3rd wave (3/6/9/12/...) is a boss for that circle.
  u8 c = circle_for_wave(wave_num);
  if (wave_num % 3 == 0) {
    spawn_boss(pgm_read_byte(&BOSS_BY_CIRCLE[c]), pgm_read_byte(&BOSS_HP_BY_CIRCLE[c]));
    return;
  }

  u8 count = (u8)(3 + wave_num);
  if (count > 12) count = 12;
  for (u8 i = 0; i < count; ++i) {
    u8 corner = (u8)((i + wave_num) & 3);
    switch (corner) {
    case 0: spawn_enemy_at(2, 2); break;
    case 1: spawn_enemy_at(123, 2); break;
    case 2: spawn_enemy_at(2, 59); break;
    case 3: spawn_enemy_at(123, 59); break;
    }
  }
}

SCENE_FN(PLAY) bool any_enemies_alive() {
  for (u8 i = 0; i < ent::POOL_SIZE; ++i) {
    if (ent::pool[i].active && ent::pool[i].kind == ent::ENEMY) return true;
  }
  return false;
}

// Begin the actual run state — separate from start_run() so the GATE_CARD
// transition can call this after its countdown finishes.
void begin_play() {
  ent::clear_pool();
  spawn_player();
  state             = PLAYING;
  wave              = 1;
  wave_cooldown     = WAVE_SPAWN_DELAY;
  spawn_pause_done  = 0;
  run_sangue_earned = 0;
  run_kills         = 0;
  run_bosses_felled = 0;
  last_card_circle  = 0xFF;  // first PLAYING entry triggers CIRCLE_CARD(0)
}

// Resume into PLAYING, but interpose a CIRCLE_CARD if the upcoming wave's
// circle differs from the last one the pilgrim saw a card for. Called by
// every "going back to PLAYING" site (GUIDE_SCREEN exits, UPGRADE return,
// GATE_CARD finish, etc) so circle transitions are detected uniformly.
void resume_play() {
  const u8 c = circle_for_wave(wave);
  if (c != last_card_circle) {
    state      = CIRCLE_CARD;
    card_timer = CARD_FRAMES;
  } else {
    state = PLAYING;
  }
}

// Triggered by ENTER GATES on the main menu. Hands control to the staged
// GATE_CARD ritual; when its timeline runs to its end (or the pilgrim
// presses A), GATE_CARD's handler runs begin_play() and resume_play().
void start_run() {
  state  = GATE_CARD;
  gate_t = 0;
}

// Single canonical exit back to MAIN_MENU. Used by every non-narrative
// return path (title-A, name-entry-A returning, tutorial-A, end_run
// abandoned, second-death FORSAKE). Direct state assignment + cursor
// reset; the SPM swap + loading-cover are handled automatically by
// scene::set_active on the bank crossing — callers don't think about
// banks.
//
// Also writes the autosave (vestigia slot 0). Every wood arrival is
// the canonical autosave moment in the lore — Hell's bookkeeping
// records the Pilgrim's current shape at every "rest" point. The
// player can later load it from the AUTOSAVE row of the VESTIGIA
// list, returning to whatever state they were in at their last
// wood arrival. Lore: docs/design/ "The ledger and the vestigia."
inline void return_to_wood() {
  state      = MAIN_MENU;
  menu_index = 0;
  vestigia::write_autosave(meta);
}

// Load a vestige slot's saved MetaCharacter into the live `meta` and
// persist to EEPROM so subsequent boots hydrate from this state. The
// VESTIGIA popup's LOAD action calls this, then return_to_wood. On
// failure (CORRUPT_SLOT, EMPTY) the live meta is left untouched.
//
// Placed in .hightext to land just before the scene bank, keeping it
// within rcall reach of bank-side callers that the popup-confirm
// helper invokes.
__attribute__((section(".hightext"))) void vestigia_load_into_meta(u8 slot) {
  storage::MetaCharacter scratch;
  vestigia::Status s = vestigia::read_slot(slot, scratch);
  if (s != vestigia::Status::OK) return;
  meta = scratch;
  storage::write_meta(meta);
}

// VESTIGIA popup confirm-action dispatcher. Caller has just validated
// the user pressed A on a non-CANCEL option; this routes to the
// appropriate load-or-save action and resets the popup state. Returns
// true if the action took the player out of the screen
// (return_to_wood), so the caller can short-circuit further handling.
//
// Placed in .hightext for the same reason as vestigia_load_into_meta.
__attribute__((section(".hightext"))) bool vestigia_popup_confirm() {
  bool exited = false;
  switch (vestigia_popup) {
  case 1:  // AUTOSAVE: option 0 = LOAD
    vestigia_load_into_meta(vestigia::SLOT_UNBURDENED);
    return_to_wood();
    exited = true;
    break;
  case 2:  // empty manual: option 0 = SAVE
    vestigia::write_slot(vestigia_cursor, meta);
    break;
  case 3:  // occupied manual: option 0 = LOAD, option 1 = OVERWRITE
    if (vestigia_popup_cursor == 0) {
      vestigia_load_into_meta(vestigia_cursor);
      return_to_wood();
      exited = true;
    } else {
      vestigia::write_slot(vestigia_cursor, meta);
    }
    break;
  default: break;
  }
  vestigia_popup        = 0;
  vestigia_popup_cursor = 0;
  return exited;
}

// Commit run results to persistent storage. Called on every death or
// forfeit. `abandoned` = true means the pilgrim turned back at the pause
// menu (no SECOND DEATH screen — thou steppest directly back into the
// wood); false means he actually fell (HP=0) and sees damnation.
SCENE_FN(PLAY) void end_run(bool abandoned = false) {
  if (player && wave > best.wave) {
    best.wave      = wave;
    best.hp        = player->max_hp;
    best.damage    = player->damage;
    best.fire_rate = player->fire_rate;
    storage::write_best_run(best);
  }

  // Roll the run's stats into the persistent meta character.
  // Wallet has been updated live during the run; just commit lifetime stats.
  // total_kills tracks shades only; keepers (bosses) get their own counter so
  // the SECOND DEATH / RECKONING screens can report SOMMA SHADES and SOMMA
  // KEEPERS distinctly.
  const u16 run_shades = (u16)(run_kills - run_bosses_felled);
  meta.total_runs      = (u16)(meta.total_runs + 1);
  meta.total_kills     = (u16)(meta.total_kills + run_shades);
  // 32bit-ok: total_sangue_earned is the lifetime u32 tally; widen the
  // u16 run total before adding to avoid silent wrap at 65535.
  meta.total_sangue_earned = meta.total_sangue_earned + (u32)run_sangue_earned;
  // u8 saturating add: cap at 255 so the lifetime keeper count doesn't wrap.
  if ((u16)meta.total_keepers_felled + (u16)run_bosses_felled > 0xFF) {
    meta.total_keepers_felled = 0xFF;
  } else {
    meta.total_keepers_felled = (u8)(meta.total_keepers_felled + run_bosses_felled);
  }
  storage::write_meta(meta);

  if (abandoned) {
    return_to_wood();
  } else {
    state      = SECOND_DEATH;
    menu_index = 0;
  }
}

// ----------------------------------------------------------- per-kind ---
//
// Shared bullet-spawn helper. Shooter could be player or enemy — same code,
// per the entity-symmetry rule. Direction is a fx-encoded unit-ish vector;
// the helper scales it by `speed` to set bullet velocity. `sprite_id` is
// the visual — defaults to sprites::BULLET (pilgrim's modern shot); enemies
// pass a DART_* id keyed to their circle (see enemy_projectile_for).
SCENE_FN(PLAY)
void spawn_bullet(ent::Kind kind, fx origin_x, fx origin_y, fx unit_dx, fx unit_dy, fx speed,
                  u8 damage, u8 sprite_id = sprites::BULLET) {
  ent::Entity* b = ent::spawn(kind);
  if (!b) return;
  b->x         = origin_x;
  b->y         = origin_y;
  b->hp        = 1;
  b->damage    = damage;
  b->sprite_id = sprite_id;
  // unit_d* is in fx (unit length 256). Scaled velocity = unit * speed / FX_ONE.
  // 32bit-ok: fx*fx multiply genuinely needs i32 (unit_d * speed can reach
  // 256 * 768 = 196608, well past i16). Right-shift by 8 stays in i16.
  b->dx = (fx)(((i32)unit_dx * speed) >> 8);
  b->dy = (fx)(((i32)unit_dy * speed) >> 8);
}

// Map an enemy's sprite_id to its projectile visual. Four circles get
// canon-coded overrides (centaur arrow from Canto XII, malebranche hook
// from XXI-XXII, ice traitor shard from Cocytus, lust spirit wind from
// V); every other damned shade hurls a generic STONE (Canto VII).
inline u8 enemy_projectile_for(u8 shooter_sprite_id) {
  switch (shooter_sprite_id) {
  case sprites::ENE_CENTAUR: return sprites::DART_ARROW;
  case sprites::ENE_MALEBRANCHE: return sprites::DART_HOOK;
  case sprites::ENE_ICE_TRAITOR: return sprites::DART_SHARD;
  case sprites::ENE_LUST_SPIRIT: return sprites::DART_WIND;
  default: return sprites::DART_STONE;
  }
}

SCENE_FN(PLAY) void update_player() {
  if (!player || !player->active) return;

  // Read d-pad → discrete -1/0/+1 axes → 8-direction enum → fixed-point unit vec.
  i8 mx = 0, my = 0;
  if (input::held(input::LEFT)) mx = -1;
  if (input::held(input::RIGHT)) mx = 1;
  if (input::held(input::UP)) my = -1;
  if (input::held(input::DOWN)) my = 1;
  dir::Dir move_dir = dir::from_input(mx, my);

  // Apply movement at furia-scaled speed. Equal speed in all 8 directions
  // because the diagonal unit vector is pre-scaled to ~0.707.
  const fx speed = player_speed_for_fire_rate(player->fire_rate);
  if (move_dir != dir::NONE) {
    const dir::Vec v = dir::read(move_dir);
    // 32bit-ok: see spawn_bullet — fx*fx velocity scale needs i32 product.
    player->x += (fx)(((i32)v.dx * speed) >> 8);
    player->y += (fx)(((i32)v.dy * speed) >> 8);
    // Facing follows movement.
    ent::player_facing_dx = v.dx;
    ent::player_facing_dy = v.dy;
    // Animation: facing-left flag mirrors when horizontal motion is
    // negative. Vertical-only movement preserves the previous facing.
    if (v.dx < 0)
      player_anim_facing_left = 1;
    else if (v.dx > 0)
      player_anim_facing_left = 0;
  }
  // If not moving, facing stays at whatever the last move direction was.

  // Animation state from input. ATTACK is sticky until its strip ends —
  // tick_player_animation() will return to IDLE/WALK on its own. While
  // attacking, walking input still updates facing (above) but doesn't
  // override the attack state.
  if (player_anim_state != ANIM_ATTACK) {
    set_player_anim(move_dir != dir::NONE ? ANIM_WALK : ANIM_IDLE);
  }
  tick_player_animation();

  // Clamp to screen (with HUD margin at top).
  const u8 w = entity_w(*player), h = entity_h(*player);
  i16 px = fx_to_px(player->x), py = fx_to_px(player->y);
  if (px < 0) {
    px        = 0;
    player->x = fx_px(px);
  }
  if (px > fb::WIDTH - (i16)w) {
    px        = fb::WIDTH - (i16)w;
    player->x = fx_px(px);
  }
  if (py < 9) {
    py        = 9;
    player->y = fx_px(py);
  }
  if (py > fb::HEIGHT - (i16)h) {
    py        = fb::HEIGHT - (i16)h;
    player->y = fx_px(py);
  }

  // Player-driven fire. A pressed (edge) OR A held both fire when the
  // cooldown is expired. Press and hold share the same cadence gate, so
  // mashing A can't exceed the weapon's natural fire_rate — the pilgrim
  // fires no faster than his furia allows.
  if (player->timer > 0) --player->timer;
  if (player->timer == 0 && (input::pressed(input::A) || input::held(input::A))) {
    set_player_anim(ANIM_ATTACK);
    fx cx = player->x + fx_px((i16)(w / 2));
    fx cy = player->y + fx_px((i16)(h / 2));
    // Look up the visual the Pilgrim's currently equipped bullet renders as.
    // Defaults to STONE (tier 0) if meta.bullet is out of range — defensive
    // against an old EEPROM read that somehow leaked past the magic check.
    u8 tier             = meta.bullet < BULLET_TIER_COUNT ? meta.bullet : 0;
    u8 bullet_sprite_id = pgm_read_byte(&BULLET_SPRITE_BY_TIER[tier]);
    spawn_bullet(ent::PLAYER_SHOT, cx, cy, ent::player_facing_dx, ent::player_facing_dy,
                 PLAYER_BULLET_SPD, player->damage, bullet_sprite_id);
    audio::play(&SFX_SHOOT);
    player->timer = fire_cooldown(player->fire_rate);
  }
}

// AI archetypes — behavior is data, derived from sprite_id so no per-entity
// RAM cost. Each sin gets its own march:
//   CHASER  — walks straight at the pilgrim. Cheapest, dumbest, fastest
//             path to damage. wraith, worm, swine, brawler.
//   STRAFER — approaches until inside a comfort band (STRAFE_INNER..OUTER),
//             then circles perpendicular to the player-line. Harasses at
//             range. lust, profligate, harpy, malebranche.
//   SNIPER  — closes to SNIPER_RANGE, then halts and shoots. Moves only
//             when the pilgrim escapes the band. jouster, tomb shade,
//             centaur, ice traitor.
enum AiKind : u8 { AI_CHASER, AI_STRAFER, AI_SNIPER };

constexpr u8 STRAFE_INNER = 26;  // px — below this, back away from player
constexpr u8 STRAFE_OUTER = 40;  // px — above this, close in
constexpr u8 SNIPER_RANGE = 36;  // px — sniper's hold distance (mid-arena)
constexpr u8 SNIPER_BAND  = 8;   // px — tolerance around SNIPER_RANGE

inline AiKind ai_kind_for(u8 sprite_id) {
  switch (sprite_id) {
  case sprites::ENE_LUST_SPIRIT:
  case sprites::ENE_PROFLIGATE:
  case sprites::ENE_HARPY:
  case sprites::ENE_MALEBRANCHE: return AI_STRAFER;
  case sprites::ENE_JOUSTER:
  case sprites::ENE_TOMB_SHADE:
  case sprites::ENE_CENTAUR:
  case sprites::ENE_ICE_TRAITOR: return AI_SNIPER;
  default: return AI_CHASER;  // wraith, worm, swine, brawler, all bosses
  }
}

SCENE_FN(PLAY) void update_enemy(ent::Entity& e) {
  if (!player || !player->active) return;

  // Direction toward player (raw, then chebyshev-normalize to a unit-ish vec).
  i16 ex = fx_to_px(e.x), ey = fx_to_px(e.y);
  i16 px = fx_to_px(player->x), py = fx_to_px(player->y);
  i16 ddx = px - ex, ddy = py - ey;
  i16 a = ddx < 0 ? -ddx : ddx;
  i16 c = ddy < 0 ? -ddy : ddy;
  i16 m = a > c ? a : c;
  if (m == 0) m = 1;
  // Normalized direction in 8.8 fx (close enough — chebyshev not euclidean).
  // ddx/ddy live in pixel space (max 128), FX_ONE=256, so the product peaks
  // at 32768 which JUST fits i16 (32767 max). Sign saves us — ddx is at
  // most +127 or -128 against a 128-wide playfield. i16 math here avoids
  // pulling in libgcc's 32-bit divide helper.
  fx unit_dx = (fx)(((i16)ddx * FX_ONE) / m);
  fx unit_dy = (fx)(((i16)ddy * FX_ONE) / m);

  // Distance (chebyshev) for band checks. Cheap; good enough on a 128x64
  // playfield — no need for euclidean.
  i16 dist = m;

  // Per-archetype movement. Shooting is shared (below).
  AiKind kind = ai_kind_for(e.sprite_id);
  fx mv_dx = 0, mv_dy = 0;
  switch (kind) {
  case AI_CHASER:
    mv_dx = unit_dx;
    mv_dy = unit_dy;
    break;
  case AI_STRAFER:
    if (dist < STRAFE_INNER) {
      // Too close — retreat straight back.
      mv_dx = -unit_dx;
      mv_dy = -unit_dy;
    } else if (dist > STRAFE_OUTER) {
      // Too far — close in.
      mv_dx = unit_dx;
      mv_dy = unit_dy;
    } else {
      // In the comfort band — strafe perpendicular. Pick rotation off the
      // entity's pool slot so the choice is stable for the life of this
      // spawn (deriving from dynamic y caused the strafer to jitter as
      // its position wiggled across even/odd pixels). Perpendicular of
      // (dx, dy) is (-dy, dx) or (dy, -dx); slot parity splits the pool.
      u8 slot = (u8)(&e - &ent::pool[0]);
      if ((slot & 1) == 0) {
        mv_dx = -unit_dy;
        mv_dy = unit_dx;
      } else {
        mv_dx = unit_dy;
        mv_dy = -unit_dx;
      }
    }
    break;
  case AI_SNIPER: {
    // Inside the band → hold. Otherwise walk toward the band.
    i16 low  = (i16)SNIPER_RANGE - (i16)SNIPER_BAND;
    i16 high = (i16)SNIPER_RANGE + (i16)SNIPER_BAND;
    if (dist < low) {
      mv_dx = -unit_dx;
      mv_dy = -unit_dy;
    } else if (dist > high) {
      mv_dx = unit_dx;
      mv_dy = unit_dy;
    }
    // else: mv stays zero — sniper holds position.
    break;
  }
  }
  // 32bit-ok: see spawn_bullet — fx*fx velocity scale needs i32 product.
  e.x += (fx)(((i32)mv_dx * ENEMY_SPEED) >> 8);
  e.y += (fx)(((i32)mv_dy * ENEMY_SPEED) >> 8);

  // Shoot at player using the same stat-driven cooldown the player uses.
  // Enforce the global enemy-bullet cap by counting active ENEMY_SHOT entities
  // before spawning. Suppressed shots still reset the cooldown — the enemy
  // tried, the bullet was just lost. (Otherwise capped enemies would all spam
  // every frame waiting for a slot.)
  if (e.timer == 0) {
    if (e.fire_rate > 0) {
      u8 in_flight = 0;
      for (u8 i = 0; i < ent::POOL_SIZE; ++i) {
        if (ent::pool[i].active && ent::pool[i].kind == ent::ENEMY_SHOT) ++in_flight;
      }
      if (in_flight < MAX_ENEMY_BULLETS) {
        fx cx = e.x + fx_px((i16)(ENEMY_SPRITE_W / 2));
        fx cy = e.y + fx_px((i16)(ENEMY_SPRITE_H / 2));
        spawn_bullet(ent::ENEMY_SHOT, cx, cy, unit_dx, unit_dy, ENEMY_BULLET_SPD, e.damage,
                     enemy_projectile_for(e.sprite_id));
      }
      e.timer = fire_cooldown(e.fire_rate);
    }
  } else {
    --e.timer;
  }
}

SCENE_FN(PLAY) void update_bullet(ent::Entity& b) {
  b.x += b.dx;
  b.y += b.dy;
  i16 px = fx_to_px(b.x), py = fx_to_px(b.y);
  if (px < -4 || px > fb::WIDTH + 4 || py < -4 || py > fb::HEIGHT + 4) {
    ent::despawn(&b);
  }
}

// Pickups (sangue): static. Despawn after lifetime. Player has to walk over
// them; pickup-vs-player collision happens in resolve_collisions().
SCENE_FN(PLAY) void update_pickup(ent::Entity& p) {
  if (p.timer == 0) {
    ent::despawn(&p);
    return;
  }
  --p.timer;
}

// ---------------------------------------------------------- collision ---

SCENE_FN(PLAY) void resolve_collisions() {
  // Player bullets vs enemies. Bullet bbox computed once per outer iter;
  // enemy bbox computed once per inner iter via the shared noinline helper.
  for (u8 i = 0; i < ent::POOL_SIZE; ++i) {
    ent::Entity& b = ent::pool[i];
    if (!b.active || b.kind != ent::PLAYER_SHOT) continue;
    BBox bb = entity_bbox(b);

    for (u8 j = 0; j < ent::POOL_SIZE; ++j) {
      ent::Entity& e = ent::pool[j];
      if (!e.active || e.kind != ent::ENEMY) continue;
      BBox eb = entity_bbox(e);
      if (!overlap(bb.x, bb.y, bb.w, bb.h, eb.x, eb.y, eb.w, eb.h)) continue;
      if (e.hp <= b.damage) {
        // Ichor drop scales with the enemy's max_hp — bosses give more.
        // Basic enemies (max_hp=2) → 1 sangue; skull (10) → 5; reaper (14) → 7;
        // sotrak (20) → 10. Drop at the enemy's center.
        u8 drop = (u8)(e.max_hp / 2);
        if (drop == 0) drop = 1;
        spawn_sangue((i16)(eb.x + (i16)(eb.w / 2)), (i16)(eb.y + (i16)(eb.h / 2)), drop);
        // Tally before despawn so we can read e.sprite_id.
        ++run_kills;
        if (e.sprite_id >= sprites::BOSS_CHARON && e.sprite_id <= sprites::BOSS_LUCIFER &&
            run_bosses_felled < 0xFF) {
          ++run_bosses_felled;
        }
        ent::despawn(&e);
        audio::play(&SFX_KILL_ENEMY);
      } else {
        e.hp = (u8)(e.hp - b.damage);
        audio::play(&SFX_HIT_ENEMY);
      }
      ent::despawn(&b);
      break;
    }
  }

  if (!player || !player->active) return;
  BBox pb = entity_bbox(*player);

  // Enemy bullets vs player.
  for (u8 i = 0; i < ent::POOL_SIZE; ++i) {
    ent::Entity& b = ent::pool[i];
    if (!b.active || b.kind != ent::ENEMY_SHOT) continue;
    BBox bb = entity_bbox(b);
    if (!overlap(bb.x, bb.y, bb.w, bb.h, pb.x, pb.y, pb.w, pb.h)) continue;
    if (player->hp <= b.damage) {
      player->hp = 0;
      ent::despawn(&b);
      audio::play(&SFX_DEATH);
      end_run();
      return;
    }
    player->hp = (u8)(player->hp - b.damage);
    ent::despawn(&b);
    audio::play(&SFX_PLAYER_HIT);
  }

  // Pickups vs player. AABB; on hit, add value to sangue counter and despawn.
  for (u8 i = 0; i < ent::POOL_SIZE; ++i) {
    ent::Entity& p_ent = ent::pool[i];
    if (!p_ent.active || p_ent.kind != ent::PICKUP) continue;
    BBox ib = entity_bbox(p_ent);
    if (!overlap(pb.x, pb.y, pb.w, pb.h, ib.x, ib.y, ib.w, ib.h)) continue;
    // Pickup goes straight into the persistent wallet so the displayed
    // count is always your character's spendable total. Lifetime tracker
    // also updates so the stats screen shows total ever earned.
    meta.sangue_vessel = (u16)(meta.sangue_vessel + p_ent.damage);
    run_sangue_earned  = (u16)(run_sangue_earned + p_ent.damage);
    ent::despawn(&p_ent);
    audio::play(&SFX_PICKUP);
  }
}

// ---------------------------------------------------------------- draw ---

// Multi-page sprite blit. Sprite data (PROGMEM on AVR, plain rodata on PC)
// is stored page-major: first `width` bytes are rows 0-7, next `width`
// bytes are rows 8-15, etc. We hand each page directly to the framebuffer's
// progmem-aware blitter — no RAM scratch buffer, which matters a lot on AVR
// where RAM is tight and a stack-resident buffer can smash the top of .bss.
void draw_progmem_sprite(i16 x, i16 y, u8 width, u8 height, const u8* flash_src) {
  u8 pages = sprites::pages_for(height);
  for (u8 p = 0; p < pages; ++p) {
    fb::draw_sprite_progmem(x, y + (i16)p * 8, width, flash_src + (u16)p * width);
  }
}

// Mirror-horizontal variant — used for left-facing animation frames
// where the source spritesheet only contains right-facing art. Drawing
// once and mirroring at render time halves the animation flash budget.
void draw_progmem_sprite_mirrored(i16 x, i16 y, u8 width, u8 height, const u8* flash_src) {
  u8 pages = sprites::pages_for(height);
  for (u8 p = 0; p < pages; ++p) {
    fb::draw_sprite_progmem_mirrored(x, y + (i16)p * 8, width, flash_src + (u16)p * width);
  }
}

// RAM-source counterpart to draw_progmem_sprite. Used for sprites stored
// LZ77-compressed in flash and decoded on-demand into a RAM cache (currently
// just bosses — see sprites::ram_data). Identical layout to the PROGMEM
// version; only the per-page blitter differs (plain deref vs pgm_read_byte).
void draw_ram_sprite(i16 x, i16 y, u8 width, u8 height, const u8* ram_src) {
  u8 pages = sprites::pages_for(height);
  for (u8 p = 0; p < pages; ++p) {
    fb::draw_sprite(x, y + (i16)p * 8, width, ram_src + (u16)p * width);
  }
}

// FX-flash-source sprite renderer. Reads ONE PAGE at a time from
// data_flash (width bytes per page) into a small stack buffer, then
// blits that page. Page-by-page so the buffer never exceeds the max
// sprite width (currently 64 — Geryon). Avoids growing the LZ77 cache
// to fit the largest sprite at the cost of N SPI transactions per
// render instead of 1. The per-transaction setup overhead is small
// vs total bytes read; a 60x60 boss costs ~8 transactions × ~70 µs
// = ~560 µs per frame, well under the 16,667 µs frame budget.
constexpr u8 FX_SPRITE_BUF_MAX = 64;  // max sprite width
void draw_fx_sprite(i16 x, i16 y, u8 width, u8 height, u32 fx_offset) {
  if (width > FX_SPRITE_BUF_MAX) return;  // sprite wider than buffer
  const u8 pages = sprites::pages_for(height);
  u8 buf[FX_SPRITE_BUF_MAX];
  for (u8 p = 0; p < pages; ++p) {
    data_flash::read(fx_offset + (u32)p * width, buf, width);
    fb::draw_sprite(x, y + (i16)p * 8, width, buf);
  }
}

// Mirrored variant: same per-page FX read, but blits via the
// column-reversed RAM blitter. The fb side has its own mirrored
// entry point that walks columns in reverse, so we re-use it.
void draw_fx_sprite_mirrored(i16 x, i16 y, u8 width, u8 height, u32 fx_offset) {
  if (width > FX_SPRITE_BUF_MAX) return;
  const u8 pages = sprites::pages_for(height);
  u8 buf[FX_SPRITE_BUF_MAX];
  u8 mirrored[FX_SPRITE_BUF_MAX];
  for (u8 p = 0; p < pages; ++p) {
    data_flash::read(fx_offset + (u32)p * width, buf, width);
    for (u8 col = 0; col < width; ++col) {
      mirrored[col] = buf[width - 1 - col];
    }
    fb::draw_sprite(x, y + (i16)p * 8, width, mirrored);
  }
}

// Engraved portrait plaque — illuminated-manuscript double-stroke
// frame, white interior fill, sprite punched out as a clear silhouette.
// Used by the bestiary's keeper portrait page and Reckoning page 0;
// previously inlined twice (two ~150 B copies); factored 2026-04-26.
//
// Algorithm:
//   1) Draw outer + inner stroke (3 px each side claimed) and fill
//      the interior white pre-invert. Post-invert this becomes a dark
//      box with a thin dark double-frame edge.
//   2) Scan the sprite from the bottom up to find its lowest lit row
//      (the bbox may include empty rows below the figure).
//   3) Bottom-anchor the sprite to the plaque floor, then apply
//      `nudge_down` (positive = push down) to keep top features like
//      horns/halos inside the plaque ceiling — Lucifer's pattern.
//   4) Loop pages with a row-mask (vertical clip) AND a width clamp
//      (horizontal clip) so partial pages or oversize sprites can't
//      punch through the engraved-frame strokes or text columns.
//
// Caller responsible for any post-process (text drawn on top of the
// plaque interior) and the screen-final fb::invert_all().
//
// Only supports FX-resident sprites — the PROGMEM scan path was
// removed after the player + boss portrait migration since every
// portrait now lives on FX. If a PROGMEM portrait re-appears, restore
// the alternate scan path from git history.
void draw_engraved_portrait(i16 box_x, i16 box_y, u8 box_w, u8 box_h, u8 sprite_id, i8 nudge_down) {
  fb::stroke_rect(box_x, box_y, box_w, box_h);
  fb::stroke_rect(box_x + 2, box_y + 2, (u8)(box_w - 4), (u8)(box_h - 4));
  fb::fill_rect(box_x + 3, box_y + 3, (u8)(box_w - 6), (u8)(box_h - 6));

  const u8 sw  = sprites::width(sprite_id);
  const u8 sh  = sprites::height(sprite_id);
  const u32 fx = sprites::fx_offset(sprite_id);
  if (fx == sprites::FX_OFFSET_NONE) return;

  // Lit-bottom scan — walk pages bottom-up, stop at first lit pixel.
  const u8 sprite_pages = (u8)((sh + 7) / 8);
  i16 lit_bottom        = (i16)sh - 1;
  {
    u8 buf[FX_SPRITE_BUF_MAX];
    bool found = false;
    for (i16 page = (i16)sprite_pages - 1; page >= 0 && !found; --page) {
      data_flash::read(fx + (u32)page * sw, buf, sw);
      const u8 valid_rows = (page == (i16)sprite_pages - 1) ? (u8)(sh - page * 8) : 8;
      for (i16 r = (i16)valid_rows - 1; r >= 0 && !found; --r) {
        for (u8 col = 0; col < sw; ++col) {
          if (buf[col] & (1 << r)) {
            lit_bottom = page * 8 + r;
            found      = true;
            break;
          }
        }
      }
    }
  }

  const i16 plaque_top    = box_y + 3;
  const i16 plaque_bottom = box_y + (i16)box_h - 4;
  const i16 plaque_right  = box_x + (i16)box_w - 4;
  const i16 sx            = box_x + ((i16)box_w - (i16)sw) / 2;
  const i16 sy            = plaque_bottom - lit_bottom + (i16)nudge_down;

  // Per-page blit with vertical row-mask + horizontal column clamp.
  for (u8 p = 0; p < sprite_pages; ++p) {
    const i16 page_y = sy + (i16)p * 8;
    if (page_y + 7 < plaque_top) continue;
    if (page_y > plaque_bottom) break;
    u8 row_mask = 0xFF;
    for (u8 b = 0; b < 8; ++b) {
      const i16 row = page_y + (i16)b;
      if (row < plaque_top || row > plaque_bottom) row_mask &= (u8) ~(1 << b);
    }
    u8 buf[FX_SPRITE_BUF_MAX];
    data_flash::read(fx + (u32)p * sw, buf, sw);
    for (u8 col = 0; col < sw; ++col)
      buf[col] &= row_mask;
    u8 draw_w = sw;
    if (sx + (i16)sw - 1 > plaque_right) {
      if (sx > plaque_right)
        draw_w = 0;
      else
        draw_w = (u8)(plaque_right - sx + 1);
    }
    if (draw_w > 0) {
      fb::clear_sprite(sx, page_y, draw_w, buf);
    }
  }
}

// Single-entry dispatcher for entity sprite rendering. PROGMEM for raw
// sprites, RAM cache for LZ77-stored ones (currently bosses). Marked
// noinline so the if/else doesn't get duplicated at every call site —
// the loop in update_screen calls this once per entity, and inlining it
// added 500+ B of code to main last attempt. One out-of-line copy is
// the right tradeoff: hot-loop, but 32 calls/frame max — call overhead
// is negligible vs the per-pixel cost inside the blit.
SCENE_FN(PLAY) void draw_entity_sprite(u8 sprite_id, i16 x, i16 y) {
  const u8 w        = sprites::width(sprite_id);
  const u8 h        = sprites::height(sprite_id);
  const u8* const p = sprites::data(sprite_id);
  if (p) {
    draw_progmem_sprite(x, y, w, h, p);
    return;
  }
  // PROGMEM path returned null — sprite is either FX-flash-resident
  // or LZ77-stored. Dispatch.
  const u32 fx_off = sprites::fx_offset(sprite_id);
  if (fx_off != sprites::FX_OFFSET_NONE) {
    draw_fx_sprite(x, y, w, h, fx_off);
    return;
  }
  // Last fallback: LZ77 (logos do their own render path; this leg is
  // a defensive nullptr → draw nothing for unrecognized ids).
  const u8* const ram = sprites::ram_data(sprite_id);
  if (ram) draw_ram_sprite(x, y, w, h, ram);
}

// Inverse blit: for every "on" pixel in the sprite, CLEAR the corresponding
// framebuffer pixel. Used to draw white-on-black sprites as black-on-white
// inside a filled-white panel.
//
// Operates byte-at-a-time on the framebuffer (one PROGMEM read + one
// AND-NOT + one write per source byte) instead of per-pixel — that's an
// ~8x speedup on a 79x10 logo (we measured 100% CPU + 19 FPS on the
// SECOND_DEATH screen with the per-pixel version; this brings it back).
void draw_progmem_sprite_inverse(i16 x, i16 y, u8 width, u8 height, const u8* flash_src) {
  const u8 pages = sprites::pages_for(height);
  const u8 shift = (u8)(y & 7);   // sub-page vertical offset
  const i8 page0 = (i8)(y >> 3);  // top page in the framebuffer

  for (u8 p = 0; p < pages; ++p) {
    const i8 fb_page_top    = page0 + (i8)p;    // upper destination page
    const i8 fb_page_bot    = fb_page_top + 1;  // lower destination page (only used if shift != 0)
    const bool top_in_range = (fb_page_top >= 0 && fb_page_top < (i8)(fb::HEIGHT / 8));
    const bool bot_in_range =
        (shift != 0 && fb_page_bot >= 0 && fb_page_bot < (i8)(fb::HEIGHT / 8));

    for (u8 col = 0; col < width; ++col) {
      const i16 px = x + (i16)col;
      if (px < 0 || px >= fb::WIDTH) continue;

      const u8 src = pgm_read_byte(&flash_src[(u16)p * width + col]);
      // dup-ok: mirrored in draw_logo_lz77_inverse for the RAM-source
      // case. Same column-walk + AND-NOT math; only the source-byte
      // fetch differs (pgm_read_byte vs plain deref). Unifying via
      // function-pointer indirection adds per-byte call cost and costs
      // more flash than the duplication — same rationale as
      // engine/framebuffer.cpp's draw_sprite/draw_sprite_progmem.
      if (top_in_range) {
        fb::buffer[(u16)px + (u16)fb_page_top * fb::WIDTH] &= (u8) ~(src << shift);
      }
      if (bot_in_range) {
        fb::buffer[(u16)px + (u16)fb_page_bot * fb::WIDTH] &= (u8) ~(src >> (8 - shift));
      }
    }
  }
}

// Render an LZ77-compressed logo. Decodes into the shared sprites::
// LZ77 cache (.bss; also used by bosses — never concurrently, since
// logos render only on TITLE/SECOND_DEATH and bosses only on PLAYING).
//
// History: this used to allocate a 256 B stack-local decode buffer.
// After the boss cache landed (276 B in .bss), stack headroom shrunk
// to ~424 B and the second-death screen's stack-buffer allocation
// blew the stack and froze the device. Single shared .bss cache
// solved it.
void draw_logo_lz77_inverse(u8 sprite_id, i16 x, i16 y, bool inverse) {
  const u8 w     = sprites::width(sprite_id);
  const u8 h     = sprites::height(sprite_id);
  const u8 pages = sprites::pages_for(h);
  const u8* buf  = sprites::ram_data(sprite_id);
  if (inverse) {
    // Inverse blit: walk the decoded RAM bytes and clear matching fb bits.
    // Mirrors draw_progmem_sprite_inverse but with a plain deref.
    const u8 shift = (u8)(y & 7);
    const i8 page0 = (i8)(y >> 3);
    for (u8 p = 0; p < pages; ++p) {
      const i8 fb_page_top    = page0 + (i8)p;
      const i8 fb_page_bot    = fb_page_top + 1;
      const bool top_in_range = (fb_page_top >= 0 && fb_page_top < (i8)(fb::HEIGHT / 8));
      const bool bot_in_range =
          (shift != 0 && fb_page_bot >= 0 && fb_page_bot < (i8)(fb::HEIGHT / 8));
      for (u8 col = 0; col < w; ++col) {
        const i16 px = x + (i16)col;
        if (px < 0 || px >= fb::WIDTH) continue;
        const u8 src = buf[(u16)p * w + col];
        // dup-ok: RAM-source twin of draw_progmem_sprite_inverse's
        // inner column write. Plain deref vs pgm_read_byte; otherwise
        // identical. See dup-ok at draw_progmem_sprite_inverse for the
        // full rationale.
        if (top_in_range) {
          fb::buffer[(u16)px + (u16)fb_page_top * fb::WIDTH] &= (u8) ~(src << shift);
        }
        if (bot_in_range) {
          fb::buffer[(u16)px + (u16)fb_page_bot * fb::WIDTH] &= (u8) ~(src >> (8 - shift));
        }
      }
    }
  } else {
    // Normal blit page-by-page from RAM.
    for (u8 p = 0; p < pages; ++p) {
      fb::draw_sprite(x, y + (i16)p * 8, w, buf + (u16)p * w);
    }
  }
}

// Convenience wrappers using the per-game sprite table. Logos are
// LZ77-compressed (saves ~209 B of flash); other sprites stay raw.
void draw_logo(u8 sprite_id, i16 x, i16 y) {
  if (sprites::is_lz77(sprite_id)) {
    draw_logo_lz77_inverse(sprite_id, x, y, /*inverse=*/false);
    return;
  }
  draw_progmem_sprite(x, y, sprites::width(sprite_id), sprites::height(sprite_id),
                      sprites::data(sprite_id));
}
void draw_logo_inverse(u8 sprite_id, i16 x, i16 y) {
  if (sprites::is_lz77(sprite_id)) {
    draw_logo_lz77_inverse(sprite_id, x, y, /*inverse=*/true);
    return;
  }
  draw_progmem_sprite_inverse(x, y, sprites::width(sprite_id), sprites::height(sprite_id),
                              sprites::data(sprite_id));
}

// Discrete pip-row HP bar. Each filled pip = 1px wide × 2px tall; pips are
// separated by 1px gaps. Missing pips render as a single bottom dot so the
// max_hp count is still readable when fully damaged.
//   Pip width   = 1
//   Pip height  = 2
//   Pip stride  = 2  (1 px pip + 1 px gap)
// Total bar width = max_hp * 2 - 1 (no trailing gap)
void draw_pip_bar(i16 left_x, i16 y, u8 current, u8 max) {
  for (u8 i = 0; i < max; ++i) {
    i16 px = left_x + (i16)i * 2;
    if (i < current) {
      // Filled pip: 1x2 vertical bar.
      fb::set_pixel(px, y);
      fb::set_pixel(px, y + 1);
    } else {
      // Empty pip: a single bottom dot, just enough to show the slot exists.
      fb::set_pixel(px, y + 1);
    }
  }
}

// Build the right-aligned text representation of a sangue value:
//   value == 0     -> "NIHIL"     (Latin "nothing", Dante-Christian register)
//   1..3999        -> Roman numerals (subtractive, e.g. "XLVII", "MCMXCIX")
//   > 3999         -> "MMMCMXCIX+" (Roman cap + plus-sign overflow indicator)
// Writes to `out` (caller buffer >= 16) and returns chars written.
u8 format_sangue_value(u16 value, char* out) {
  if (value == 0) {
    out[0] = 'N';
    out[1] = 'I';
    out[2] = 'H';
    out[3] = 'I';
    out[4] = 'L';
    out[5] = 0;
    return 5;
  }
  if (value > 3999) {
    // Roman numerals cap at 3999 (no overline glyph for thousands at this
    // scale). Display the max value as a hard ceiling.
    static const char OVF[] PROGMEM = "MMMCMXCIX";
    u8 n                            = 0;
    char c;
    while ((c = (char)pgm_read_byte(&OVF[n])) != 0) {
      out[n++] = c;
    }
    out[n] = 0;
    return n;
  }
  return format_roman_u16(value, out);
}

// Render a sangue value with the Phlegethon ripple ('~' = a single drop of
// boiling blood) flush to its right. `x_right` is the right edge the ripple
// occupies; the value sits to its left so the pair reads as "<count> drops"
// with no internal gap. Values formatted via format_sangue_value (Roman /
// NIHIL / overflow).
void draw_sangue(i16 x_right, i16 y, u16 value) {
  char buf[16];
  u8 n  = format_sangue_value(value, buf);
  i16 x = x_right - (i16)font::STRIDE;  // ripple slot
  font::draw_char(x, y, '~');
  // Walk the value backward, one glyph per STRIDE moving left.
  for (u8 i = 0; i < n; ++i) {
    x -= (i16)font::STRIDE;
    font::draw_char(x, y, buf[n - 1 - i]);
  }
}

// u32 variant — for lifetime counters (total_sangue_earned). Uses the
// vinculum overline to render thousands: 1..3999 renders as standard
// Roman; 4000..3,999,999 splits into overlined-thousands + ones-Roman.
// The blood-drop ripple occupies the rightmost slot; the ones part
// draws next (plain); then the thousands part (overlined). This keeps
// the canonical left-to-right reading MCMXCIX ~ with thousands leftmost.
// Vinculum Roman renderer. Splits u32 into thousands + ones; thousands
// render with overline (×1000 multiplier), ones in plain Roman, blood-
// drop ripple right-anchored. Period-correct to ~3,999,999.
//
// noinline because LTO will otherwise inline this into its sole caller
// (draw_stats_page_lifetime, in the MAIN_MENU bank), which drags the
// 32-bit divmod into bank-local code. The libgcc helper __udivmodsi4
// lives in CORE .text and is right at the 13-bit rcall horizon from the
// bank — any layout shift puts it out of reach and the link fails. By
// keeping draw_sangue_u32 as a real function in CORE, the divmod call
// stays in CORE where it can always reach libgcc.
__attribute__((noinline)) void draw_sangue_u32(i16 x_right, i16 y, u32 value) {
  if (value <= 3999u) {
    draw_sangue(x_right, y, (u16)value);
    return;
  }
  // Split by 1000. `big` fits u16 since u32 max / 1000 < 4.3M < u32 max,
  // and we only support up to 3,999,999 so big <= 3999.
  const u16 big   = (u16)((value > 3999999u ? 3999999u : value) / 1000u);
  const u16 small = (u16)(value % 1000u);
  char thousands[16], ones[16];
  u8 tn = format_roman_u16(big, thousands);
  u8 on = small > 0 ? format_roman_u16(small, ones) : 0;
  i16 x = x_right - (i16)font::STRIDE;
  font::draw_char(x, y, '~');
  for (u8 i = 0; i < on; ++i) {
    x -= (i16)font::STRIDE;
    font::draw_char(x, y, ones[on - 1 - i]);
  }
  for (u8 i = 0; i < tn; ++i) {
    x -= (i16)font::STRIDE;
    font::draw_char_overlined(x, y, thousands[tn - 1 - i]);
  }
}

// Inverse variant for screens drawn black-on-white (SECOND_DEATH's panel,
// which the trailing invert_all() flips into white-on-black).
void draw_sangue_inverse(i16 x_right, i16 y, u16 value) {
  char buf[16];
  u8 n  = format_sangue_value(value, buf);
  i16 x = x_right - (i16)font::STRIDE;  // ripple slot
  font::draw_char_inverse(x, y, '~');
  for (u8 i = 0; i < n; ++i) {
    x -= (i16)font::STRIDE;
    font::draw_char_inverse(x, y, buf[n - 1 - i]);
  }
}

// Player overlay: HP pip bar under the sprite.
//
// Used to also draw a facing-direction pixel for aim feedback when the
// player was a 5x8 sprite that didn't visually orient. The animated
// world sprites flip horizontally via the mirror flag, so the body
// itself indicates facing — the extra pixel was just visual noise.
SCENE_FN(PLAY) void draw_player_overlay(const ent::Entity& e) {
  // Use the *current* entity's sprite dimensions, not the legacy
  // PLAYER_SPRITE_W/H constants — those still describe the 5x8 fallback,
  // not the in-world sprite (which is 12x16 today and grows with class
  // level). entity_w/entity_h read sprites::width(sprite_id) etc which
  // tracks whatever the player render path actually blits.
  const u8 w  = entity_w(e);
  const u8 h  = entity_h(e);
  const i16 x = fx_to_px(e.x);
  const i16 y = fx_to_px(e.y);

  // HP pip bar: centered under the figure's body, 1 px gap below. Uses
  // IDLE F0 as the reference for centering so the bar stays put while
  // the animation plays — measuring per-frame would make it jitter as
  // legs/arms swing. The mirror flag flips the offset when facing left.
  if (e.max_hp > 0) {
    static u8 cached_visual_center = 0xFF;  // sentinel: compute once
    if (cached_visual_center == 0xFF) {
      const u8 ref_id  = sprites::PEN_L1_IDLE_F0;
      const u8 sw      = sprites::width(ref_id);
      const u8 pages   = sprites::pages_for(sprites::height(ref_id));
      const u32 fx_off = sprites::fx_offset(ref_id);
      const u8* ref    = nullptr;
      u8 fxbuf[FX_SPRITE_BUF_MAX];
      bool from_fx = false;
      if (fx_off != sprites::FX_OFFSET_NONE) {
        const u16 n = (u16)sw * pages;
        if (n <= FX_SPRITE_BUF_MAX) {
          data_flash::read(fx_off, fxbuf, n);
          ref     = fxbuf;
          from_fx = true;
        }
      } else {
        ref = sprites::data(ref_id);
      }
      u8 left = sw, right = 0;
      if (ref) {
        for (u8 col = 0; col < sw; ++col) {
          u8 any = 0;
          for (u8 p = 0; p < pages; ++p) {
            const u8* page_ptr = ref + (u16)p * sw + col;
            any |= from_fx ? *page_ptr : pgm_read_byte(page_ptr);
          }
          if (any) {
            if (col < left) left = col;
            if (col > right) right = col;
          }
        }
      }
      cached_visual_center = (left <= right) ? (u8)((left + right) / 2) : (u8)(sw / 2);
    }
    u8 visual_center = cached_visual_center;
    // Mirror flips the offset around the bbox center.
    if (player_anim_facing_left) visual_center = (u8)((w - 1) - visual_center);
    u8 bar_w  = (u8)(e.max_hp * 2 - 1);
    i16 bar_x = x + (i16)visual_center - (i16)(bar_w / 2);
    i16 bar_y = y + h + 1;
    draw_pip_bar(bar_x, bar_y, e.hp, e.max_hp);
  }
}

// Enemy overlay: HP pip bar above the sprite. Only drawn when max_hp > 1
// (a single-HP enemy is either alive or already despawned — no bar needed).
SCENE_FN(PLAY) void draw_enemy_overlay(const ent::Entity& e) {
  if (e.max_hp <= 1) return;
  i16 x = fx_to_px(e.x), y = fx_to_px(e.y);
  const u8 w = ENEMY_SPRITE_W;
  u8 bar_w   = (u8)(e.max_hp * 2 - 1);
  i16 bar_x  = x + ((i16)w - (i16)bar_w) / 2;
  i16 bar_y  = y - 3;  // 2 px above sprite, accounting for 2 px pip height
  draw_pip_bar(bar_x, bar_y, e.hp, e.max_hp);
}

SCENE_FN(PLAY) void draw_hud() {
  // Top row: descent indicator (III.2/3), VITA, SANGUE.
  // VITA = HP (Canto I.1 "nel mezzo del cammin di nostra vita"),
  // SANGUE (Canto XII, the river of blood Phlegethon),
  // descent uses Dante's "round"/giro subdivision (Canto XIII.17).
  //
  // HUD band: 9 rows reserved (playfield clamp is y >= 9). Glyphs are
  // 5 tall; 1 px above (row 0 blank), text on rows 1-5, 1 px below
  // (row 6 blank), separator at row 7. Playfield starts at row 9.
  constexpr i16 HUD_TEXT_Y = 1;
  char d[8];
  format_descent_short(wave, d);
  font::draw_text(2, HUD_TEXT_Y, d);
  font::draw_text_pgm(32, HUD_TEXT_Y, LIT_VITAC);
  draw_roman(56, HUD_TEXT_Y, player ? player->hp : 0);
  font::draw_text_pgm(64, HUD_TEXT_Y, LIT_SANGUEC);
  draw_sangue(fb::WIDTH, HUD_TEXT_Y, meta.sangue_vessel);
  // Thin separator below the HUD band.
  fb::fill_rect(0, 7, fb::WIDTH, 1);
}

// ---- Menus (white box + black text overlay) -----------------------------
//
// All menus in the game share a renderer: a centered white box, an inverse
// title, a list of inverse option strings, and a '>' selector to the left
// of the active option. Inverse text + clear_rect lets the box float over
// the world (game over) or be opaque (pause).

constexpr u8 MENU_PAD_X    = 6;
constexpr u8 MENU_PAD_Y    = 4;
constexpr u8 MENU_LINE_GAP = 2;
constexpr u8 MENU_BORDER   = 1;
constexpr u8 LINE_H        = font::GLYPH_H + MENU_LINE_GAP;

u8 strlen_(const char* s) {
  u8 n = 0;
  while (s[n])
    ++n;
  return n;
}

// Copy a NUL-terminated RAM string into `out`, returning chars written
// (NOT including the terminator). `out` must have space for at least
// strlen_(s) + 1 bytes. Used to replace the half-dozen hand-rolled
// `while (head[n]) { out[n] = head[n]; ++n; }` patterns this codebase
// kept growing — the helper inlines tighter than the duplicates and
// the linter (efficiency_lint duplicate-string-walk rule) flags any
// new copies that don't go through it.
u8 strcpy_(char* out, const char* s) {
  u8 n = 0;
  while (s[n]) {
    out[n] = s[n];
    ++n;
  }
  out[n] = 0;
  return n;
}

// Longest menu string in the game ("DOST THOU TURN BACK?" title) is 20 chars.
// Sized for that with a little headroom so a future option doesn't silently
// truncate.
constexpr u8 MENU_LINE_MAX = 24;

// PROGMEM-flavored menu renderer. `title_pgm` may be null or point to an
// empty flash string for "no title"; `opts_pgm` is a PROGMEM array of
// PROGMEM C-string pointers. `cols` is the longest-line width in glyphs
// (passed by the caller — measuring it inside the function added ~110 B
// of flash for a value the caller already knows statically). No info[]
// row support — none of the callers use it and dropping the loop keeps
// the function small.
void draw_menu_pgm(const char* title_pgm, const char* const* opts_pgm, u8 n_opts, u8 selected,
                   u8 cols) {
  char buf[MENU_LINE_MAX];

  char title_buf[MENU_LINE_MAX];
  title_buf[0]   = 0;
  bool has_title = (title_pgm && pgm_read_byte(title_pgm) != 0);
  if (has_title) {
    copy_pgm_str(title_pgm, title_buf, MENU_LINE_MAX);
  }
  u16 inner_w = (u16)cols * font::STRIDE + (u16)font::STRIDE;  // +1 char for selector
  u8 rows     = (u8)((has_title ? 1 : 0) + n_opts);
  u16 inner_h = (u16)rows * LINE_H;

  u16 box_w = inner_w + 2 * MENU_PAD_X;
  u16 box_h = inner_h + 2 * MENU_PAD_Y;
  i16 box_x = (fb::WIDTH - (i16)box_w) / 2;
  i16 box_y = (fb::HEIGHT - (i16)box_h) / 2;

  fb::fill_rect(box_x, box_y, (u8)box_w, (u8)box_h);
  fb::clear_rect(box_x + MENU_BORDER, box_y + MENU_BORDER, (u8)(box_w - 2 * MENU_BORDER), 1);
  fb::clear_rect(box_x + MENU_BORDER, box_y + (i16)box_h - 2 * MENU_BORDER,
                 (u8)(box_w - 2 * MENU_BORDER), 1);
  fb::clear_rect(box_x + MENU_BORDER, box_y + MENU_BORDER, 1, (u8)(box_h - 2 * MENU_BORDER));
  fb::clear_rect(box_x + (i16)box_w - 2 * MENU_BORDER, box_y + MENU_BORDER, 1,
                 (u8)(box_h - 2 * MENU_BORDER));

  i16 first_y = box_y + MENU_PAD_Y;
  if (has_title) {
    i16 title_x = box_x + ((i16)box_w - (i16)strlen_(title_buf) * (i16)font::STRIDE) / 2;
    font::draw_text_inverse(title_x, first_y, title_buf);
  }
  u8 info_row_base = has_title ? 1 : 0;

  i16 opt_x_left = box_x + MENU_PAD_X + (i16)font::STRIDE;
  for (u8 i = 0; i < n_opts; ++i) {
    const char* p = (const char*)pgm_read_ptr(&opts_pgm[i]);
    copy_pgm_str(p, buf, MENU_LINE_MAX);
    i16 y = first_y + (i16)LINE_H * (i16)(info_row_base + i);
    font::draw_text_inverse(opt_x_left, y, buf);
    if (i == selected) {
      font::draw_char_inverse(box_x + MENU_PAD_X, y, '>');
    }
  }
}

// ---- Per-screen builders --------------------------------------------------

// Draw a stat row inside the SECOND_DEATH panel. `label_x` is the left edge of
// the label; `value_x_right` is the right edge the digits are aligned to.
// Returns nothing — caller controls y. All text is inverse (black on white).

// Footer text in the standard slot (left-aligned at HEIGHT-7).
// Used by every screen with a "PRESS A / TURN BACK" hint. Factored
// from 6 inline call sites 2026-04-27.
inline void draw_footer_pgm(const char* footer_pgm) {
  font::draw_text_pgm(2, fb::HEIGHT - 7, footer_pgm);
}

// Single-pixel horizontal manuscript rule below a screen's title row.
// y=8 is hard-coded — every titled screen uses 7 px for the title +
// 1 px gap. Was 8 inline copies of fb::fill_rect(0, 8, fb::WIDTH, 1)
// until factored 2026-04-27.
inline void draw_header_rule() {
  fb::fill_rect(0, 8, fb::WIDTH, 1);
}

// RECKONING-style header: title literal on the left, Pilgrim name on
// the right, separator rule below. Used by both Reckoning pages and
// will be used by any future "this Pilgrim" screen. Was duplicated
// inline at multiple sites until factored 2026-04-27.
void draw_pilgrim_name_header(const char* title_pgm) {
  font::draw_text_pgm(2, 1, title_pgm);
  char name_tmp[storage::NAME_LEN + 1];
  for (u8 i = 0; i < storage::NAME_LEN; ++i)
    name_tmp[i] = meta.name[i];
  name_tmp[storage::NAME_LEN] = 0;
  const i16 nx                = (i16)fb::WIDTH - (i16)storage::NAME_LEN * (i16)font::STRIDE;
  font::draw_text(nx, 1, name_tmp);
  draw_header_rule();
}

// Right-align a Roman numeral at x_right. Delegates to format_roman_u16
// (the original Roman formatter, defined further up alongside the sangue
// renderer that also uses it). Single source of truth for the conversion
// — an earlier audit accidentally introduced a duplicate `to_roman`
// alongside this; that duplicate is gone.
void draw_roman(i16 x_right, i16 y, u16 value) {
  char buf[16];
  u8 len = format_roman_u16(value, buf);
  font::draw_text(x_right - (i16)len * (i16)font::STRIDE, y, buf);
}

// Render `current DE total` right-anchored at x_right. Positions every
// element from the total's left edge, so longer totals (VIII, XIV…)
// don't crowd "DE" — earlier layout used fixed x=104/108/100 which
// looked correct for 3-char totals (XXI) but ate the right padding for
// 4-char totals like VIII. Each visible gap is one font::STRIDE.
void draw_position_of_total(i16 x_right, i16 y, u16 current, u16 total) {
  constexpr i16 S = (i16)font::STRIDE;
  char buf[16];
  u8 total_len   = format_roman_u16(total, buf);
  i16 total_left = x_right - (i16)total_len * S;
  font::draw_text(total_left, y, buf);
  // "DE": E ends one stride before total starts, D ends one stride
  // before E starts.
  i16 e_x = total_left - 2 * S;  // E's left edge
  i16 d_x = e_x - S;             // D's left edge
  font::draw_char(d_x, y, 'D');
  font::draw_char(e_x, y, 'E');
  // Current Roman: right edge sits one stride before D.
  draw_roman(d_x - S, y, current);
}

// Roman version of draw_uint_or_nihil: NIHIL for 0, Roman otherwise.
void draw_roman_or_nihil(i16 x_right, i16 y, u16 value) {
  if (value == 0) {
    font::draw_text_pgm(x_right - 5 * (i16)font::STRIDE, y, LIT_NIHIL);
    return;
  }
  draw_roman(x_right, y, value);
}

void draw_stat_row(i16 label_x, i16 value_x_right, i16 y, const char* label, u16 value) {
  font::draw_text_inverse(label_x, y, label);
  // We can't use draw_uint_inverse directly (it doesn't exist), so build
  // the value string manually. Zero -> "NIHIL" for the tally read.
  i16 x = value_x_right - (i16)font::STRIDE;
  if (value == 0) {
    // Reversed for the right-to-left walk; PROGMEM-resident so the 6 B
    // doesn't sit in .data forever.
    static const char NIHIL[] PROGMEM = "LIHIN";
    for (u8 i = 0; i < 5; ++i) {
      font::draw_char_inverse(x, y, (char)pgm_read_byte(&NIHIL[i]));
      x -= (i16)font::STRIDE;
    }
    return;
  }
  char buf[6];
  u8 n = 0;
  while (value > 0 && n < 5) {
    buf[n++] = (char)('0' + (value % 10));
    value /= 10;
  }
  for (u8 i = 0; i < n; ++i) {
    font::draw_char_inverse(x, y, buf[i]);
    x -= (i16)font::STRIDE;
  }
}

SCENE_FN(PLAY) void draw_second_death() {
  fb::clear();

  // Full-screen panel. After the invert_all() at the end of this function,
  // any unfilled framebuffer (here: nothing) flips to white — keep the
  // panel flush to the screen edges so we don't leave white side-columns
  // around a centered box.
  constexpr i16 BOX_X = 0, BOX_Y = 0;
  constexpr u8 BOX_W = fb::WIDTH, BOX_H = fb::HEIGHT;

  fb::fill_rect(BOX_X, BOX_Y, BOX_W, BOX_H);
  // Inner clear-pixel ring as a 1px frame just inside the white edge.
  fb::clear_rect(BOX_X + 1, BOX_Y + 1, BOX_W - 2, 1);
  fb::clear_rect(BOX_X + 1, BOX_Y + BOX_H - 2, BOX_W - 2, 1);
  fb::clear_rect(BOX_X + 1, BOX_Y + 1, 1, BOX_H - 2);
  fb::clear_rect(BOX_X + BOX_W - 2, BOX_Y + 1, 1, BOX_H - 2);

  // Gothic "SECOND DEATH" logo, inverse (black-on-white) inside the white
  // panel. Centered at top.
  i16 title_x = BOX_X + ((i16)BOX_W - (i16)sprites::width(sprites::LOGO_SECOND_DEATH)) / 2;
  draw_logo_inverse(sprites::LOGO_SECOND_DEATH, title_x, BOX_Y + 4);

  // Single-column this-run summary. Descent line + three stat rows + footer.
  // Tighter row height than the 9-px default (still a multiple of 3) so
  // the four data lines read as one block, not a sparse ladder.
  //   y=21   DESCENT line
  //   y=30   SANGUE BORNE
  //   y=36   SHADES UNDONE
  //   y=42   KEEPERS UNDONE
  //   y=54   options
  // Label column starts at x=6 (4 px in from the inner-border ring at x=1);
  // value column right-aligns at x=124 (4 px in from the right border).
  constexpr i16 LABEL_X = 6;
  constexpr i16 VALUE_X = 124;
  constexpr i16 TITLE_Y = 21;
  constexpr i16 STATS_Y = 30;
  constexpr i16 ROW_H   = 6;  // tighter than 9 — block of stats, not ladder
  constexpr i16 OPTS_Y  = 54;

  // Full descent string, centered (e.g. "CIRCLE III, ROUND 2").
  char descent_text[22];
  format_descent_long(wave, descent_text);
  u8 dlen = strlen_(descent_text);
  i16 dx  = BOX_X + ((i16)BOX_W - (i16)dlen * (i16)font::STRIDE) / 2;
  font::draw_text_inverse(dx, TITLE_Y, descent_text);

  // What the pilgrim took from this descent: blood borne out, shades and
  // keepers given the seconda morte. Post-positive verbs (epitaph register);
  // UNDONE is Longfellow's word from Inferno III ("death had so many undone").
  // Labels in PROGMEM so the literals don't burn ~38 B of .data.
  static const char L_SANGUE[] PROGMEM  = "SANGUE BORNE";
  static const char L_SHADES[] PROGMEM  = "SHADES UNDONE";
  static const char L_KEEPERS[] PROGMEM = "KEEPERS UNDONE";
  char lbuf[16];
  copy_pgm_str(L_SANGUE, lbuf, sizeof(lbuf));
  // Sangue gets the Phlegethon ripple after the count; label drawn solo.
  font::draw_text_inverse(LABEL_X, STATS_Y + 0 * ROW_H, lbuf);
  draw_sangue_inverse(VALUE_X, STATS_Y + 0 * ROW_H, run_sangue_earned);
  copy_pgm_str(L_SHADES, lbuf, sizeof(lbuf));
  draw_stat_row(LABEL_X, VALUE_X, STATS_Y + 1 * ROW_H, lbuf, run_kills);
  copy_pgm_str(L_KEEPERS, lbuf, sizeof(lbuf));
  draw_stat_row(LABEL_X, VALUE_X, STATS_Y + 2 * ROW_H, lbuf, run_bosses_felled);

  // Options: YEA, AGAIN (10 chars = 40 px) and UNTO THE WOOD (13 chars =
  // 52 px). Two selectors at 4 px each = 100 px in a 124-px panel, leaving
  // ~24 px lateral slack. PROGMEM-resident — bare string literals here
  // would land in .data (one-screen text shouldn't burn RAM).
  static const char OPT_RETRY[] PROGMEM = "YEA, AGAIN";
  static const char OPT_QUIT[] PROGMEM  = "UNTO THE WOOD";
  char ob[16];
  i16 retry_x = 8;
  i16 quit_x  = 60;
  copy_pgm_str(OPT_RETRY, ob, sizeof(ob));
  font::draw_text_inverse(retry_x + (i16)font::STRIDE, OPTS_Y, ob);
  copy_pgm_str(OPT_QUIT, ob, sizeof(ob));
  font::draw_text_inverse(quit_x + (i16)font::STRIDE, OPTS_Y, ob);
  if (menu_index == 0) {
    font::draw_char_inverse(retry_x, OPTS_Y, '>');
  } else {
    font::draw_char_inverse(quit_x, OPTS_Y, '>');
  }

  // Pilgrim is dead, deepest in Hell — dark mode. The white panel built
  // above flips to a black panel with white inscriptions inside.
  fb::invert_all();
}

// Pause strings in PROGMEM — the string literals plus the pointer arrays
// otherwise burn ~110 B of .data for text that's only read on one screen.
static const char PAUSE_CONFIRM_TITLE[] PROGMEM       = "DOST THOU TURN BACK?";
static const char PAUSE_CONF_OPT_0[] PROGMEM          = "UNTO THE WOOD";
static const char PAUSE_CONF_OPT_1[] PROGMEM          = "PRESS ON";
static const char* const PAUSE_CONFIRM_OPTS[] PROGMEM = {
    PAUSE_CONF_OPT_0,
    PAUSE_CONF_OPT_1,
};
static const char PAUSE_EMPTY_TITLE[] PROGMEM = "";
static const char PAUSE_OPT_0[] PROGMEM       = "PRESS ON";
static const char PAUSE_OPT_1[] PROGMEM       = "UNTO THE WOOD";
// GRIMOIRE row removed — see commit history. Slated to return as an
// unlockable mid-descent codex once the unlock condition is designed.
static const char* const PAUSE_OPTS[] PROGMEM = {
    PAUSE_OPT_0,
    PAUSE_OPT_1,
};

SCENE_FN(PLAY) void draw_pause_menu() {
  if (pause_confirming) {
    // Two-option confirm before quitting. Title invokes Dante's I.36 —
    // the pilgrim's impulse to turn back from the descent.
    // cols=20: title "DOST THOU TURN BACK?" is the longest line.
    draw_menu_pgm(PAUSE_CONFIRM_TITLE, PAUSE_CONFIRM_OPTS, 2, menu_index, 20);
    return;
  }
  // Title left empty — the floating menu in the center is itself the
  // "you're paused" signal; a literal label is redundant.
  // cols=13: longest option is "UNTO THE WOOD".
  draw_menu_pgm(PAUSE_EMPTY_TITLE, PAUSE_OPTS, 2, menu_index, 13);
}

}  // namespace

namespace game {

// Forward decls for the per-scene update/draw bodies below.
void update_title_scene();
void update_gate_scene();
void update_main_menu_scene();
void update_play_scene();
bool draw_title_scene();
bool draw_gate_scene();
bool draw_main_menu_scene();
bool draw_play_scene();

// One vtable per scene bucket. See docs/scene-paging.md for the
// bucket assignments. These live in CORE today; once paging is real,
// only the active bucket's functions live in the swap bank and the
// vtable is repopulated by scene::switch_to() after each swap.
constexpr scene::VTable VT_TITLE = {
    update_title_scene,
    draw_title_scene,
    nullptr,
    nullptr,
};
constexpr scene::VTable VT_MAIN_MENU = {
    update_main_menu_scene,
    draw_main_menu_scene,
    nullptr,
    nullptr,
};
constexpr scene::VTable VT_GATE = {
    update_gate_scene,
    draw_gate_scene,
    nullptr,
    nullptr,
};
constexpr scene::VTable VT_PLAY = {
    update_play_scene,
    draw_play_scene,
    nullptr,
    nullptr,
};

// Map a State to the vtable that owns it. PROGMEM-resident lookup
// table indexed by State enum value. Direct array indexing is cheaper
// than a switch (no CSWTCH table in .data) and the table cost is
// State-enum-count × 2 B = 36 B in flash, ~0 B in RAM.
//
// Bug here = wrong scene's update fires for a state, game softlocks.
// Order MUST match the State enum declaration above.
const scene::VTable* const VTABLE_BY_STATE[] PROGMEM = {
    &VT_TITLE,      // TITLE
    &VT_MAIN_MENU,  // NAME_ENTRY
    &VT_MAIN_MENU,  // MAIN_MENU
    &VT_MAIN_MENU,  // UPGRADE_MENU
    &VT_MAIN_MENU,  // STATS_SCREEN
    &VT_MAIN_MENU,  // SHADES_SCREEN
    &VT_MAIN_MENU,  // NUMERALS_SCREEN
    &VT_MAIN_MENU,  // LEXICON_SCREEN
    &VT_MAIN_MENU,  // TEXT_SCREEN
    &VT_MAIN_MENU,  // TUTORIAL
    &VT_GATE,       // GATE_CARD
    &VT_GATE,       // CIRCLE_CARD
    &VT_PLAY,       // PLAYING
    &VT_MAIN_MENU,  // GUIDE_SCREEN
    &VT_PLAY,       // PAUSED
    &VT_PLAY,       // SECOND_DEATH
    &VT_MAIN_MENU,  // VESTIGIA_SCREEN
};

// Parallel id table — indexed by State, returns the scene::Id of the
// scene bank the state lives in. Used by the paging path so set_active
// can compare the new id to current_id and decide whether to swap.
// Order MUST match the State enum declaration above (same as VTABLE_BY_STATE).
const u8 SCENE_ID_BY_STATE[] PROGMEM = {
    scene::ID_TITLE,      // TITLE
    scene::ID_MAIN_MENU,  // NAME_ENTRY
    scene::ID_MAIN_MENU,  // MAIN_MENU
    scene::ID_MAIN_MENU,  // UPGRADE_MENU
    scene::ID_MAIN_MENU,  // STATS_SCREEN
    scene::ID_MAIN_MENU,  // SHADES_SCREEN
    scene::ID_MAIN_MENU,  // NUMERALS_SCREEN
    scene::ID_MAIN_MENU,  // LEXICON_SCREEN
    scene::ID_MAIN_MENU,  // TEXT_SCREEN
    scene::ID_MAIN_MENU,  // TUTORIAL
    scene::ID_GATE,       // GATE_CARD
    scene::ID_GATE,       // CIRCLE_CARD
    scene::ID_PLAY,       // PLAYING
    scene::ID_MAIN_MENU,  // GUIDE_SCREEN
    scene::ID_PLAY,       // PAUSED
    scene::ID_PLAY,       // SECOND_DEATH
    scene::ID_MAIN_MENU,  // VESTIGIA_SCREEN
};

const scene::VTable* vtable_for_state(u8 s) {
  // pgm_read_ptr (NOT pgm_read_word): VTABLE_BY_STATE stores C pointers
  // (const VTable*), which are 2 B on AVR but 8 B on a 64-bit PC. Using
  // pgm_read_word here truncates to 16 bits on PC, giving a bogus
  // pointer and a segfault inside scene::set_active. See engine/progmem.h
  // line 49 for the rule.
  return (const scene::VTable*)pgm_read_ptr(&VTABLE_BY_STATE[s]);
}

u8 scene_id_for_state(u8 s) {
  return pgm_read_byte(&SCENE_ID_BY_STATE[s]);
}

void init() {
  best = storage::read_best_run();
  meta = storage::read_meta();
  // vestigia::init() is deferred — calling it here on a fresh chip
  // blocks for ~400 ms while bootstrap_fresh() erases sector 0 and
  // writes slot 0. During that time main() hasn't entered its frame
  // loop, so the OLED renders uninitialized RAM (white screen) and
  // the audio ISR drives stale freq values into the speaker (noise).
  // We init lazily on first VESTIGIA_SCREEN entry instead.
  state = TITLE;
  // First-boot detection happens after TITLE->A: if name is blank we route
  // to NAME_ENTRY before MAIN_MENU.
  scene::set_active(vtable_for_state(state), scene_id_for_state(state));
}

// Public dispatch: re-select the active scene's vtable each frame
// (cheap — usually a no-op fast-path inside set_active when the
// pointer hasn't changed) and dispatch through it. Doing the selection
// here, rather than at every `state = X` assignment site, keeps the
// 36 scattered state assignments untouched.
void update() {
  scene::set_active(vtable_for_state(state), scene_id_for_state(state));
  if (scene::current && scene::current->update) scene::current->update();
}

bool draw() {
  scene::set_active(vtable_for_state(state), scene_id_for_state(state));
  if (scene::current && scene::current->draw) return scene::current->draw();
  return false;
}

#ifdef ENABLE_DEBUG_OVERRIDES
// SDL-only runtime debug-state. Polled at hot points (e.g. spawn_wave)
// to gate off normal behavior for visual / sandbox tests.
namespace {
bool g_no_enemies = false;
}

bool debug_no_enemies() {
  return g_no_enemies;
}

// Apply test-harness overrides from the SDL CLI. Called once after init()
// by platform/sdl/main.cpp. Everything here is transient (nothing writes
// to storage) EXCEPT --wipe, which explicitly erases the meta character.
//
// States past TITLE skip the splash; lower states leave the boot flow
// untouched. --spawn forces PLAYING state and drops one shade into the
// scene using the same routing the old debug spawner used.
void apply_debug_overrides(const DebugCfg& cfg) {
  g_no_enemies = cfg.no_enemies;
  if (cfg.wipe) {
    storage::wipe_meta();
    meta = storage::read_meta();
  }
  if (cfg.has_stats) {
    meta.level_hp        = cfg.level_hp;
    meta.level_damage    = cfg.level_damage;
    meta.level_fire_rate = cfg.level_fire_rate;
  }
  if (cfg.has_sangue) meta.sangue_vessel = cfg.sangue_vessel;
  if (cfg.has_total_sangue) meta.total_sangue_earned = cfg.total_sangue;
  if (cfg.has_runs) meta.total_runs = cfg.total_runs;
  if (cfg.has_kills) meta.total_kills = cfg.total_kills;
  if (cfg.has_keepers) meta.total_keepers_felled = cfg.total_keepers;
  if (cfg.has_bullet) meta.bullet = cfg.bullet_tier;

  if (!cfg.has_state) return;

  // PLAYING requires a live entity pool + player. Route through the same
  // path a real start uses (begin_play + resume_play) so nothing is
  // skipped that PLAYING code expects.
  if (cfg.state == PLAYING) {
    if (cfg.has_wave) wave = cfg.wave;
    begin_play();
    // resume_play's normal flow is PLAYING -> CIRCLE_CARD(0) on fresh
    // runs. Force directly into PLAYING so testers land in-combat.
    state            = PLAYING;
    last_card_circle = circle_for_wave(wave);
    if (cfg.has_spawn) {
      if (cfg.spawn_sprite_id >= 18 && cfg.spawn_sprite_id <= 26) {
        u8 circle = (u8)(cfg.spawn_sprite_id - 18);
        u8 hp     = pgm_read_byte(&BOSS_HP_BY_CIRCLE[circle]);
        spawn_boss(cfg.spawn_sprite_id, hp);
      } else {
        ent::Entity* e = ent::spawn(ent::ENEMY);
        if (e) {
          e->x         = fx_px((i16)((fb::WIDTH - 8) / 2));
          e->y         = fx_px(20);
          e->hp        = ENEMY_BASE_HP;
          e->max_hp    = ENEMY_BASE_HP;
          e->fire_rate = ENEMY_BASE_FIRE_RATE;
          e->damage    = ENEMY_BASE_DAMAGE;
          e->sprite_id = cfg.spawn_sprite_id;
          e->timer     = 0;
          shades_mark_seen(cfg.spawn_sprite_id);
        }
      }
    }
    return;
  }

  // Non-PLAYING: just land on the requested state. Menu entries are
  // safe to enter cold (they read meta/wave from globals already set
  // above). A few states expect specific cursor/phase globals; set the
  // defensible defaults so the state doesn't render garbage.
  state = (State)cfg.state;
  switch (state) {
  case MAIN_MENU: menu_index = 0; break;
  case UPGRADE_MENU: upgrade_cursor = 0; break;
  case SHADES_SCREEN:
    shades_view   = 0;
    shades_cursor = 0;
    break;
  case NUMERALS_SCREEN: numerals_cursor = 0; break;
  case LEXICON_SCREEN: lexicon_cursor = 0; break;
  case TEXT_SCREEN:
    text_section = 0xFF;
    text_cursor  = 0;
    break;
  case SECOND_DEATH: menu_index = 0; break;
  default: break;
  }
}
#endif  // ENABLE_DEBUG_OVERRIDES

// Cycle a cursor up/down through `count` options on UP/DOWN press,
// playing the standard menu SFX. Used by every menu/list screen with
// the canonical 0..count-1 wrap.
//
// Returns true if the cursor moved this frame (caller can use this to
// invalidate sub-state, e.g. close a detail view on cursor change).
// dup-ok: the up/down branches mirror by design — this IS the helper
// that consolidated 8 sites session-wide. Two near-identical branches
// inside a 13-line function is the optimum shape.
bool menu_cursor_step(u8& cursor, u8 count) {
  if (input::pressed(input::DOWN)) {
    cursor = (u8)((cursor + 1) % count);
    audio::play(&SFX_MENU);
    return true;
  }
  // dup-ok: see above — mirror branch for UP press.
  if (input::pressed(input::UP)) {
    cursor = (u8)((cursor + count - 1) % count);
    audio::play(&SFX_MENU);
    return true;
  }
  return false;
}

// dup-ok: 4-direction sibling of menu_cursor_step. Body looks like the
// 2-direction version but the input check is what differs — abstracting
// further would add a parameterized callback (~12 B) for negative gain.
// 4-direction variant for lookup lists where left/right also scroll
// (SHADES, NUMERALS, LEXICON). DOWN || RIGHT advance forward; UP || LEFT
// retreat. Same SFX, same wrap.
// dup-ok: 4-direction sibling of menu_cursor_step. Same justification.
bool menu_cursor_step4(u8& cursor, u8 count) {
  if (input::pressed(input::DOWN) || input::pressed(input::RIGHT)) {
    cursor = (u8)((cursor + 1) % count);
    audio::play(&SFX_MENU);
    return true;
  }
  // dup-ok: mirror branch.
  if (input::pressed(input::UP) || input::pressed(input::LEFT)) {
    cursor = (u8)((cursor + count - 1) % count);
    audio::play(&SFX_MENU);
    return true;
  }
  return false;
}

// Helper: route from TITLE press-A to either NAME_ENTRY (first boot) or MAIN_MENU.
SCENE_FN(TITLE) void leave_title() {
  if (meta.name[0] < 'A' || meta.name[0] > 'Z') {
    // No saved name yet — first boot. Route to name entry. Initial buffer
    // is all 'A's; positions the pilgrim doesn't edit stay A.
    for (u8 i = 0; i < storage::NAME_LEN; ++i)
      name_buf[i] = 'A';
    name_cursor = 0;
    state       = NAME_ENTRY;
  } else {
    return_to_wood();
  }
}

// Fill `out` with the visible GuideAction values in display order and
// return how many there are. RELIC is hidden when the player can't use it
// (no sangue to spend, or no HP to restore). OFFERINGS and CHALICE always
// show — Offerings self-handles maxed stats, Chalice is always affordable.
SCENE_FN(MAIN_MENU) u8 guide_visible_actions(GuideAction* out) {
  u8 n = 0;
  bool can_heal =
      (player != nullptr) && (player->hp < player->max_hp) && (meta.sangue_vessel >= RELIC_COST);
  if (can_heal) out[n++] = GA_RELIC;
  out[n++] = GA_OFFERINGS;
  out[n++] = GA_CHALICE;
  return n;
}

// Per-(level) chalice payout tier. Uses meta.level_hp as per design —
// "you've grown into something that can drink more." Returns a u8 range
// by filling [out_lo, out_hi].
SCENE_FN(MAIN_MENU) void chalice_range(u8& out_lo, u8& out_hi) {
  u8 lv = meta.level_hp;
  if (lv <= 2) {
    out_lo = 1;
    out_hi = 10;
  } else if (lv <= 5) {
    out_lo = 20;
    out_hi = 40;
  } else {
    out_lo = 30;
    out_hi = 50;
  }
}

// Pick a random sangue amount in [lo, hi] inclusive. No stdlib — seed off
// frame counter XOR'd with the entity pool state for non-determinism that
// survives a cold boot. All math runs in u16 so the AVR libgcc 32-bit
// divide helper isn't pulled in by the modulo at the bottom (~70 B saved).
SCENE_FN(MAIN_MENU) u8 chalice_roll(u8 lo, u8 hi) {
  u16 mix = (u16)clock::frame_count;
  for (u8 i = 0; i < ent::POOL_SIZE; ++i) {
    mix = (u16)(mix ^ ((u16)ent::pool[i].x + (u16)ent::pool[i].y * 131u));
  }
  u8 span = (u8)(hi - lo + 1);
  return (u8)(lo + (u8)(mix % span));
}

// ---------------------------------------------------------------- scenes ---
//
// Per-scene update bodies. Reached via scene::current->update — the
// volatile fnptr in scene::VTable, which defeats LTO inlining and lets
// us measure (and eventually page out) each scene's code separately.
//
// Bucket assignments per docs/scene-paging.md:
//   SCENE_TITLE      = TITLE
//   SCENE_MAIN_MENU  = NAME_ENTRY, MAIN_MENU, UPGRADE_MENU, STATS_SCREEN,
//                      SHADES_SCREEN, NUMERALS_SCREEN, LEXICON_SCREEN,
//                      TEXT_SCREEN, TUTORIAL, GUIDE_SCREEN
//   SCENE_GATE       = GATE_CARD, CIRCLE_CARD
//   SCENE_PLAY       = PLAYING, PAUSED, SECOND_DEATH

SCENE_ENTRY(TITLE) void update_title_scene() {
  // Title screen: A advances to main menu (or name entry on first boot).
  if (input::pressed(input::A)) leave_title();
}

SCENE_ENTRY(GATE) void update_gate_scene() {
  if (state == GATE_CARD) {
    // Advance the staged timeline; A skips straight to the hand-off.
    const bool skip = input::pressed(input::A);
    if (skip || gate_t >= GATE_FADE_OUT_END) {
      begin_play();
      resume_play();  // -> CIRCLE_CARD(0) since last_card_circle=0xFF
    } else {
      ++gate_t;
    }
    return;
  }

  if (state == CIRCLE_CARD) {
    const bool skip = input::pressed(input::A);
    if (skip || card_timer == 0) {
      last_card_circle = circle_for_wave(wave);
      state            = PLAYING;
    } else {
      --card_timer;
    }
    return;
  }
}

SCENE_ENTRY(MAIN_MENU) void update_main_menu_scene() {
  if (state == NAME_ENTRY) {
    // UP advances forward through the alphabet (A -> B -> ... -> Z -> A),
    // DOWN walks it back. Matches the mental model that "up" = "later in
    // the alphabet" the same way list menus treat UP as moving up in
    // rank/order.
    if (input::pressed(input::UP)) {
      char& c = name_buf[name_cursor];
      c       = (c >= 'Z') ? 'A' : (char)(c + 1);
    }
    if (input::pressed(input::DOWN)) {
      char& c = name_buf[name_cursor];
      c       = (c <= 'A') ? 'Z' : (char)(c - 1);
    }
    if (input::pressed(input::LEFT) && name_cursor > 0) --name_cursor;
    if (input::pressed(input::RIGHT) && name_cursor < storage::NAME_LEN - 1) ++name_cursor;
    if (input::pressed(input::A)) {
      if (name_cursor < storage::NAME_LEN - 1) {
        ++name_cursor;
      } else {
        // Confirm: write meta with the chosen name + zero stats. On a
        // brand-new save (total_runs == 0) show the one-shot tutorial
        // before dropping into the main menu. Subsequent boots skip it.
        for (u8 i = 0; i < storage::NAME_LEN; ++i)
          meta.name[i] = name_buf[i];
        storage::write_meta(meta);
        if (meta.total_runs == 0) {
          state          = TUTORIAL;
          tutorial_slide = 0;
        } else {
          return_to_wood();
        }
      }
    }
    // B: turn back to TITLE. Discards any in-progress name (no commit until
    // A on the last slot). The sigil cover plays automatically on the
    // M→T bank crossing — we don't need a separate transit state.
    if (input::pressed(input::B)) {
      state      = TITLE;
      menu_index = 0;
    }
    return;
  }

  if (state == TUTORIAL) {
    // Single welcome screen. A presses on into the wood; B returns to
    // NAME_ENTRY (the only place TUTORIAL is reached from). The 5-slide
    // mechanics walkthrough was retired in favor of mystery — players
    // discover the systems by playing or by consulting GRIMOIRE.
    if (input::pressed(input::A)) {
      return_to_wood();
      audio::play(&SFX_MENU);
    }
    if (input::pressed(input::B)) {
      // Return to NAME_ENTRY; restore the cursor at the last slot since
      // the player just confirmed there.
      state       = NAME_ENTRY;
      name_cursor = storage::NAME_LEN - 1;
      audio::play(&SFX_MENU);
    }
    return;
  }

  if (state == MAIN_MENU) {
    // 6 rows: DESCEND / OFFERINGS / RECKONING / VESTIGIA / GRIMOIRE / FORSAKE
    constexpr u8 N = 6;
    menu_cursor_step(menu_index, N);
    if (input::pressed(input::A)) {
      audio::play(&SFX_MENU);
      switch (menu_index) {
      case 0: start_run(); break;
      case 1:
        state          = UPGRADE_MENU;
        upgrade_cursor = 0;
        break;
      case 2: state = STATS_SCREEN; break;
      case 3:
        // VESTIGIA: list of saved traces. Slot 0 (AUTOSAVE) is the
        // permanent first row; slots 1..N-1 are user-managed. See
        // docs/design/ "The ledger and the vestigia".
        //
        // Eagerly init vestigia on first entry so the slot-list cache
        // (slot_occupied / slot_vestige / slot_burden) reflects what's
        // actually in FX flash. init() is idempotent and bootstraps a
        // fresh save region on first boot, writing slot 0 with a blank
        // unburdened MetaCharacter.
        vestigia::init();
        state           = VESTIGIA_SCREEN;
        vestigia_cursor = 0;
        break;
      case 4:
        // GRIMOIRE: top-list of forbidden knowledge (commands, lore,
        // numerals, lexicon, shades). The shades lives here now too.
        state        = TEXT_SCREEN;
        text_cursor  = 0;
        text_section = 0xFF;
        break;
      case 5:
        // FORSAKE: pilgrim withdraws from the menu and returns to the
        // title. The sigil cover plays automatically on the M→T bank
        // crossing — no separate transit state needed. Lifetime EEPROM
        // stats are preserved; only the in-RAM navigation state resets.
        state      = TITLE;
        menu_index = 0;
        break;
      }
    }
    return;
  }

  if (state == UPGRADE_MENU) {
    // 4 rows now: VITA / IRA / FURIA / BULLET. Bullet row buys the next
    // tier in the projectile ladder (see BULLET_SPRITE_BY_TIER).
    constexpr u8 OFFERINGS_ROWS = 4;
    menu_cursor_step(upgrade_cursor, OFFERINGS_ROWS);
    if (input::pressed(input::A)) {
      if (upgrade_cursor == 3) {
        // BULLET row: unlock the next tier if we're not already at the top.
        // Tier rises monotonically — no downgrade. The new tier becomes the
        // equipped bullet immediately (per design: there's no separate
        // "equip" step yet, just a ladder of unlocked-and-active).
        if (meta.bullet + 1 < BULLET_TIER_COUNT) {
          u16 cost = bullet_cost((u8)(meta.bullet + 1));
          if (meta.sangue_vessel >= cost) {
            meta.sangue_vessel = (u16)(meta.sangue_vessel - cost);
            ++meta.bullet;
            storage::write_meta(meta);
            audio::play(&SFX_PICKUP);
          }
        }
      } else {
        u8* level_ptr = (upgrade_cursor == 0)   ? &meta.level_hp
                        : (upgrade_cursor == 1) ? &meta.level_damage
                                                : &meta.level_fire_rate;
        if (*level_ptr < MAX_UPGRADE_LEVEL) {
          u16 cost = upgrade_cost(*level_ptr);
          if (meta.sangue_vessel >= cost) {
            meta.sangue_vessel = (u16)(meta.sangue_vessel - cost);
            ++(*level_ptr);
            storage::write_meta(meta);
            audio::play(&SFX_PICKUP);  // upgrade-purchased: same chime as sangue pickup
          }
        }
      }
    }
    if (input::pressed(input::B)) {
      if (upgrade_from_guide) {
        // Came from the pre-boss interlude; return there, not main menu.
        upgrade_from_guide = 0;
        state              = GUIDE_SCREEN;
        menu_index         = 0;
      } else {
        state      = MAIN_MENU;
        menu_index = 1;
      }
      audio::play(&SFX_MENU);
    }
    return;
  }

  if (state == STATS_SCREEN) {
    // Idle-bob the page-0 portrait icon every frame regardless of
    // input. Same cadence as the PLAYING-state ANIM_IDLE so the
    // figure on the stats card breathes at the same pace as in-game.
    if (reckoning_anim_timer == 0) {
      reckoning_anim_frame = (u8)((reckoning_anim_frame + 1) & 1);
      reckoning_anim_timer = RECKONING_IDLE_TICKS;
    } else {
      --reckoning_anim_timer;
    }
    // B always exits to main menu (TURN BACK across the whole UI).
    if (input::pressed(input::B)) {
      state      = MAIN_MENU;
      menu_index = 2;
      return;
    }
    // Left/Right cycle 3 pages: 0 = stats card, 1 = portrait beat,
    // 2 = lifetime tally. Right advances, Left rewinds; both wrap.
    if (input::pressed(input::RIGHT)) {
      reckoning_page = (u8)((reckoning_page + 1) % 3);
      audio::play(&SFX_MENU);
      return;
    }
    if (input::pressed(input::LEFT)) {
      reckoning_page = (u8)((reckoning_page + 2) % 3);
      audio::play(&SFX_MENU);
      return;
    }
    // A is reserved for future Reckoning interactions (e.g. opening a
    // sub-page on the lifetime tally). For now, it's a no-op — the
    // dev preview cycle was removed when meta.vestige + meta.burden
    // became the source of truth. Use `make DEV_BURDEN=<name> ardens`
    // to preview different (vestige, burden) states.
    return;
  }

  if (state == SHADES_SCREEN) {
    const bool is_boss = (shades_cursor >= SHADES_FIRST_BOSS);
    switch (shades_view) {
    case SHADES_PORTRAIT:
    case SHADES_DETAIL: {
      // Inner pages: UP/DOWN walks across ALL shades (not just bosses
      // / not just minions). The view auto-adapts to the new cursor:
      //   * minion target (idx < FIRST_BOSS) → always DETAIL (PORTRAIT
      //     doesn't exist for minions).
      //   * boss target (idx ≥ FIRST_BOSS) → stays in whichever view
      //     the player STARTED browsing in (sticky_was_portrait,
      //     captured below). Pressing UP from Charon's PORTRAIT to a
      //     minion's DETAIL then DOWN back to Charon should land on
      //     PORTRAIT, not DETAIL — the player's "browsing keepers"
      //     intent is preserved across the minion gap.
      // LEFT/RIGHT intentionally not bound — vertical-only matches the
      // on-screen visual cue.
      static bool sticky_was_portrait = false;  // updated on every nav
      if (shades_view == SHADES_PORTRAIT)
        sticky_was_portrait = true;
      else if (is_boss)
        sticky_was_portrait = false;  // boss DETAIL == explicit detail

      if (input::pressed(input::DOWN) || input::pressed(input::UP)) {
        menu_cursor_step(shades_cursor, SHADES_COUNT);
        const bool target_is_boss = (shades_cursor >= SHADES_FIRST_BOSS);
        shades_view = (target_is_boss && sticky_was_portrait) ? SHADES_PORTRAIT : SHADES_DETAIL;
      }
      if (input::pressed(input::A) && shades_view == SHADES_DETAIL && is_boss) {
        shades_view         = SHADES_PORTRAIT;
        sticky_was_portrait = true;
        audio::play(&SFX_MENU);
      }
      if (input::pressed(input::B)) {
        if (shades_view == SHADES_PORTRAIT) {
          shades_view = SHADES_DETAIL;
        } else {
          shades_view = SHADES_LIST;
        }
        audio::play(&SFX_MENU);
      }
      break;
    }
    case SHADES_LIST:
    default:
      // Scrollable list: arrows scroll, A opens detail, B returns to GRIMOIRE.
      menu_cursor_step4(shades_cursor, SHADES_COUNT);
      if (input::pressed(input::A)) {
        shades_view = SHADES_DETAIL;
        audio::play(&SFX_MENU);
      }
      if (input::pressed(input::B)) {
        state        = TEXT_SCREEN;
        text_section = TEXT_SECTION_LIST;
        text_cursor  = 4;
        audio::play(&SFX_MENU);
      }
      break;
    }
    return;
  }

  // NUMERALS lookup-list: scroll-only, no detail page. A is a no-op (the
  // value is already on-screen next to the symbol). B returns to GRIMOIRE.
  if (state == NUMERALS_SCREEN) {
    menu_cursor_step4(numerals_cursor, NUMERALS_COUNT);
    if (input::pressed(input::B)) {
      state        = TEXT_SCREEN;
      text_section = TEXT_SECTION_LIST;
      text_cursor  = 2;
      audio::play(&SFX_MENU);
    }
    return;
  }

  // LEXICON lookup-list: identical to NUMERALS — scroll, B returns.
  if (state == LEXICON_SCREEN) {
    menu_cursor_step4(lexicon_cursor, LEXICON_COUNT);
    if (input::pressed(input::B)) {
      state        = TEXT_SCREEN;
      text_section = TEXT_SECTION_LIST;
      text_cursor  = 3;
      audio::play(&SFX_MENU);
    }
    return;
  }

  if (state == VESTIGIA_SCREEN) {
    // Idle-bob the row icons every RECKONING_IDLE_TICKS frames. Reuses
    // the same timer/frame variables RECKONING uses, so visiting both
    // screens keeps the figure breathing at one cadence across the UI.
    if (reckoning_anim_timer == 0) {
      reckoning_anim_frame = (u8)((reckoning_anim_frame + 1) & 1);
      reckoning_anim_timer = RECKONING_IDLE_TICKS;
    } else {
      --reckoning_anim_timer;
    }

    if (vestigia_popup != 0) {
      // Popup active: input drives popup options, B cancels.
      // Mode 3 has 3 options (LOAD/OVERWRITE/CANCEL); modes 1-2 have 2.
      const u8 n_opts = (vestigia_popup == 3) ? 3 : 2;
      menu_cursor_step(vestigia_popup_cursor, n_opts);
      if (input::pressed(input::B)) {
        vestigia_popup        = 0;
        vestigia_popup_cursor = 0;
        audio::play(&SFX_MENU);
        return;
      }
      if (input::pressed(input::A)) {
        // Last option in every mode is CANCEL (index n_opts-1).
        if (vestigia_popup_cursor == (u8)(n_opts - 1)) {
          vestigia_popup        = 0;
          vestigia_popup_cursor = 0;
          audio::play(&SFX_MENU);
          return;
        }
        // SFX before write so audio::silence_pins() in the SPI write
        // doesn't clip the press-feedback sound.
        audio::play(&SFX_MENU);
        vestigia_popup_confirm();
        return;
      }
      return;
    }

    // No popup: normal slot-list navigation.
    menu_cursor_step(vestigia_cursor, vestigia::SLOT_COUNT);
    if (input::pressed(input::A)) {
      // Open popup. Mode depends on slot state.
      if (vestigia_cursor == vestigia::SLOT_UNBURDENED) {
        vestigia_popup = 1;  // AUTOSAVE: LOAD / CANCEL
      } else if (vestigia::slot_occupied(vestigia_cursor)) {
        vestigia_popup = 3;  // occupied: LOAD / OVERWRITE / CANCEL
      } else {
        vestigia_popup = 2;  // empty: SAVE / CANCEL
      }
      vestigia_popup_cursor = 0;
      audio::play(&SFX_MENU);
      return;
    }
    if (input::pressed(input::B)) {
      state      = MAIN_MENU;
      menu_index = 3;  // restore wood cursor at VESTIGIA row
      audio::play(&SFX_MENU);
    }
    return;
  }

  if (state == TEXT_SCREEN) {
    if (text_section == TEXT_SECTION_LIST) {
      // Top-level list: CONTROLS / LEXICON / ABOUT.
      menu_cursor_step(text_cursor, TEXT_TOP_SECTIONS);
      if (input::pressed(input::A)) {
        // Row 0 -> COMMANDS body, 1 -> LORE sub-list. Rows 2..4 are
        // encyclopedia screens (list+detail) hosted as their own top-level
        // states; their B button returns here with the cursor restored.
        switch (text_cursor) {
        case 0: text_section = 0; break;
        case 1:
          text_section = TEXT_SECTION_ABOUT;
          text_cursor  = 0;
          break;
        case 2:
          state           = NUMERALS_SCREEN;
          numerals_cursor = 0;
          break;
        case 3:
          state          = LEXICON_SCREEN;
          lexicon_cursor = 0;
          break;
        case 4:
          state         = SHADES_SCREEN;
          shades_cursor = 0;
          shades_view   = SHADES_LIST;
          break;
        }
        audio::play(&SFX_MENU);
      }
      if (input::pressed(input::B)) {
        state      = MAIN_MENU;
        menu_index = 3;  // leave the cursor on the GRIMOIRE row
        audio::play(&SFX_MENU);
      }
    } else if (text_section == TEXT_SECTION_ABOUT) {
      // ABOUT sub-list: row 0 -> DESCENT body (section 2),
      // row 1 -> GUIDE body (section 3).
      menu_cursor_step(text_cursor, TEXT_ABOUT_SECTIONS);
      if (input::pressed(input::A)) {
        // Row -> body section mapping (rows 0..3 -> sections 2..5):
        //   0 -> THE DESCENT  (section 2)
        //   1 -> THE GUIDE    (section 3)
        //   2 -> THE SANGUE   (section 4)
        //   3 -> THE VIRTU    (section 5)
        // The beasts live in MAIN_MENU → ROLL OF SHADES; LORE no longer
        // duplicates that door.
        text_section = (u8)(2 + text_cursor);
        audio::play(&SFX_MENU);
      }
      if (input::pressed(input::B)) {
        text_section = TEXT_SECTION_LIST;
        text_cursor  = 1;  // keep the top-level cursor on LORE for continuity
        audio::play(&SFX_MENU);
      }
    } else {
      // Body page. B returns to whichever list opened it: section 0
      // (COMMANDS) comes from the top list, sections 2..5 come from the
      // LORE sub-list. Section 1 (LEXICON, retired) is never opened.
      if (input::pressed(input::B)) {
        u8 s = text_section;
        if (s >= 2) {
          text_section = TEXT_SECTION_ABOUT;
          text_cursor  = (u8)(s - 2);  // DESCENT->0 .. VIRTU->3
        } else {
          text_section = TEXT_SECTION_LIST;
          text_cursor  = 0;  // back to COMMANDS row
        }
        audio::play(&SFX_MENU);
      }
    }
    return;
  }

  if (state == GUIDE_SCREEN) {
    GuideAction actions[3];
    u8 n = guide_visible_actions(actions);
    // Clamp in case the availability changed since last frame (e.g. player
    // picked Offerings -> upgrade -> came back with different wallet).
    if (menu_index >= n) menu_index = 0;

    menu_cursor_step(menu_index, n);
    if (input::pressed(input::A)) {
      switch (actions[menu_index]) {
      case GA_RELIC:
        // Flat 5-sangue full heal. guide_visible_actions() guarantees we
        // can afford it and have damage to undo, so no re-check needed.
        meta.sangue_vessel = (u16)(meta.sangue_vessel - RELIC_COST);
        player->hp         = player->max_hp;
        audio::play(&SFX_PICKUP);
        resume_play();  // interlude ends; CIRCLE_CARD if circle changed, else PLAYING
        break;
      case GA_OFFERINGS:
        // Mid-run stat upgrade. Flag tells UPGRADE_MENU's B handler to
        // return to MERCHANT instead of MAIN_MENU.
        upgrade_from_guide = 1;
        state              = UPGRADE_MENU;
        upgrade_cursor     = 0;
        audio::play(&SFX_MENU);
        break;
      case GA_CHALICE: {
        u8 lo, hi;
        chalice_range(lo, hi);
        u8 gain            = chalice_roll(lo, hi);
        meta.sangue_vessel = (u16)(meta.sangue_vessel + gain);
        audio::play(&SFX_PICKUP);
        resume_play();
        break;
      }
      }
    }
    // B is intentionally not handled — the player is locked into the choice.
    return;
  }
}  // update_main_menu_scene

SCENE_ENTRY(PLAY) void update_play_scene() {
  // B dispatch for PLAYING / PAUSED: press-edge toggles pause. (The old
  // in-engine debug spawner on hold-B has been replaced by the PC-only
  // --spawn=<SPRITE> CLI flag — see platform/sdl/cli.cpp.)
  if (state == PLAYING || state == PAUSED) {
    if (input::pressed(input::B)) {
      state            = (state == PLAYING) ? PAUSED : PLAYING;
      menu_index       = 0;
      pause_confirming = 0;
    }
  }

  if (state == SECOND_DEATH) {
    // RETRY | MENU
    constexpr u8 SECOND_DEATH_OPTS = 2;
    // cursor-wrap-ok: 4-direction toggle, no SFX — RETRY|MENU is binary,
    // any d-pad press flips. menu_cursor_step doesn't fit this shape.
    if (input::pressed(input::LEFT) || input::pressed(input::RIGHT) || input::pressed(input::UP) ||
        input::pressed(input::DOWN)) {
      menu_index = (u8)((menu_index + 1) % SECOND_DEATH_OPTS);
    }
    if (input::pressed(input::A)) {
      if (menu_index == 0)
        start_run();
      else {
        return_to_wood();
      }
    }
    return;
  }

  if (state == PAUSED) {
    if (pause_confirming) {
      // "Dost thou turn back?" — yes quits the run, no returns to pause.
      constexpr u8 CONFIRM_OPTS = 2;  // YEA, UNTO THE WOOD / NAY, I PRESS ON
      // cursor-wrap-ok: 2-option toggle on EITHER direction (single
      // movement axis collapsed). menu_cursor_step is up/down only.
      if (input::pressed(input::DOWN) || input::pressed(input::UP)) {
        menu_index = (u8)((menu_index + 1) % CONFIRM_OPTS);
        audio::play(&SFX_MENU);
      }
      if (input::pressed(input::A)) {
        if (menu_index == 0) {
          pause_confirming = 0;
          end_run(/*abandoned=*/true);  // pilgrim turns back — straight to wood, no damnation
        } else {
          pause_confirming = 0;
          menu_index       = 2;  // leave cursor on UNTO THE WOOD
        }
      }
      if (input::pressed(input::B)) {
        // Back out of the confirm without quitting.
        pause_confirming = 0;
        menu_index       = 1;  // restore cursor to UNTO THE WOOD row
        audio::play(&SFX_MENU);
      }
      return;
    }

    constexpr u8 PAUSE_OPTS = 2;  // PRESS ON, UNTO THE WOOD
    // cursor-wrap-ok: pause uses no SFX on cursor move (the menu is
    // already blocking gameplay; an extra beep would feel intrusive).
    if (input::pressed(input::DOWN)) menu_index = (u8)((menu_index + 1) % PAUSE_OPTS);
    if (input::pressed(input::UP)) menu_index = (u8)((menu_index + PAUSE_OPTS - 1) % PAUSE_OPTS);
    if (input::pressed(input::A)) {
      switch (menu_index) {
      case 0: state = PLAYING; break;
      case 1:
        // Don't quit immediately — ask "dost thou turn back?" first. The
        // pilgrim's moment of doubt at Canto I.36 ("many times I to return
        // had turned").
        pause_confirming = 1;
        menu_index       = 1;  // default cursor on NAY (safer)
        audio::play(&SFX_MENU);
        break;
      }
    }
    return;
  }

  // PLAYING: wave management.
#ifdef ENABLE_DEBUG_OVERRIDES
  if (!game::debug_no_enemies()) {
#endif
    if (!any_enemies_alive() && spawn_pause_done == 0) {
      if (wave_cooldown > 0)
        --wave_cooldown;
      else {
        spawn_wave(wave);
        spawn_pause_done = 1;
      }
    } else if (any_enemies_alive() && spawn_pause_done == 1) {
      // wait
    } else if (!any_enemies_alive() && spawn_pause_done == 1) {
      ++wave;
      wave_cooldown    = WAVE_SPAWN_DELAY;
      spawn_pause_done = 0;
      // The Guide appears before each boss wave (3, 6, 9, ...): heal, upgrade,
      // or gamble sangue. The player is locked during this interlude.
      if (wave % 3 == 0) {
        state      = GUIDE_SCREEN;
        menu_index = 0;
      }
    }
#ifdef ENABLE_DEBUG_OVERRIDES
  }
#endif

  for (u8 i = 0; i < ent::POOL_SIZE; ++i) {
    ent::Entity& e = ent::pool[i];
    if (!e.active) continue;
    switch (e.kind) {
    case ent::PLAYER: update_player(); break;
    case ent::ENEMY: update_enemy(e); break;
    case ent::PLAYER_SHOT:
    case ent::ENEMY_SHOT: update_bullet(e); break;
    case ent::PICKUP: update_pickup(e); break;
    default: break;
    }
  }

  resolve_collisions();
}

// Title screen. Full-screen Mars vista with the game name and a prompt
// overlaid. The image takes most of the screen; we use inverse text in a
SCENE_FN(TITLE) void draw_title() {
  // Beatrice's portrait (58×58) centered as the title art. Read direct
  // from FX every frame — ~5–10 ms SPI cost on a static screen, fine.
  // The earlier Mars vista (LZ77 forest landscape) lived at
  // OFFSET_TITLE_LZ77 and is preserved on FX for a possible future
  // intro splash; the title scene no longer references it. To restore
  // it from history: see commit 6adcf07.
  fb::clear();
  constexpr i16 BEATRICE_X = (fb::WIDTH - 58) / 2;   // 35
  constexpr i16 BEATRICE_Y = (fb::HEIGHT - 58) / 2;  // 3
  draw_fx_sprite(BEATRICE_X, BEATRICE_Y, 58, 58, fxdata::OFFSET_BEATRICE);

  // Top: a single black bar holding the gothic title sprite. Sized for
  // symmetric padding around the current 12-tall logo (2 px above, 2 px
  // below); if the logo grows or shrinks, update TOP_BAR_H to logo_h + 4.
  // Drawn OVER Beatrice so the logo is always legible.
  const u8 logo_w        = sprites::width(sprites::LOGO_TITLE);
  const u8 logo_h        = sprites::height(sprites::LOGO_TITLE);
  constexpr u8 TOP_BAR_H = 16;  // logo_h (12) + 2 px top + 2 px bottom
  fb::clear_rect(0, 0, fb::WIDTH, TOP_BAR_H);

  i16 logo_x = (fb::WIDTH - (i16)logo_w) / 2;
  i16 logo_y = (TOP_BAR_H - (i16)logo_h) / 2;  // = 2 for 16-tall bar, 12-tall logo
  draw_logo(sprites::LOGO_TITLE, logo_x, logo_y);

  // Bottom bar: clear strip and center the PRESS A prompt vertically in it.
  // Strip is 9 px; glyph is 5 px tall; (9-5)/2 = 2 px above, 2 px below.
  // Drawn OVER Beatrice — the prompt always reads.
  constexpr u8 BOT_BAR_H     = 9;
  constexpr i16 BOT_BAR_Y    = fb::HEIGHT - BOT_BAR_H;  // 55
  constexpr i16 PROMPT_TOP_Y = BOT_BAR_Y + 2;           // 57 — centered
  fb::clear_rect(0, BOT_BAR_Y, fb::WIDTH, BOT_BAR_H);
  if ((clock::frame_count >> 5) & 1) {
    static const char PROMPT[] PROGMEM = "PRESS A";
    char pb[8];
    copy_pgm_str(PROMPT, pb, sizeof(pb));
    i16 prompt_x = (fb::WIDTH - (i16)7 * (i16)font::STRIDE) / 2;
    font::draw_text(prompt_x, PROMPT_TOP_Y, pb);
  }

  // Light-mode flip: pre-Hell screens render black-on-white. The pilgrim
  // has not yet crossed Acheron; the world above still has daylight.
  fb::invert_all();
}

// ---- Transition cards ---------------------------------------------------
// Shared helper: draw a centered title strip across the middle of the
// frame so text reads on a solid black band against any background art.
// Used by both GATE_CARD and CIRCLE_CARD.
SCENE_FN(GATE) void draw_card_text_strip(const char* line) {
  // Measure the line so we can both clear a tight band and center text.
  u8 len           = strlen_(line);
  const i16 text_w = (i16)len * (i16)font::STRIDE;
  const i16 text_x = (fb::WIDTH - text_w) / 2;
  // Black band: 8 px tall centered vertically. font::draw_text writes
  // white-on-black anyway, so a clear-rect under it is enough — no
  // inverse helper needed.
  constexpr i16 BAND_Y = 28;
  constexpr u8 BAND_H  = 9;
  fb::clear_rect(0, BAND_Y, fb::WIDTH, BAND_H);
  font::draw_text(text_x, BAND_Y + 2, line);
}

// Render a "fade level" (0..16) of the gate image into fb::buffer.
//   level == 0  -> empty (no pixels lit)
//   level == 16 -> full image
// Each pixel of the source contributes only if its 4x4 Bayer threshold
// is below the current level (shared engine table; see fb::bayer_threshold).
//
// Implementation: iterate column-by-column, build the page byte from the
// source byte ANDed with a per-column Bayer mask. Cheaper than walking
// every pixel individually.
// Lay the GATE scene into the framebuffer via the tilemap renderer.
// Clears first because tilemap::draw uses OR-blits (per the sprite
// renderer's standard semantics) — without a clear, residue from the
// previous frame leaks through and the gate looks permanently double-
// exposed. Was the regression that broke the GATE_CARD transition the
// first time this was wired.
SCENE_FN(GATE) void draw_gate_full() {
  fb::clear();
  // Palette lives on FX flash; layout stays in PROGMEM. tilemap::draw_fx
  // reads each non-empty tile (32 B) into a stack buffer via
  // data_flash::read. ~7 ms total render — cold path, only fires during
  // GATE_CARD transitions.
  tilemap::draw_fx(fxdata::OFFSET_GATE_PALETTE, images::GATE_LAYOUT_data);
}

SCENE_FN(GATE) void draw_gate_faded(u8 level) {
  if (level >= 16) {
    draw_gate_full();
    return;
  }
  if (level == 0) {
    fb::clear();
    return;
  }
  // Lay the gate down at full strength, then walk the framebuffer and
  // clear pixels whose Bayer threshold is >= the requested fade level.
  // (Was a single byte-major masking pass over GATE_data; can't read the
  // tile palette as a flat 1024-B image, so we render-then-mask. Same
  // visual result; one extra pass over the framebuffer per fade frame,
  // dwarfed by the SPI flush time anyway.)
  draw_gate_full();
  for (u8 page = 0; page < 8; ++page) {
    for (u8 x = 0; x < 128; ++x) {
      u8 mask = 0;
      for (u8 b = 0; b < 8; ++b) {
        const u8 y = (u8)(page * 8 + b);
        if (fb::bayer_threshold(x, y) < level) mask |= (u8)(1 << b);
      }
      fb::buffer[(u16)page * 128 + x] &= mask;
    }
  }
}

// Set framebuffer pixels in a rectangle to white, masked by the same Bayer
// reveal pattern used for the gate. At level 16 the rect is fully solid;
// at level 0 it's invisible. Clipped to screen bounds. Used for the top
// and bottom plaque bays in the gate ritual.
void fill_rect_bayer(i16 x, i16 y, u8 w, u8 h, u8 level) {
  if (level == 0) return;
  if (level >= 16) {
    fb::fill_rect(x, y, w, h);
    return;
  }
  for (u8 dy = 0; dy < h; ++dy) {
    const i16 py = y + (i16)dy;
    if (py < 0 || py >= (i16)fb::HEIGHT) continue;
    for (u8 dx = 0; dx < w; ++dx) {
      const i16 px = x + (i16)dx;
      if (px < 0 || px >= (i16)fb::WIDTH) continue;
      if (fb::bayer_threshold((u8)px, (u8)py) < level) fb::set_pixel(px, py);
    }
  }
}

// Trim trailing spaces from the pilgrim's 6-char name and append a comma.
// Writes into `out` (cap >= NAME_LEN + 2). Returns chars written.
u8 build_pilgrim_address(char* out) {
  u8 n = 0;
  for (u8 i = 0; i < storage::NAME_LEN; ++i) {
    char c = meta.name[i];
    if (c >= 'A' && c <= 'Z') out[n++] = c;
  }
  out[n++] = ',';
  out[n]   = 0;
  return n;
}

// Compute whether a "flash blink" is currently visible at elapsed time
// `t_in_stage` (frames since the text first appeared). The text holds for
// 6 frames, blanks for 4, three times, then settles. Total ~30 frames.
// After the blink loop finishes, returns true (text holds visible).
bool blink_visible(u16 t_in_stage) {
  if (t_in_stage >= 30) return true;  // settled
  // 10-frame cycle: 6 visible, 4 hidden.
  return (t_in_stage % 10) < 6;
}

// "Gates of Hell" staged transition.
//   - Bayer-mask fade-in over 60 frames
//   - Pilgrim's name flashes at top (with comma)
//   - ABANDON / ALL / HOPE flash in at the bottom, one at a time
//   - Bayer-mask fade-out over 90 frames
// Skips to CIRCLE_CARD on A or after the timeline ends.
// Per-transition dirty cache for the gate ritual. The visible output
// only changes on a small set of state edges (level steps, word blink
// flips, stage boundaries) — re-rendering on every gate_t tick is
// pointless. Cache the visible-state tuple; if unchanged, return
// without redrawing.
//
// Reset to sentinels at gate entry (gate_t == 0) so the first frame of
// each gate ritual paints fresh.
namespace {
u8 last_gate_stage       = 0xFF;  // 0=fade-in, 1=words, 2=flash, 3=dark-hold, 4=fade-out
u8 last_gate_level       = 0xFF;
u8 last_gate_name_vis    = 0xFF;
u8 last_gate_abandon_vis = 0xFF;
u8 last_gate_all_vis     = 0xFF;
u8 last_gate_hope_vis    = 0xFF;
}  // namespace

SCENE_FN(GATE) void draw_gate_card() {
  // First-frame reset of the cache.
  if (gate_t == 0) {
    last_gate_stage       = 0xFF;
    last_gate_level       = 0xFF;
    last_gate_name_vis    = 0xFF;
    last_gate_abandon_vis = 0xFF;
    last_gate_all_vis     = 0xFF;
    last_gate_hope_vis    = 0xFF;
  }

  // ---- Exit drama ------------------------------------------------------
  // After all three inscription words have appeared, punch a 5-frame white
  // flash, then HOLD the dark-mode gate for ~1s ("thou art in Hell now"),
  // then dither out into pure black before CIRCLE_CARD takes over.
  if (gate_t >= GATE_HOPE_END) {
    if (gate_t < GATE_FLASH_END) {
      // White flash. Repaint only on stage entry; held thereafter.
      if (last_gate_stage == 2) return;
      fb::clear();
      fb::invert_all();
      last_gate_stage = 2;
      return;
    }
    if (gate_t < GATE_DARK_HOLD_END) {
      // Dark-mode hold. Repaint only on stage entry.
      if (last_gate_stage == 3) return;
      draw_gate_full();
      last_gate_stage = 3;
      return;
    }
    // Dither fade-out. Repaint only when level changes.
    const u16 dt       = (u16)(gate_t - GATE_DARK_HOLD_END);
    const u16 fade_len = (u16)(GATE_FADE_OUT_END - GATE_DARK_HOLD_END);
    const u16 inv      = (dt * 16) / fade_len;
    const u8 level     = (u8)(inv >= 16 ? 0 : 16 - inv);
    if (last_gate_stage == 4 && last_gate_level == level) return;
    draw_gate_faded(level);
    last_gate_stage = 4;
    last_gate_level = level;
    return;
  }

  // ---- Light-mode ritual (entry through HOPE) --------------------------
  // 1) Background gate at the appropriate fade level.
  u8 level;
  if (gate_t < GATE_FADE_IN_END) {
    // Fade in: 0..16 across 60 frames.
    level = (u8)((gate_t * 16) / GATE_FADE_IN_END);
    if (level > 16) level = 16;
  } else {
    level = 16;  // fully visible while text appears
  }

  // Compute word visibility flags up-front; the dirty cache compares
  // these against last frame to decide whether anything changed.
  const u8 stage_now = (gate_t < GATE_FADE_IN_END) ? 0 : 1;
  u8 name_vis        = 0;
  u8 abandon_vis     = 0;
  u8 all_vis         = 0;
  u8 hope_vis        = 0;
  if (level >= 16 && gate_t >= GATE_SILENT_END) {
    if (blink_visible((u16)(gate_t - GATE_SILENT_END))) name_vis = 1;
  }
  if (level >= 16 && gate_t >= GATE_NAME_END) {
    if (blink_visible((u16)(gate_t - GATE_NAME_END))) abandon_vis = 1;
  }
  if (level >= 16 && gate_t >= GATE_ABANDON_END) {
    if (blink_visible((u16)(gate_t - GATE_ABANDON_END))) all_vis = 1;
  }
  if (level >= 16 && gate_t >= GATE_ALL_END) {
    if (blink_visible((u16)(gate_t - GATE_ALL_END))) hope_vis = 1;
  }
  // Cache hit: same stage, same level, same word-visibility tuple.
  if (stage_now == last_gate_stage && level == last_gate_level && name_vis == last_gate_name_vis &&
      abandon_vis == last_gate_abandon_vis && all_vis == last_gate_all_vis &&
      hope_vis == last_gate_hope_vis) {
    return;
  }

  draw_gate_faded(level);

  // 2) Per-word plaques. Instead of a full top/bottom strip, each word
  // gets its own tight white backing — the inscription burns itself into
  // the gate face one phrase at a time. Plaque draws only when its word
  // is currently visible (so the backing blinks with the text).
  constexpr i16 TOP_TEXT_Y = 4;
  constexpr i16 BOT_TEXT_Y = 56;
  constexpr i16 PAD_X      = 1;  // horizontal padding around each word
  constexpr i16 PAD_Y      = 1;  // vertical padding (5-tall glyphs => 7-tall plaque)
  constexpr u8 PLAQUE_H    = (u8)(font::GLYPH_H + 2 * PAD_Y);

  // Helper-shape: lay an inverse-text label over its own Bayer plaque.
  auto draw_word_plaque = [&](i16 x, i16 y, const char* s, u8 len) {
    const i16 plaque_x = x - PAD_X;
    const i16 plaque_y = y - PAD_Y;
    const u8 plaque_w  = (u8)(len * font::STRIDE + 2 * PAD_X);
    fill_rect_bayer(plaque_x, plaque_y, plaque_w, PLAQUE_H, level);
    if (level >= 16) font::draw_text_inverse(x, y, s);
  };

  if (name_vis) {
    char addr[storage::NAME_LEN + 2];
    const u8 n  = build_pilgrim_address(addr);
    const i16 x = ((i16)fb::WIDTH - (i16)n * (i16)font::STRIDE) / 2;
    draw_word_plaque(x < 0 ? 0 : x, TOP_TEXT_Y, addr, n);
  }
  if (abandon_vis) draw_word_plaque(32, BOT_TEXT_Y, "ABANDON", 7);
  if (all_vis) draw_word_plaque(64, BOT_TEXT_Y, "ALL", 3);
  if (hope_vis) draw_word_plaque(80, BOT_TEXT_Y, "HOPE", 4);

  // Pilgrim still stands above ground for the inscription ritual — light
  // mode. After HOPE the upper branch above takes over and we render dark.
  fb::invert_all();

  // Update cache.
  last_gate_stage       = stage_now;
  last_gate_level       = level;
  last_gate_name_vis    = name_vis;
  last_gate_abandon_vis = abandon_vis;
  last_gate_all_vis     = all_vis;
  last_gate_hope_vis    = hope_vis;
}

// Per-circle title card. Inverted strategy-E art for the circle as the
// background; "CIRCLE <Roman>: <NAME>" overlaid on a black band.
//
// Per-circle background art is currently disabled (3 KB flash recovered);
// the LIMBO/VIOLENCE/TREACHERY tripartite split was for the original
// 3-tile atlas in backgrounds.cpp.disabled. If you re-enable the art,
// restore the CIRCLE_TO_CARD lookup below.
// const u8 CIRCLE_TO_CARD[9] PROGMEM = {0, 0, 0, 1, 1, 1, 2, 2, 2};

SCENE_FN(GATE) void draw_circle_card() {
  const u8 c = circle_for_wave(wave);
  // Background art (per-circle dithered architecture) used to fill the
  // frame here via backgrounds::render(card, fb::buffer); that consumed
  // 3 KB of flash for 1.5-second transition cards. Replaced with a clean
  // black field — the centered "CIRCLE III: NAME" strip below carries
  // the screen alone, in keeping with dark-mode = in-Hell convention.
  // The art + renderer are preserved in backgrounds.cpp.disabled and
  // backgrounds.h.disabled; restore by un-renaming if you ever buy back
  // the flash budget.
  fb::clear();

  // Build "CIRCLE <Roman>: <NAME>" into a stack buffer.
  static const char N0[] PROGMEM            = "LIMBO";
  static const char N1[] PROGMEM            = "LUST";
  static const char N2[] PROGMEM            = "GLUTTONY";
  static const char N3[] PROGMEM            = "GREED";
  static const char N4[] PROGMEM            = "WRATH";
  static const char N5[] PROGMEM            = "HERESY";
  static const char N6[] PROGMEM            = "VIOLENCE";
  static const char N7[] PROGMEM            = "FRAUD";
  static const char N8[] PROGMEM            = "TREACHERY";
  static const char* const NAMES[9] PROGMEM = {N0, N1, N2, N3, N4, N5, N6, N7, N8};

  char line[24];
  u8 n = strcpy_(line, "CIRCLE ");
  n += format_roman((u8)(c + 1), line + n);
  line[n++] = ':';
  line[n++] = ' ';
  // Circle name from PROGMEM
  const char* name = (const char*)pgm_read_ptr(&NAMES[c]);
  for (u8 i = 0; i < (u8)(sizeof(line) - n - 1); ++i) {
    char ch = (char)pgm_read_byte(&name[i]);
    if (!ch) break;
    line[n++] = ch;
  }
  line[n] = 0;

  draw_card_text_strip(line);
}

// ---- Name entry --------------------------------------------------------
// 3 letter cells, A-Z, cursor moves L/R, letter cycles U/D, A confirms.
SCENE_FN(MAIN_MENU) void draw_name_entry() {
  fb::clear();
  font::draw_text_pgm(38, 4, LIT_AVOW_THY_NAME);
  // 6 letter slots, each 8px wide, centered.
  constexpr i16 SLOT_W = 8;
  i16 total_w          = (i16)storage::NAME_LEN * SLOT_W;
  i16 base_x           = (fb::WIDTH - total_w) / 2;
  // Underline under the active slot blinks on the same phase as the title
  // prompt (0.5s duty cycle) so the player can see which letter the D-pad
  // is editing without any explicit hint.
  const bool cursor_on = ((clock::frame_count >> 5) & 1) != 0;
  for (u8 i = 0; i < storage::NAME_LEN; ++i) {
    i16 x  = base_x + (i16)i * SLOT_W;
    char c = name_buf[i];
    if (c < 'A' || c > 'Z') c = 'A';
    char s[2] = {c, 0};
    font::draw_text(x + 2, 24, s);
    if (i == name_cursor && cursor_on) {
      fb::fill_rect(x, 32, SLOT_W - 2, 1);
    }
  }
  // Confirm + back hints. The letter/cursor affordance is carried by the
  // blinking underline, so an explicit ARROWS footer is redundant.
  draw_footer_pgm(LIT_ACPRESS_ON_BCTURN_BACK);
  // Pre-Hell: light mode.
  fb::invert_all();
}

// ---- Tutorial (one-shot welcome for brand-new pilgrims) ----------------
// Shown exactly once, after NAME_ENTRY on a save with total_runs == 0.
// Two slides of the TEXT content reused verbatim:
//   slide 0 -> CONTROLS body (section id 0), footer "A:PRESS ON"
//   slide 1 -> THE DESCENT body (section id 2), footer "B:BACK  A:BEGIN"
// No new assets — the slideshow is pure reuse + one extra state byte.
//
// Forward decls — the body renderer and footer strings are defined later
// in this file next to the rest of the TEXT-screen code.
// Single welcome screen shown to brand-new pilgrims after NAME_ENTRY.
// Five lines paraphrasing Inferno I.1-3 (Longfellow), addressing the
// pilgrim by name. No mechanics — mystery is the point; the systems are
// revealed by play and by GRIMOIRE.
//
//   THOU ART <NAME>.
//
//   MIDWAY THIS MORTAL LIFE
//   THOU FINDEST THYSELF
//   IN THE SELVA OSCURA.
//
//   THE WAY IS LOST.
//
// All literals in PROGMEM so the welcome poem doesn't burn ~80 B of RAM.
SCENE_FN(MAIN_MENU) void draw_tutorial() {
  static const char TUT_PREFIX[] PROGMEM = "THOU ART ";
  static const char TUT_LINE_1[] PROGMEM = "MIDWAY THIS MORTAL LIFE";
  static const char TUT_LINE_2[] PROGMEM = "THOU FINDEST THYSELF";
  static const char TUT_LINE_3[] PROGMEM = "IN THE SELVA OSCURA";
  static const char TUT_CLOSER[] PROGMEM = "THE WAY IS LOST";
  static const char TUT_FOOTER[] PROGMEM = "A:PRESS ON  B:TURN BACK";

  fb::clear();

  // Build "THOU ART <NAME>." into a stack buffer.
  char addr[10 + storage::NAME_LEN + 1 + 1];  // "THOU ART " + name + "." + NUL
  u8 n = 0;
  for (u8 i = 0;; ++i) {
    char c = (char)pgm_read_byte(&TUT_PREFIX[i]);
    if (!c) break;
    addr[n++] = c;
  }
  for (u8 i = 0; i < storage::NAME_LEN; ++i)
    addr[n++] = meta.name[i];
  addr[n] = 0;

  // Layout: rule of threes, 9-px row height. Body rows at y=3, 21, 30, 39
  // with blank gaps between for readability.
  char buf[24];
  font::draw_text(2, 3, addr);
  copy_pgm_str(TUT_LINE_1, buf, sizeof(buf));
  font::draw_text(2, 21, buf);
  copy_pgm_str(TUT_LINE_2, buf, sizeof(buf));
  font::draw_text(2, 30, buf);
  copy_pgm_str(TUT_LINE_3, buf, sizeof(buf));
  font::draw_text(2, 39, buf);
  copy_pgm_str(TUT_CLOSER, buf, sizeof(buf));
  font::draw_text(2, fb::HEIGHT - 16, buf);
  copy_pgm_str(TUT_FOOTER, buf, sizeof(buf));
  font::draw_text(2, fb::HEIGHT - 7, buf);

  // Pre-Hell: light mode.
  fb::invert_all();
}

// ---- Main menu ---------------------------------------------------------
// Currently CORE-resident. Originally promoted out of the MAIN_MENU bank
// because draw_wood_transition (GATE bucket) painted the menu underneath
// its dither sweep, which crossed banks. The dither was retired and
// draw_wood_transition deleted, so the cross-bank dependency is gone —
// this function could now move back into the MAIN_MENU bank. Leaving
// in CORE for now to avoid perturbing layout; revisit if CORE flash
// pressure makes the bank move worthwhile (~268 B reclaimed).
void draw_main_menu() {
  // Forest backdrop FIRST — anything drawn before this gets clobbered.
  // PREVIEW: decode the LZ77-compressed forest straight into fb::buffer
  // (skips the intermediate full-image copy; the decode WRITES the 1024
  // bytes the framebuffer expects). After decode we blank the top 9 rows
  // so the HUD/separator can paint into a clean strip.
  lz77::decode_from_fx(fxdata::OFFSET_FOREST_LZ77, fb::buffer);
  fb::clear_rect(0, 0, fb::WIDTH, 9);

  // HUD strip across the top, full width. The pilgrim is currently *in*
  // the dark wood (Inferno I: "mi ritrovai per una selva oscura"), so the
  // hub honors only the place's name — no resource counters, no pilgrim
  // identity. Sangue is checked in RECKONING and spent in OFFERINGS;
  // the hub stays pure place-and-mood. Manuscript-style ornamental frieze
  // flanks the header — three daggers (†) alternating with three lozenges
  // (◊) per side at 1-px built-in glyph spacing, echoing the threefold
  // structure that runs through the Commedia (Trinity, terza rima, three
  // blessed ladies). ('+' renders as dagger, '*' as lozenge via the font
  // glyph table.)
  // PROGMEM-resident frieze (saves ~33 B RAM that .data would burn). Copied
  // to a stack buffer at draw time and rendered via the standard text path.
  static const char HEADER_LINE[] PROGMEM =
      "+*+*+*+*+ SELVA OSCURA +*+*+*+*+";  // 32 ch × 4 = 128 px
  constexpr u8 HEADER_LEN = sizeof(HEADER_LINE) - 1;
  constexpr i16 HEADER_X  = (fb::WIDTH - (i16)((u16)HEADER_LEN * font::STRIDE)) / 2;
  char header_buf[HEADER_LEN + 1];
  copy_pgm_str(HEADER_LINE, header_buf, sizeof(header_buf));
  font::draw_text(HEADER_X, 1, header_buf);
  // Separator line below HUD.
  draw_header_rule();

  // Menu plaque: framed box centered in the forest area (y 9..63 = 55 px
  // tall, 128 wide). Engraved-plaque double border (outer stroke, 1-px
  // channel, inner stroke), interior cleared so text reads clean against
  // the forest. Same border idiom used on the SHADES detail icon and
  // keeper portrait.
  constexpr u8 PLAQUE_W  = 72;
  constexpr u8 PLAQUE_H  = 51;                              // multiple of 3 to play with row stride
  constexpr i16 PLAQUE_X = (fb::WIDTH - PLAQUE_W) / 2;      // 28
  constexpr i16 PLAQUE_Y = 9 + ((55 - (i16)PLAQUE_H) / 2);  // 11 (centered in 9..63)
  fb::clear_rect(PLAQUE_X, PLAQUE_Y, PLAQUE_W, PLAQUE_H);
  fb::stroke_rect(PLAQUE_X, PLAQUE_Y, PLAQUE_W, PLAQUE_H);
  fb::stroke_rect(PLAQUE_X + 2, PLAQUE_Y + 2, (u8)(PLAQUE_W - 4), (u8)(PLAQUE_H - 4));

  // Menu options. PROGMEM table keeps strings out of RAM. OFFERINGS and
  // RECKONING reuse the LIT_* symbols defined at the top of this anon
  // namespace (also used as bare-literal replacements in font::draw_text
  // calls); the unique ones live here.
  static const char MM_OPT_0[] PROGMEM    = "DESCEND";
  static const char MM_OPT_3[] PROGMEM    = "VESTIGIA";
  static const char MM_OPT_4[] PROGMEM    = "GRIMOIRE";
  static const char MM_OPT_5[] PROGMEM    = "FORSAKE";
  static const char* const opts[] PROGMEM = {MM_OPT_0, LIT_OFFERINGS, LIT_RECKONING,
                                             MM_OPT_3, MM_OPT_4,      MM_OPT_5};
  constexpr u8 N                          = 6;
  // Row stride dropped from 9 to 8 to pack 6 rows into the same plaque
  // dimensions the original 5-row layout used. 6*8 - 3 = 45 px tall,
  // leaves 3 px margins top and bottom inside the 51-px plaque.
  constexpr i16 ROW_H  = 8;
  constexpr i16 BASE_Y = PLAQUE_Y + 3;
  constexpr i16 SEL_X  = PLAQUE_X + 6;
  constexpr i16 TEXT_X = PLAQUE_X + 12;
  char buf[16];
  for (u8 i = 0; i < N; ++i) {
    i16 y = BASE_Y + (i16)i * ROW_H;
    if (i == menu_index) font::draw_char(SEL_X, y, '>');
    const char* p = (const char*)pgm_read_ptr(&opts[i]);
    copy_pgm_str(p, buf, sizeof(buf));
    font::draw_text(TEXT_X, y, buf);
  }

  // Light-mode flip: pre-Hell, daylight still reigns.
  fb::invert_all();
}

// ---- Upgrade screen ----------------------------------------------------
SCENE_FN(MAIN_MENU) void draw_upgrade_menu() {
  fb::clear();
  font::draw_text_pgm(2, 1, LIT_OFFERINGS);
  font::draw_text_pgm(50, 1, LIT_SANGUEC);
  draw_sangue(fb::WIDTH, 1, meta.sangue_vessel);
  draw_header_rule();

  // Four rows: VITA / IRA / FURIA / BULLET. Stat rows show level + price;
  // bullet row shows the active tier's name + price for the *next* tier
  // (or FULL when all five are unlocked). The Pilgrim's word "BULLET"
  // sits in his own modern voice while the tier name (STONE / ARROW /
  // HOOK / SHARD / WIND) is what Hell calls it — see docs/design/.
  const u8 levels[3] = {meta.level_hp, meta.level_damage, meta.level_fire_rate};
  // L_VITA and L_IRA include trailing padding spaces for column alignment;
  // they're distinct from LIT_VITA / LIT_IRA. L_FURIA matches LIT_FURIA
  // exactly so it just reuses that symbol.
  static const char L_VITA[] PROGMEM        = "VITA ";
  static const char L_IRA[] PROGMEM         = "IRA  ";
  static const char* const labels[] PROGMEM = {L_VITA, L_IRA, LIT_FURIA};
  constexpr i16 ROW_H                       = 9;   // rule of threes
  constexpr i16 BASE_Y                      = 12;  // rule of threes
  char lbuf[8];
  for (u8 i = 0; i < 3; ++i) {
    i16 y = BASE_Y + (i16)i * ROW_H;
    if (i == upgrade_cursor) font::draw_char(2, y, '>');
    copy_pgm_str((const char*)pgm_read_ptr(&labels[i]), lbuf, sizeof(lbuf));
    font::draw_text(8, y, lbuf);
    font::draw_text_pgm(32, y, LIT_RANK);
    // Rank value is Hell's count (the Pilgrim's standing in this stat),
    // so Roman. Price also Roman now: the player's vessel reads as
    // Roman (see draw_sangue), so an arabic price reads against a
    // Roman wallet — impossible mental math. Same register on both
    // sides.
    draw_roman(64, y, levels[i]);
    if (levels[i] >= MAX_UPGRADE_LEVEL) {
      font::draw_text_pgm(68, y, LIT_FULL);
    } else {
      font::draw_text_pgm(68, y, LIT_PRICEC);
      draw_roman(fb::WIDTH, y, upgrade_cost(levels[i]));
    }
  }
  // BULLET row.
  {
    i16 y = BASE_Y + 3 * ROW_H;
    if (upgrade_cursor == 3) font::draw_char(2, y, '>');
    font::draw_text_pgm(8, y, LIT_BULLET);
    // Active tier's true-name (STONE / ARROW / HOOK / SHARD / WIND).
    char nbuf[8];
    u8 tier = meta.bullet < BULLET_TIER_COUNT ? meta.bullet : 0;
    copy_pgm_str((const char*)pgm_read_ptr(&BULLET_NAMES[tier]), nbuf, sizeof(nbuf));
    font::draw_text(32, y, nbuf);
    if (meta.bullet + 1 >= BULLET_TIER_COUNT) {
      font::draw_text_pgm(68, y, LIT_FULL);
    } else {
      font::draw_text_pgm(68, y, LIT_PRICEC);
      draw_roman(fb::WIDTH, y, bullet_cost((u8)(meta.bullet + 1)));
    }
  }
  draw_footer_pgm(LIT_ACPRESS_ON_BCTURN_BACK);
  // Pre-Hell (main-menu) upgrade: light mode. Mid-run (from guide) stays
  // dark because the pilgrim is still in Hell when he visits the merchant.
  if (!upgrade_from_guide) fb::invert_all();
}

// ---- Stats screen ------------------------------------------------------
//
// Two pages, paged via Left/Right:
//   page 0 — "this Pilgrim": portrait + class + level-name + BURDEN +
//            VITA/IRA/FURIA. Dev-mode A cycles through all 9 (class ×
//            level) combinations until class-select mechanics ship.
//   page 1 — lifetime tally (PILGRIMAGES, SOMMA SHADES, etc.) — the
//            screen as it shipped originally.

// Per-class stat triangles. Indexed by class * 3 + (level - 1).
// Each row is {VITA, IRA, FURIA}. Sums climb cycle 1 → 3 (no value
// exceeds the cycle-1 cap of 32). The triangle expresses the lore:
//
//   PENITENT  — Vita strong, Furia weak  (resists, Hell loads slowly)
//   HERETIC   — Ira strong,  Vita weak   (asserts, fixates first)
//   WRETCHED  — Furia strong, Vita weak  (flees, never quite arrives)
//
// Replace these with real progression-table values once class-select +
// level-up wire up. The shapes here are placeholders chosen to match
// the lore triangle so the screen READS right while debugging.
struct ClassStats {
  u8 vita;
  u8 ira;
  u8 furia;
};
const ClassStats RECKONING_STATS[9] PROGMEM = {
    {12, 6, 3},   // penitent L1
    {20, 8, 4},   // penitent L2
    {30, 10, 5},  // penitent L3
    {3, 6, 12},   // wretched L1
    {4, 8, 20},   // wretched L2
    {5, 10, 30},  // wretched L3
    {4, 12, 6},   // heretic  L1
    {5, 20, 8},   // heretic  L2
    {6, 30, 10},  // heretic  L3
};

// Class-name PROGMEM string-pointer table. Indexed by class id 0..2.
const char* const CLASS_NAMES[3] PROGMEM = {
    LIT_PENITENT,
    LIT_WRETCHED,
    LIT_HERETIC,
};

// Per-class level-name table. Indexed by class * 3 + (level - 1).
// Order matches CLASS_NAMES → PENITENT/WRETCHED/HERETIC × L1/L2/L3.
const char* const CLASS_LEVEL_NAMES[9] PROGMEM = {
    LIT_PILGRIM,  LIT_BEARER, LIT_MANTLE,  // PENITENT — Inf I → VII → XXIII
    LIT_VAGRANT,  LIT_STING,  LIT_WIND,    // WRETCHED — Inf III → III → V
    LIT_APOSTATE, LIT_ZEALOT, LIT_TOMB,    // HERETIC  — Inf X
};

// Per-(class, level) vertical nudge for the Reckoning portrait plaque.
// Same pattern as SHADES_PORTRAIT_NUDGE_DOWN in the bestiary —
// positive values push the sprite DOWN from its bottom-anchored
// position by N pixels, sacrificing the floor anchor in exchange for
// keeping the head/horns visible inside the plaque interior. Indexed
// by class * 3 + (level - 1). Defaults to 0 (strict floor anchor);
// override only the figures whose lit-bottom alignment lands their
// top features above the inner ceiling.
const i8 RECKONING_PORTRAIT_NUDGE_DOWN[9] PROGMEM = {
    // PENITENT (Vita-focused, hood + staff)
    0,
    0,
    0,
    // WRETCHED (low-crouched, spiked hair)
    0,
    0,
    0,
    // HERETIC (tall, halo at L3)
    0,
    0,
    0,
};

// Sprite-id base for the 9 class portraits, indexed by class * 3 + (level - 1).
inline u8 reckoning_portrait_id(u8 class_idx, u8 level_idx) {
  return (u8)(sprites::SP_CLASS_FIRST + class_idx * 3 + level_idx);
}

// World-variant sprite id for the page-0 idle icon. Penitent L1 has
// real idle-strip animation (f0 ↔ f1); the other 8 combos don't yet
// have animation frames baked, so the bob falls back to a static
// world token for them. As more class anims ship, expand this dispatch.
inline u8 reckoning_world_sprite_id(u8 class_idx, u8 level_idx, u8 anim_frame) {
  if (class_idx == 0 && level_idx == 0) {
    return anim_frame ? sprites::PEN_L1_IDLE_F1 : sprites::PEN_L1_IDLE_F0;
  }
  return (u8)(sprites::SP_PENITENT_L1_WORLD + class_idx * 3 + level_idx);
}

// Stats subtree (RECKONING) lives in CORE rather than the MAIN_MENU bank.
// Reason: draw_stats_page_lifetime calls draw_sangue_u32 → libgcc divmod,
// and -mcall-prologues forces all non-leaf functions to rcall the
// shared __prologue_saves__ helper in CORE .text. From inside the
// MAIN_MENU bank the rcall to that helper is at the 13-bit horizon
// (~4 KB byte range) and any layout shift breaks the link. Moving the
// entire stats-page subtree to CORE keeps these libgcc calls
// CORE-internal, where they always reach. ~700 B of CORE growth in
// exchange for ~700 B less in the MAIN_MENU bank — net flash identical
// but no horizon fragility.
void draw_stats_page_pilgrim() {
  // Page 0 is the stats card: HUD-style header (RECKONING + Pilgrim
  // name) + small animated idle icon on the left + class/burden/
  // VITA/IRA/FURIA on the right + footer. The big portrait gets its
  // own page (page 1, draw_stats_page_portrait below); this page is
  // for reading numbers, with a breathing icon to keep the figure
  // present.
  //
  // Read the active save's vestige + burden directly from meta.
  // UNBURDENED (vestige=0) is the pre-class wraith state — all four
  // stats read 0, label is "UNBURDENED", burden row reads "NIHIL".
  // Otherwise: vestige 1=PENITENT/2=WRETCHED/3=HERETIC, burden 1..3.
  const bool is_unburdened = (meta.vestige == storage::VESTIGE_UNBURDENED);
  const u8 cls = is_unburdened ? 0 : (u8)(meta.vestige - 1);  // 0..2 for class table indexing
  const u8 lvl = is_unburdened ? 0 : (u8)(meta.burden - 1);   // 0..2 for L1..L3

  draw_pilgrim_name_header(LIT_RECKONING);

  // Identity rows above the box: class name + burden row. For class
  // states, the level-name (PILGRIM / TOMB / WIND etc.) IS the
  // burden's value — at L3 the soul has *become* the punishment-
  // object. For the unburdened, both rows reflect the zero state.
  if (is_unburdened) {
    font::draw_text_pgm(2, 12, LIT_UNBURDENED);
    font::draw_text_pgm(2, 20, LIT_BURDEN);
    // BURDEN: NIHIL — Hell has not measured him.
    font::draw_text_pgm((i16)fb::WIDTH - 5 * (i16)font::STRIDE, 20, LIT_NIHIL);
  } else {
    font::draw_text_pgm(2, 12, (const char*)pgm_read_ptr(&CLASS_NAMES[cls]));
    const char* bname = (const char*)pgm_read_ptr(&CLASS_LEVEL_NAMES[cls * 3 + lvl]);
    char bbuf[12];
    copy_pgm_str(bname, bbuf, sizeof(bbuf));
    const u8 blen = strlen_(bbuf);
    font::draw_text_pgm(2, 20, LIT_BURDEN);
    font::draw_text((i16)fb::WIDTH - (i16)blen * (i16)font::STRIDE, 20, bbuf);
  }

  // Engraved icon box on the left holding the animated sprite. For
  // class states this is the world-variant token (~12-20 px wide,
  // bobbing once per second). For the unburdened, the idle anim
  // frames (UB_IDLE_F0 ↔ UB_IDLE_F1) bob at the same cadence — the
  // wraith breathes in place. Walk/attack frames are baked but only
  // surface during gameplay (not yet wired to a class-aware state
  // machine; they sit on FX flash queued for later).
  constexpr i16 ICON_X = 2;
  constexpr i16 ICON_Y = 28;
  constexpr u8 ICON_W  = 28;
  constexpr u8 ICON_H  = 28;
  const u8 wid = is_unburdened ? (reckoning_anim_frame ? sprites::UB_IDLE_F1 : sprites::UB_IDLE_F0)
                               : reckoning_world_sprite_id(cls, lvl, reckoning_anim_frame);
  draw_engraved_portrait(ICON_X, ICON_Y, ICON_W, ICON_H, wid, /*nudge=*/0);

  // Stats column to the right of the icon box. Same row stride as the
  // lifetime-tally page (9 px between rows). Labels start past the
  // icon's right edge; values right-anchor to the screen edge.
  // Unburdened reads 0/0/0; class states pull from RECKONING_STATS.
  const u8 v            = is_unburdened ? 0 : pgm_read_byte(&RECKONING_STATS[cls * 3 + lvl].vita);
  const u8 i_           = is_unburdened ? 0 : pgm_read_byte(&RECKONING_STATS[cls * 3 + lvl].ira);
  const u8 f            = is_unburdened ? 0 : pgm_read_byte(&RECKONING_STATS[cls * 3 + lvl].furia);
  constexpr i16 STAT_TX = 36;
  font::draw_text_pgm(STAT_TX, 30, LIT_VITA);
  draw_roman_or_nihil(fb::WIDTH, 30, v);
  font::draw_text_pgm(STAT_TX, 39, LIT_IRA);
  draw_roman_or_nihil(fb::WIDTH, 39, i_);
  font::draw_text_pgm(STAT_TX, 48, LIT_FURIA);
  draw_roman_or_nihil(fb::WIDTH, 48, f);

  draw_footer_pgm(LIT_ACPRESS_ON_BCTURN_BACK);
}

void draw_stats_page_portrait() {
  // Dedicated portrait beat. The big class portrait gets the full 128×64
  // canvas — no header, no footer. Same engraved-plaque idiom as the
  // bestiary's keeper portrait, sized up to use every available pixel.
  // Temporary layout — likely to evolve into a proper class-select
  // ceremony screen once the mechanics are wired.
  //
  // UNBURDENED: full-screen wraith. Class states: per-class portrait
  // with the right NUDGE_DOWN.
  if (meta.vestige == storage::VESTIGE_UNBURDENED) {
    draw_engraved_portrait(0, 0, (u8)fb::WIDTH, (u8)fb::HEIGHT, sprites::SP_UNBURDENED,
                           /*nudge=*/0);
    return;
  }
  const u8 cls   = (u8)(meta.vestige - 1);
  const u8 lvl   = (u8)(meta.burden - 1);
  const u8 pid   = reckoning_portrait_id(cls, lvl);
  const i8 nudge = (i8)pgm_read_byte(&RECKONING_PORTRAIT_NUDGE_DOWN[cls * 3 + lvl]);
  draw_engraved_portrait(0, 0, (u8)fb::WIDTH, (u8)fb::HEIGHT, pid, nudge);
}

void draw_stats_page_lifetime() {
  draw_pilgrim_name_header(LIT_RECKONING);

  // Rule of threes: row ladder at 12, 21, 30, 39, 48 (9-spacing).
  // Tally counts use Roman numerals — Hell tracks pilgrimages in the
  // period notation, not modern arabic. NIHIL stands in for 0.
  font::draw_text_pgm(2, 12, LIT_PILGRIMAGES);
  draw_roman_or_nihil(fb::WIDTH, 12, meta.total_runs);
  font::draw_text_pgm(2, 21, LIT_SOMMA_SHADES);
  draw_roman_or_nihil(fb::WIDTH, 21, meta.total_kills);
  font::draw_text_pgm(2, 30, LIT_SOMMA_KEEPERS);
  draw_roman_or_nihil(fb::WIDTH, 30, (u16)meta.total_keepers_felled);
  font::draw_text_pgm(2, 39, LIT_SOMMA_SANGUE);
  draw_sangue_u32(fb::WIDTH, 39, meta.total_sangue_earned);

  // Deepest descent: best.wave ever cleared, rendered Dante-style as
  // "CIRCLE III, ROUND II". On a brand-new save shows bare "NIHIL".
  font::draw_text_pgm(2, 48, LIT_DEEPEST);
  char d[22];
  if (best.wave > 0) {
    format_descent_long((u16)best.wave, d);
  } else {
    static const char NIL_DESCENT[] PROGMEM = "NIHIL";
    u8 i                                    = 0;
    char c;
    while ((c = (char)pgm_read_byte(&NIL_DESCENT[i])) != 0) {
      d[i++] = c;
    }
    d[i] = 0;
  }
  u8 dlen = strlen_(d);
  i16 dx  = (i16)fb::WIDTH - (i16)dlen * (i16)font::STRIDE;
  font::draw_text(dx, 48, d);

  draw_footer_pgm(LIT_BCTURN_BACK);
}

void draw_stats_screen() {
  fb::clear();
  if (reckoning_page == 0) {
    draw_stats_page_pilgrim();
  } else if (reckoning_page == 1) {
    draw_stats_page_portrait();
  } else {
    draw_stats_page_lifetime();
  }
  // Pre-Hell: light mode (RECKONING is only reachable from MAIN_MENU).
  fb::invert_all();
}

// ---- Bestiary --------------------------------------------------------
// Single entry view: large sprite on the left, name/index on the right.
// LEFT/RIGHT cycles through entries. Unseen entries show ??? + a placeholder
// box instead of the sprite.
// List view: 4 visible rows of [> icon name], scrolls to keep cursor in view.
// Icon is always the top 8px of the beast sprite — for 8-tall minions this is
// the whole thing; for 16-tall bosses it's a "head portrait." Full sprite is
// revealed on the detail page.
// Forward decl — defined further down with the encyclopedia widget block.
void draw_named_list(const char* title_pgm, const char* const* names_pgm, u8 count, u8 cursor,
                     bool show_count = true);

// Bestiary list now reuses the shared encyclopedia widget. Icons used to
// sit beside minion names but were dropped — the detail page shows the
// full sprite (and full stats), and the cleaner list scales to 5 visible
// rows instead of 4.
SCENE_FN(MAIN_MENU) void draw_shades_list() {
  static const char SHADES_TITLE[] PROGMEM = "ROLL OF SHADES";
  draw_named_list(SHADES_TITLE, SHADES_NAMES, SHADES_COUNT, shades_cursor);
}

// Tier label per shades entry. Five tiers:
//   SINNER  — upper-Hell damned (Limbo through Greed)
//   THRALL  — bound damned of mid-Hell (Wrath, Heresy, Violence)
//   FIEND   — Hell's hook-bearing demons + the deepest traitor
//   KEEPER  — circle keepers (Charon through Geryon)
//   EMPEROR — Lucifer alone, frozen at Hell's center
// Indexed by shades idx 0..20 (matches sprite-ID order).
const char TIER_SINNER[] PROGMEM                    = "SINNER";
const char TIER_THRALL[] PROGMEM                    = "THRALL";
const char TIER_FIEND[] PROGMEM                     = "FIEND";
const char TIER_KEEPER[] PROGMEM                    = "KEEPER";
const char TIER_EMPEROR[] PROGMEM                   = "EMPEROR";
const char* const TIER_OF_IDX[SHADES_COUNT] PROGMEM = {
    // 0..5: SINNERS (Wraith, Lust Spirit, Worm, Swine, Jouster, Profligate)
    TIER_SINNER,
    TIER_SINNER,
    TIER_SINNER,
    TIER_SINNER,
    TIER_SINNER,
    TIER_SINNER,
    // 6..8: THRALLS (Brawler, Tomb Shade, Harpy)
    TIER_THRALL,
    TIER_THRALL,
    TIER_THRALL,
    // 9..11: FIENDS (Centaur, Malebranche, Ice Traitor)
    TIER_FIEND,
    TIER_FIEND,
    TIER_FIEND,
    // 12..19: KEEPERS (Charon..Geryon)
    TIER_KEEPER,
    TIER_KEEPER,
    TIER_KEEPER,
    TIER_KEEPER,
    TIER_KEEPER,
    TIER_KEEPER,
    TIER_KEEPER,
    TIER_KEEPER,
    // 20: EMPEROR (Lucifer)
    TIER_EMPEROR,
};

// Detail view: full sprite + name + tier + 3 stats + description.
SCENE_FN(MAIN_MENU) void draw_shades_detail() {
  fb::clear();
  u8 idx       = shades_cursor;
  u8 sprite_id = (u8)(SHADES_FIRST_ID + idx);
  bool is_boss = idx >= SHADES_FIRST_BOSS;

  // Header: shade name (left) + Roman `current DE total` (right).
  // Hell's count, not modern. "DE" is the period-correct manuscript
  // separator (capitulum xxi de xxi); the helper handles spacing
  // around variable-width totals.
  font::draw_text_pgm(2, 1, (const char*)pgm_read_ptr(&SHADES_NAMES[idx]));
  draw_position_of_total(fb::WIDTH, 1, (u16)(idx + 1), SHADES_COUNT);
  draw_header_rule();

  // Sprite area: minions get the inverted-box icon on the left so the
  // text column starts past it. Keepers (bosses) skip the icon entirely
  // here — their portrait is too large for an inline thumbnail; press A
  // on the boss detail to open a dedicated portrait page.
  i16 text_x = 2;
  if (!is_boss) {
    u8 sw                   = sprites::width(sprite_id);
    u8 sh                   = sprites::height(sprite_id);
    constexpr i16 SPR_BOX_X = 4, SPR_BOX_Y = 12;
    constexpr u8 SPR_BOX_W = 28, SPR_BOX_H = 28;
    // Engraved-plaque double border + black interior + sprite silhouette.
    // Same idiom as the boss portrait box, scaled down to the 28x28 icon.
    fb::stroke_rect(SPR_BOX_X, SPR_BOX_Y, SPR_BOX_W, SPR_BOX_H);
    fb::stroke_rect(SPR_BOX_X + 2, SPR_BOX_Y + 2, (u8)(SPR_BOX_W - 4), (u8)(SPR_BOX_H - 4));
    fb::fill_rect(SPR_BOX_X + 3, SPR_BOX_Y + 3, (u8)(SPR_BOX_W - 6), (u8)(SPR_BOX_H - 6));
    i16 sx = SPR_BOX_X + ((i16)SPR_BOX_W - (i16)sw) / 2;
    i16 sy = SPR_BOX_Y + ((i16)SPR_BOX_H - (i16)sh) / 2;
    fb::clear_sprite_progmem(sx, sy, sw, sprites::data(sprite_id));
    text_x = SPR_BOX_X + SPR_BOX_W + 4;
  }

  // Right column: tier + stats. Name lived here at y=12 in an earlier
  // layout, but it's already in the header — duplication confused the
  // page. Tier slides up to y=12, stats compress to 18/24/30, desc to
  // y=39 — gives the description more breathing room above the footer.
  char tier_buf[12];
  copy_pgm_str((const char*)pgm_read_ptr(&TIER_OF_IDX[idx]), tier_buf, sizeof(tier_buf));
  font::draw_text(text_x, 12, tier_buf);

  // Stats. Minions share ENEMY_BASE_*; bosses use per-circle HP.
  u16 hp, dmg, fr;
  if (is_boss) {
    u8 c = (u8)(idx - SHADES_FIRST_BOSS);  // boss index 0..8 = circle 0..8
    hp   = pgm_read_byte(&BOSS_HP_BY_CIRCLE[c]);
    dmg  = BOSS_DAMAGE;
    fr   = BOSS_FIRE_RATE;
  } else {
    hp  = ENEMY_BASE_HP;
    dmg = ENEMY_BASE_DAMAGE;
    fr  = ENEMY_BASE_FIRE_RATE;
  }
  font::draw_text_pgm(text_x, 18, LIT_VITA);
  draw_roman(fb::WIDTH, 18, hp);
  font::draw_text_pgm(text_x, 24, LIT_IRA);
  draw_roman(fb::WIDTH, 24, dmg);
  font::draw_text_pgm(text_x, 30, LIT_FURIA);
  draw_roman(fb::WIDTH, 30, fr);

  // Description: up to ~32 chars. Conditional y to balance each
  // layout's whitespace:
  //   * minion: sprite box ends at y=39, so desc sits at y=44 (4-px gap).
  //   * boss:   no sprite box, so desc rides higher at y=39 — keeps the
  //             rhythm tight against the stats row at y=30 instead of
  //             leaving a 14-px void.
  char desc[SHADES_DESC_LEN];
  shades_desc_to_ram(idx, desc);
  font::draw_text(2, is_boss ? 39 : 44, desc);

  static const char FOOTER_BOSS[] PROGMEM   = "A:VIEW KEEPER  B:TURN BACK";
  static const char FOOTER_MINION[] PROGMEM = "B:TURN BACK";
  char fbuf[28];
  copy_pgm_str(is_boss ? FOOTER_BOSS : FOOTER_MINION, fbuf, sizeof(fbuf));
  font::draw_text(2, fb::HEIGHT - 7, fbuf);
}

// Boss portrait page: full-frame view of a single keeper in a black-
// bordered box with black interior, the keeper punched out as a white
// silhouette. Reached via A on the boss detail page; B returns.
//
// Same inverted-box idiom as the minion icons on the detail page —
// reads as "thou peerest into Hell at the keeper's outline." Vertically
// centered on the page; footer at y=57.
// Per-sprite vertical nudge for the bestiary plaque. Positive = push
// the sprite DOWN from its bottom-aligned position by N pixels. Used
// when an oversized sprite's bottom-aligned position lands the head
// or horn tips above the inner ceiling — a downward nudge sacrifices
// the floor anchor in exchange for keeping the head visible. Default
// 0 = strict floor anchor. Indexed by (id - SHADES_FIRST_ID).
//
// Lucifer (60x58) has antenna horns reaching the very top of his bbox.
// Bottom-aligned, the horn tips clip the inner ceiling; +6 pushes him
// down so the horns fit (legs / floor area gets sacrificed instead).
const i8 SHADES_PORTRAIT_NUDGE_DOWN[sprites::COUNT - SHADES_FIRST_ID] PROGMEM = {
    // sinners
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    // bosses
    0, 0, 0, 0, 0, 0, 0, 0,
    8,  // BOSS_LUCIFER — push down so horns fit at the top
};

SCENE_FN(MAIN_MENU) void draw_shades_portrait() {
  fb::clear();
  const u8 idx       = shades_cursor;
  const u8 sprite_id = (u8)(SHADES_FIRST_ID + idx);
  // Full-screen-width plaque so Lucifer (60w × 58h) fits edge-to-edge
  // of the inner padding. Smaller shades center with whitespace.
  // Footer baseline at y=57 so AVAILABLE_H = 56 leaves the bottom
  // strip free.
  const i8 nudge = (i8)pgm_read_byte(&SHADES_PORTRAIT_NUDGE_DOWN[idx]);
  draw_engraved_portrait(0, 0, (u8)fb::WIDTH, 56, sprite_id, nudge);
  draw_footer_pgm(LIT_BCTURN_BACK);
}

SCENE_FN(MAIN_MENU) void draw_shades() {
  switch (shades_view) {
  case SHADES_PORTRAIT: draw_shades_portrait(); break;
  case SHADES_DETAIL: draw_shades_detail(); break;
  case SHADES_LIST:
  default: draw_shades_list(); break;
  }
  // Pre-Hell: ROLL OF SHADES is only reachable from MAIN_MENU.
  fb::invert_all();
}

// Stack buffer cap for any string copied out of PROGMEM and rendered.
// Hardcoded into copy_pgm_table_entry, so every caller's local `buf` MUST
// be at least this large or copy_pgm_str's terminator-write smashes the
// stack (see CLAUDE.md: "PROGMEM stack-buffer trap"). Defined here, ahead
// of the encyclopedia widgets, because they were the first victims.
constexpr u8 TEXT_LINE_MAX = 40;

// ---- Encyclopedia widget for NUMERALS / LEXICON ------------------------
// Both screens follow the same shape: a header line, a list of names
// pulled from a PROGMEM pointer table, with cursor-driven scroll. Detail
// view shows the entry's name as a header and its body lines below.

// The PROGMEM data tables live further down with the other text content;
// declare just the array pointers here so the draw functions compile.
// PROGMEM attribute MUST match the definition or the compiler emits the
// wrong load instructions on AVR (RAM ld vs flash lpm), reading garbage
// and hanging the CPU.
extern const char* const NUMERALS_NAMES[] PROGMEM;
extern const char* const NUMERALS_VALUES[] PROGMEM;
extern const char* const LEXICON_NAMES[] PROGMEM;
extern const char* const LEXICON_VALUES[] PROGMEM;

// And the helper used by draw_named_list (defined further down with the
// other text-screen plumbing).
void copy_pgm_table_entry(const char* const* table, u8 i, char* out);

// Unified scrollable-list renderer. Used by every list screen with the
// 5-row + cursor + scroll-window pattern.
//
//   title_pgm:   header drawn at y=1 in PROGMEM
//   names_pgm:   PROGMEM array of PROGMEM C-string pointers (length count)
//   values_pgm:  optional second-column PROGMEM strings (nullptr = single
//                column with footer "A:PRESS ON  B:TURN BACK"); when set,
//                values render at x=60 and footer becomes "B:TURN BACK"
//   count:       number of entries
//   cursor:      highlighted index (0..count-1)
//   show_count:  if true, draws "<total>/<cursor+1>" Roman pair top-right.
//                Lookup-list mode (values_pgm != nullptr) always shows it.
//
// Replaces the former draw_named_list + draw_named_lookup_list pair —
// 468 B combined; this consolidates ~85% of the shared body.
SCENE_FN(MAIN_MENU)
void draw_scrollable_list(const char* title_pgm, const char* const* names_pgm,
                          const char* const* values_pgm, u8 count, u8 cursor, bool show_count) {
  fb::clear();
  // Sized for TEXT_LINE_MAX because copy_pgm_table_entry hard-codes that cap.
  // A smaller buffer here is the "hot blood river" stack-smash setup —
  // see CLAUDE.md / .claude/rules-footprint.md.
  char buf[TEXT_LINE_MAX];
  copy_pgm_str(title_pgm, buf, sizeof(buf));
  font::draw_text(2, 1, buf);
  // Lookup-list mode forces show_count; otherwise honor caller flag.
  if (values_pgm || show_count) {
    // `current DE total` right-anchored. Helper handles variable total
    // widths so longer totals (VIII, XIV) don't eat the right padding.
    draw_position_of_total(fb::WIDTH, 1, (u16)(cursor + 1), count);
  }
  draw_header_rule();

  constexpr u8 VISIBLE_ROWS = 5;
  constexpr i16 ROW_H       = 9;   // rule of threes
  constexpr i16 ROW_BASE_Y  = 12;  // rule of threes
  constexpr i16 VAL_X       = 60;  // value column (when in lookup mode)

  // Scroll window: keep cursor within [top, top + VISIBLE_ROWS).
  u8 top = 0;
  if (cursor >= VISIBLE_ROWS) top = (u8)(cursor - (VISIBLE_ROWS - 1));
  if (count > VISIBLE_ROWS && top > (u8)(count - VISIBLE_ROWS)) top = (u8)(count - VISIBLE_ROWS);

  for (u8 r = 0; r < VISIBLE_ROWS; ++r) {
    u8 idx = (u8)(top + r);
    if (idx >= count) break;
    i16 y = ROW_BASE_Y + (i16)r * ROW_H;
    if (idx == cursor) font::draw_char(2, y, '>');
    copy_pgm_table_entry(names_pgm, idx, buf);
    font::draw_text(8, y, buf);
    if (values_pgm) {
      copy_pgm_table_entry(values_pgm, idx, buf);
      font::draw_text(VAL_X, y, buf);
    }
  }
  // Single-column lists support A (open detail), lookup-lists do not.
  font::draw_text(2, fb::HEIGHT - 7, values_pgm ? "B:TURN BACK" : "A:PRESS ON  B:TURN BACK");
}

// Wrappers preserving the prior call-site signatures so no caller needs
// to know about values_pgm. Compiles down to a tail call to the unified
// renderer; LTO usually inlines completely.
SCENE_FN(MAIN_MENU)
void draw_named_list(const char* title_pgm, const char* const* names_pgm, u8 count, u8 cursor,
                     bool show_count) {
  draw_scrollable_list(title_pgm, names_pgm, nullptr, count, cursor, show_count);
}
SCENE_FN(MAIN_MENU)
void draw_named_lookup_list(const char* title_pgm, const char* const* names_pgm,
                            const char* const* values_pgm, u8 count, u8 cursor) {
  draw_scrollable_list(title_pgm, names_pgm, values_pgm, count, cursor, false);
}

// NUMERALS draw entry point.
SCENE_FN(MAIN_MENU) void draw_numerals() {
  static const char NUM_TITLE[] PROGMEM = "NUMERALS";
  draw_named_lookup_list(NUM_TITLE, NUMERALS_NAMES, NUMERALS_VALUES, NUMERALS_COUNT,
                         numerals_cursor);
  fb::invert_all();
}

// LEXICON draw entry point.
SCENE_FN(MAIN_MENU) void draw_lexicon() {
  static const char LEX_TITLE[] PROGMEM = "LEXICON";
  draw_named_lookup_list(LEX_TITLE, LEXICON_NAMES, LEXICON_VALUES, LEXICON_COUNT, lexicon_cursor);
  fb::invert_all();
}

// ---- TEXT (gloss) screen -----------------------------------------------
// Top list: CONTROLS / LEXICON / ABOUT. ABOUT is a grouping, not a body —
// opening it reveals a sub-list with THE DESCENT / THE GUIDE, which in
// turn open body pages. Four body pages total (ids 0..3) survive from the
// old layout so the table lookup stays dense.
//
// All strings live in PROGMEM (flash-only on AVR, rodata on PC) so they
// don't burn RAM. Rendering copies one line at a time to a small stack
// buffer sized for the longest body line. (TEXT_LINE_MAX defined earlier.)

// Top-level list titles for the GRIMOIRE hub. Five rows:
//   COMMANDS — single body page, button reference
//   LORE     — opens the LORE sub-list (DESCENT/GUIDE/SANGUE/VIRTU)
//   NUMERALS — single body page, Roman numeral reference
//   LEXICON  — single body page, Italian/Latin sin-word gloss
//   SHADES   — state-jump to SHADES_SCREEN (the shades lives outside
//              the TEXT state machine but reads as another sub-page)
const char T_TOP_0[] PROGMEM                                 = "COMMANDS";
const char T_TOP_1[] PROGMEM                                 = "LORE";
const char T_TOP_2[] PROGMEM                                 = "NUMERALS";
const char T_TOP_3[] PROGMEM                                 = "LEXICON";
const char T_TOP_4[] PROGMEM                                 = "ROLL OF SHADES";
const char* const TEXT_TOP_TITLES[TEXT_TOP_SECTIONS] PROGMEM = {
    T_TOP_0, T_TOP_1, T_TOP_2, T_TOP_3, T_TOP_4,
};
// Header titles for the two text-screen lists. PROGMEM so draw_named_list
// can copy them through copy_pgm_str like every other title.
const char TEXT_TITLE_TOP[] PROGMEM   = "GRIMOIRE";
const char TEXT_TITLE_ABOUT[] PROGMEM = "LORE";

// LORE sub-list titles. Each row opens a body page (section id = row + 2).
// The beasts live in MAIN_MENU → ROLL OF SHADES; LORE stays text-only.
const char T_ABOUT_0[] PROGMEM                                   = "THE DESCENT";
const char T_ABOUT_1[] PROGMEM                                   = "THE GUIDE";
const char T_ABOUT_2[] PROGMEM                                   = "THE SANGUE";
const char T_ABOUT_3[] PROGMEM                                   = "THE VIRTU";
const char* const TEXT_ABOUT_TITLES[TEXT_ABOUT_SECTIONS] PROGMEM = {
    T_ABOUT_0,
    T_ABOUT_1,
    T_ABOUT_2,
    T_ABOUT_3,
};

// Body page titles, indexed by section id 0..4. Reused from both lists.
// LEXICON (slot 1) is retired but the slot stays so existing section ids
// remain stable; the dispatcher returns an empty body if anyone opens it.
const char T_TITLE_0[] PROGMEM                   = "COMMANDS";
const char T_TITLE_1[] PROGMEM                   = "";
const char T_TITLE_2[] PROGMEM                   = "THE DESCENT";
const char T_TITLE_3[] PROGMEM                   = "THE GUIDE";
const char T_TITLE_4[] PROGMEM                   = "THE SANGUE";
const char T_TITLE_5[] PROGMEM                   = "THE VIRTU";
const char T_TITLE_6[] PROGMEM                   = "NUMERALS";
const char T_TITLE_7[] PROGMEM                   = "LEXICON";
const char* const TEXT_SECTION_TITLES[8] PROGMEM = {
    T_TITLE_0, T_TITLE_1, T_TITLE_2, T_TITLE_3, T_TITLE_4, T_TITLE_5, T_TITLE_6, T_TITLE_7,
};

// CONTROLS: pure buttons table — each row is a key and its verb. The
// auto-fire hint used to live here as a headless row; it's moved to the
// closer of THE DESCENT where it lands as payoff rather than instruction.
// Verbs padded to 7 chars (longest is WAYFARE) so the `~` sangue-drop
// separator aligns vertically across rows. Costs 3 B of PROGMEM padding
// for a much cleaner read.
const char T_CTRL_H0[] PROGMEM                  = "ARROWS";
const char T_CTRL_D0[] PROGMEM                  = "WAYFARE ~ PONDER";
const char T_CTRL_H1[] PROGMEM                  = "A";
const char T_CTRL_D1[] PROGMEM                  = "SHOOT   ~ PRESS ON";
const char T_CTRL_H2[] PROGMEM                  = "B";
const char T_CTRL_D2[] PROGMEM                  = "REPOSE  ~ TURN BACK";
const char* const TEXT_CONTROLS_HEADS[] PROGMEM = {
    T_CTRL_H0,
    T_CTRL_H1,
    T_CTRL_H2,
};
const char* const TEXT_CONTROLS_DESCS[] PROGMEM = {
    T_CTRL_D0,
    T_CTRL_D1,
    T_CTRL_D2,
};

// THE VIRTU: the three faculties of the pilgrim. Two-column body — heading
// at x=2, description at a fixed x further right. Reuses the same renderer
// the old LEXICON used; SANGUE / SHADE / KEEPER dropped (they live in their
// own LORE entries or are taught by the shades).
// VIRTU heads (VITA/IRA/FURIA) reuse the LIT_* symbols at the top of
// this anon namespace; the descs are unique to this screen.
const char T_VIR_D0[] PROGMEM = "FLESH THOU BEAREST";
const char T_VIR_D1[] PROGMEM = "WRATH PER BULLET";
const char T_VIR_D2[] PROGMEM = "SWIFT OF FOOT AND BLOW";
// Parallel arrays: head[i] and desc[i] render on the same row.
const char* const TEXT_VIRTU_HEADS[] PROGMEM = {
    LIT_VITA,
    LIT_IRA,
    LIT_FURIA,
};
const char* const TEXT_VIRTU_DESCS[] PROGMEM = {
    T_VIR_D0,
    T_VIR_D1,
    T_VIR_D2,
};

// THE DESCENT: short structural overview. Blank-row spacing between lines
// (via row_h=10 in the body renderer) gives each sentence its own beat.
const char T_DES_0[] PROGMEM                   = "NINE CIRCLES WAIT BELOW";
const char T_DES_1[] PROGMEM                   = "EACH HOLDS THREE ROUNDS";
const char T_DES_2[] PROGMEM                   = "A KEEPER GUARDS THE LAST";
const char* const TEXT_DESCENT_LINES[] PROGMEM = {
    T_DES_0,
    T_DES_1,
    T_DES_2,
};

// Closer line on THE DESCENT page — centered above the footer. Not a body
// row (those are left-aligned with paragraph padding); the renderer treats
// this string as a special case for section 2 only.
// Trimmed from "THY WRATH SEEKS THE NEAREST SHADE!" — dropping "THE" fits
// the line in 120 px (screen is 128). The archaic omission reads as epic
// register rather than truncation. Closes with `!` for the death-beat.
const char T_DES_CLOSER[] PROGMEM = "THY WRATH FLIES WHERE THOU FACEST";

const char T_GUI_0[] PROGMEM                 = "THE POET COMES AT EACH";
const char T_GUI_1[] PROGMEM                 = "DESCENT";
const char T_GUI_2[] PROGMEM                 = "FOLLOW TO BE HEALED";
const char T_GUI_3[] PROGMEM                 = "OFFER TO GROW";
const char T_GUI_4[] PROGMEM                 = "DRINK TO TEMPT FATE";
const char* const TEXT_GUIDE_LINES[] PROGMEM = {
    T_GUI_0, T_GUI_1, T_GUI_2, T_GUI_3, T_GUI_4,
};

// SANGUE: blood of the slain. The HUD tracks it; the Guide bargains for
// it; this entry tells the pilgrim where it comes from. Phlegethon (Inf.
// XII) is Dante's river of boiling blood — the literal source of sangue.
const char T_SAN_0[] PROGMEM                  = "PHLEGETHON RUNS RED BELOW";
const char T_SAN_1[] PROGMEM                  = "EACH SHADE THOU FELLEDST";
const char T_SAN_2[] PROGMEM                  = "LEAVES A DROP BEHIND";
const char T_SAN_3[] PROGMEM                  = "GATHER  OFFER  RISE";
const char* const TEXT_SANGUE_LINES[] PROGMEM = {
    T_SAN_0,
    T_SAN_1,
    T_SAN_2,
    T_SAN_3,
};

// NUMERALS: Roman symbols, 7 entries. List names are the bare symbol;
// detail body shows value + a subtractive-notation example so the player
// learns IX = 9 etc.  (NUMERALS_COUNT lives in the forward-decl block above.)
const char NUM_NAME_I[] PROGMEM                          = "I";
const char NUM_NAME_V[] PROGMEM                          = "V";
const char NUM_NAME_X[] PROGMEM                          = "X";
const char NUM_NAME_L[] PROGMEM                          = "L";
const char NUM_NAME_C[] PROGMEM                          = "C";
const char NUM_NAME_D[] PROGMEM                          = "D";
const char NUM_NAME_M[] PROGMEM                          = "M";
const char* const NUMERALS_NAMES[NUMERALS_COUNT] PROGMEM = {
    NUM_NAME_I, NUM_NAME_V, NUM_NAME_X, NUM_NAME_L, NUM_NAME_C, NUM_NAME_D, NUM_NAME_M,
};

// Per-numeral lookup value rendered alongside the symbol. Lite mode:
// no detail page, just `name  value` per row.
const char NUM_VAL_I[] PROGMEM                            = "1";
const char NUM_VAL_V[] PROGMEM                            = "5";
const char NUM_VAL_X[] PROGMEM                            = "10";
const char NUM_VAL_L[] PROGMEM                            = "50";
const char NUM_VAL_C[] PROGMEM                            = "100";
const char NUM_VAL_D[] PROGMEM                            = "500";
const char NUM_VAL_M[] PROGMEM                            = "1000";
const char* const NUMERALS_VALUES[NUMERALS_COUNT] PROGMEM = {
    NUM_VAL_I, NUM_VAL_V, NUM_VAL_X, NUM_VAL_L, NUM_VAL_C, NUM_VAL_D, NUM_VAL_M,
};

// LEXICON: Italian/Latin sin-words. Encyclopedia idiom — list of the
// words alphabetically; press A on one to read its detail (gloss + a
// short Dante-rooted note).  (LEXICON_COUNT lives in the forward-decl block.)
// Lexicon names: FURIA/IRA/VITA reuse the LIT_* symbols defined at the
// top of this anon namespace; the others are unique to LEXICON. Sorted
// alphabetically.
const char LEX_NAME_DE[] PROGMEM                       = "DE";
const char LEX_NAME_2[] PROGMEM                        = "PHLEGETHON";
const char LEX_NAME_3[] PROGMEM                        = "SANGUE";
const char LEX_NAME_4[] PROGMEM                        = "SOMMA";
const char LEX_NAME_5[] PROGMEM                        = "VIRTU";
const char* const LEXICON_NAMES[LEXICON_COUNT] PROGMEM = {
    LEX_NAME_DE, LIT_FURIA, LIT_IRA, LEX_NAME_2, LEX_NAME_3, LEX_NAME_4, LEX_NAME_5, LIT_VITA,
};

// Per-word gloss rendered alongside the lexicon entry. Lite mode: bare
// gloss, no fuller note.
const char LEX_VAL_DE[] PROGMEM                         = "OF";
const char LEX_VAL_FUR[] PROGMEM                        = "FURY";
const char LEX_VAL_IRA[] PROGMEM                        = "WRATH";
const char LEX_VAL_PHL[] PROGMEM                        = "HOT BLOOD RIVER";
const char LEX_VAL_SAN[] PROGMEM                        = "BLOOD";
const char LEX_VAL_SOM[] PROGMEM                        = "SUM";
const char LEX_VAL_VIR[] PROGMEM                        = "POWER  WORTH";
const char LEX_VAL_VIT[] PROGMEM                        = "LIFE";
const char* const LEXICON_VALUES[LEXICON_COUNT] PROGMEM = {
    LEX_VAL_DE,  LEX_VAL_FUR, LEX_VAL_IRA, LEX_VAL_PHL,
    LEX_VAL_SAN, LEX_VAL_SOM, LEX_VAL_VIR, LEX_VAL_VIT,
};

// Section-body table lookup. Returns a PROGMEM pointer to the line-pointer
// table and the count. The caller must pgm_read_word() into that table to
// get each line's PROGMEM pointer, then copy the line out via memcpy_P.
// text_body intentionally untagged: 50 B dispatcher whose single call
// site benefits from inlining; SCENE_FN's noinline would cost more
// than the function body. The text-screen call graph still lands in
// .scene.MAIN_MENU because the callers are tagged.
void text_body(u8 s, const char* const*& out_table, u8& out_n) {
  switch (s) {
  case 0:
  case 1:
    // CONTROLS and LEXICON are rendered by the two-column renderer
    // (draw_text_two_column_body) which reads from HEAD/DESC parallel
    // tables directly, bypassing text_body(). Keep these arms empty so a
    // stray call surfaces as a blank page rather than garbage.
    out_table = nullptr;
    out_n     = 0;
    break;
  case 2:
    out_table = TEXT_DESCENT_LINES;
    out_n     = (u8)(sizeof(TEXT_DESCENT_LINES) / sizeof(TEXT_DESCENT_LINES[0]));
    break;
  case 3:
    out_table = TEXT_GUIDE_LINES;
    out_n     = (u8)(sizeof(TEXT_GUIDE_LINES) / sizeof(TEXT_GUIDE_LINES[0]));
    break;
  case 4:
    out_table = TEXT_SANGUE_LINES;
    out_n     = (u8)(sizeof(TEXT_SANGUE_LINES) / sizeof(TEXT_SANGUE_LINES[0]));
    break;
  // NUMERALS and LEXICON section ids are retained for the title lookup
  // (TEXT_SECTION_TITLES[6] and [7]) but neither has line tables anymore —
  // they're served by their own NUMERALS_SCREEN / LEXICON_SCREEN states
  // with the encyclopedia list+detail idiom.
  default:
    out_table = nullptr;
    out_n     = 0;
    break;
  }
}

// Load title of body section `s` (0..3) into `out` (cap == TEXT_LINE_MAX).
SCENE_FN(MAIN_MENU) void text_title_to_ram(u8 s, char* out) {
  const char* p = (const char*)pgm_read_ptr(&TEXT_SECTION_TITLES[s]);
  copy_pgm_str(p, out, TEXT_LINE_MAX);
}

// Load body line `i` of section `s` into `out`.
SCENE_FN(MAIN_MENU) void text_line_to_ram(u8 s, u8 i, char* out) {
  const char* const* table;
  u8 n;
  text_body(s, table, n);
  if (!table || i >= n) {
    out[0] = 0;
    return;
  }
  const char* p = (const char*)pgm_read_ptr(&table[i]);
  copy_pgm_str(p, out, TEXT_LINE_MAX);
}

// Copy a PROGMEM string pointer read from a PROGMEM table slot.
void copy_pgm_table_entry(const char* const* table, u8 i, char* out) {
  const char* p = (const char*)pgm_read_ptr(&table[i]);
  copy_pgm_str(p, out, TEXT_LINE_MAX);
}

// Both text-screen lists used to be near-identical hand-rolled drawers.
// Consolidated through draw_named_list with show_count=false so the
// header doesn't sprout a count display these screens never had.
SCENE_FN(MAIN_MENU) void draw_text_top_list() {
  draw_named_list(TEXT_TITLE_TOP, TEXT_TOP_TITLES, TEXT_TOP_SECTIONS, text_cursor, false);
}

SCENE_FN(MAIN_MENU) void draw_text_about_list() {
  draw_named_list(TEXT_TITLE_ABOUT, TEXT_ABOUT_TITLES, TEXT_ABOUT_SECTIONS, text_cursor, false);
}

// Default footer strings used on TEXT body pages. Overridden when the
// tutorial slideshow reuses these renderers (slide 0 uses "A:PRESS ON",
// later slides use "A:PRESS ON  B:TURN BACK").
const char TEXT_FOOTER_BACK[] PROGMEM = "B:TURN BACK";
const char TUT_FOOTER_FIRST[] PROGMEM = "A:PRESS ON";
const char TUT_FOOTER_MID[] PROGMEM   = "A:PRESS ON  B:TURN BACK";
const char TUT_FOOTER_LAST[] PROGMEM  = "A:PRESS ON  B:TURN BACK";

// Two-column body renderer. Used by CONTROLS (s=0) and LEXICON (s=1). Heads
// column starts at x=2, descriptions at DESC_X (32 px) so the right column
// lines up vertically regardless of head length. `footer_pgm` may be null
// to skip the footer; otherwise copied + drawn at the bottom of the page.
// Common opening for the two text-body screens — clear, draw the
// VESTIGIA list: phase-1 stub. Renders the slot rows as text labels
// only; the class-portrait dispatch and load/save/overwrite UI come
// in phase 2. Slot 0's row is "UNBURDENED" by doctrine; the others
// will read their occupant's class+burden from the actual vestige
// record once vestigia::init() is wired (the wood UI shipped first
// to keep the integration test loop tight).
// Class+burden display name for an occupied vestige row. Returns into
// the caller's buffer (>=12 bytes); reads from PROGMEM. Empty slot
// label is handled separately. Mirrors the level-name doctrine in
// docs/design/ (PILGRIM/BEARER/MANTLE for Penitent, etc.).
SCENE_FN(MAIN_MENU) void vestigia_row_label(u8 vestige, u8 burden, char* out) {
  // burden is 1..3 → indexes 0..2 in the per-class arrays. burden==0
  // means unburdened (no level installed yet).
  if (vestige == storage::VESTIGE_UNBURDENED || burden == 0) {
    static const char S[] PROGMEM = "UNBURDENED";
    copy_pgm_str(S, out, 12);
    return;
  }
  // Per-class L1/L2/L3 names. Match what RECKONING shows.
  static const char PEN_L1[] PROGMEM = "PILGRIM";
  static const char PEN_L2[] PROGMEM = "BEARER";
  static const char PEN_L3[] PROGMEM = "MANTLE";
  static const char WRE_L1[] PROGMEM = "VAGRANT";
  static const char WRE_L2[] PROGMEM = "STING";
  static const char WRE_L3[] PROGMEM = "WIND";
  static const char HER_L1[] PROGMEM = "APOSTATE";
  static const char HER_L2[] PROGMEM = "ZEALOT";
  static const char HER_L3[] PROGMEM = "TOMB";
  const char* tbl[3][3]              = {
      {PEN_L1, PEN_L2, PEN_L3},
      {WRE_L1, WRE_L2, WRE_L3},
      {HER_L1, HER_L2, HER_L3},
  };
  const u8 cls = (u8)(vestige - 1);  // 0..2
  const u8 lvl = (u8)(burden - 1);   // 0..2
  if (cls > 2 || lvl > 2) {
    static const char S[] PROGMEM = "?";
    copy_pgm_str(S, out, 12);
    return;
  }
  copy_pgm_str(tbl[cls][lvl], out, 12);
}

// Sprite-id of the world-token icon to render next to a vestige row.
// Picks f0/f1 based on `anim_frame` so rows breathe in step with
// RECKONING. UNBURDENED uses UB_IDLE_F0/F1; PEN_L1 has matching idle
// frames; the other class+level combos don't yet have animation
// frames baked, so they render the static world token regardless of
// anim_frame (matches reckoning_world_sprite_id's fallback).
SCENE_FN(MAIN_MENU) u8 vestigia_row_sprite_id(u8 vestige, u8 burden, u8 anim_frame) {
  if (vestige == storage::VESTIGE_UNBURDENED || burden == 0) {
    return anim_frame ? sprites::UB_IDLE_F1 : sprites::UB_IDLE_F0;
  }
  const u8 cls = (u8)(vestige - 1);
  const u8 lvl = (u8)(burden - 1);
  if (cls == 0 && lvl == 0) {
    return anim_frame ? sprites::PEN_L1_IDLE_F1 : sprites::PEN_L1_IDLE_F0;
  }
  return (u8)(sprites::SP_PENITENT_L1_WORLD + cls * 3 + lvl);
}

// VESTIGIA popup option strings + per-mode option arrays. PROGMEM-
// resident; draw_menu_pgm reads them through pgm_read_word. See
// vestigia_popup state-var comment for mode map.
static const char VP_LOAD[] PROGMEM              = "LOAD";
static const char VP_SAVE[] PROGMEM              = "SAVE";
static const char VP_OVERWRITE[] PROGMEM         = "OVERWRITE";
static const char VP_CANCEL[] PROGMEM            = "CANCEL";
static const char* const VP_OPTS_MODE1[] PROGMEM = {VP_LOAD, VP_CANCEL};
static const char* const VP_OPTS_MODE2[] PROGMEM = {VP_SAVE, VP_CANCEL};
static const char* const VP_OPTS_MODE3[] PROGMEM = {VP_LOAD, VP_OVERWRITE, VP_CANCEL};

// Render the popup overlay. Reuses draw_menu_pgm for layout
// consistency with PAUSE_CONFIRM. Empty title — popup options are
// self-explanatory and a title would push the box too tall.
//
// .hightext placement keeps this near the bank base; otherwise the
// bank-side draw_vestigia_screen rcall to it could overflow.
__attribute__((section(".hightext"))) void draw_vestigia_popup() {
  static const char EMPTY_TITLE[] PROGMEM = "";
  switch (vestigia_popup) {
  case 1: draw_menu_pgm(EMPTY_TITLE, VP_OPTS_MODE1, 2, vestigia_popup_cursor, 9); break;
  case 2: draw_menu_pgm(EMPTY_TITLE, VP_OPTS_MODE2, 2, vestigia_popup_cursor, 9); break;
  case 3: draw_menu_pgm(EMPTY_TITLE, VP_OPTS_MODE3, 3, vestigia_popup_cursor, 9); break;
  default: break;
  }
}

SCENE_FN(MAIN_MENU) void draw_vestigia_screen() {
  fb::clear();
  static const char H[] PROGMEM = "VESTIGIA";
  char buf[16];
  copy_pgm_str(H, buf, sizeof(buf));
  font::draw_text(2, 1, buf);
  draw_header_rule();

  // Layout: 18-px rows so each row can hold a 16-px-tall world-token
  // icon (UB_IDLE_F0 = 16x20, PEN_L1_IDLE_F0 = 12x16). Three rows fit
  // in the body (54 px); list scrolls when the cursor moves past row
  // VISIBLE-1.
  constexpr i16 BASE_Y = 10;
  constexpr i16 ROW_H  = 18;
  constexpr u8 N       = vestigia::SLOT_COUNT;
  constexpr u8 VISIBLE = 3;
  // Scroll the window so the cursor is always inside it.
  u8 first = (vestigia_cursor < VISIBLE) ? 0 : (u8)(vestigia_cursor - VISIBLE + 1);
  if (first + VISIBLE > N) first = (N > VISIBLE) ? (u8)(N - VISIBLE) : 0;

  for (u8 i = 0; i < VISIBLE && (first + i) < N; ++i) {
    const u8 slot = (u8)(first + i);
    const i16 y   = BASE_Y + (i16)i * ROW_H;

    // Cursor + slot number column (left edge).
    if (slot == vestigia_cursor) font::draw_char(2, y + 4, '>');
    char num[2] = {(char)('0' + slot), 0};
    font::draw_text(10, y + 4, num);

    // Phase 3: read occupied state + class/burden from vestigia's
    // cached slot metadata. The cache is refreshed on every successful
    // write, so what we render mirrors what's actually in FX flash.
    // Slot 0 is the AUTOSAVE row — always occupied (bootstrap_fresh
    // wrote it on first boot), label forced to "AUTOSAVE" regardless
    // of the stored class. The icon still reflects whatever class+
    // burden the autosave holds, so the player sees their current
    // shape on the autosave row.
    const bool occupied = vestigia::slot_occupied(slot);
    if (!occupied) {
      static const char EMPTY[] PROGMEM = "(empty)";
      char b[10];
      copy_pgm_str(EMPTY, b, sizeof(b));
      font::draw_text(32, y + 4, b);
      continue;
    }
    const u8 vestige_id = vestigia::slot_vestige(slot);
    const u8 burden_id  = vestigia::slot_burden(slot);

    // World-token icon, aligned to the row's vertical center. Only
    // the cursor-highlighted row animates (f0 ↔ f1 idle bob); other
    // rows render the static f0 frame. Keeps the screen calmer and
    // makes the cursor row visibly "alive."
    const u8 anim_frame = (slot == vestigia_cursor) ? reckoning_anim_frame : 0;
    const u8 sid        = vestigia_row_sprite_id(vestige_id, burden_id, anim_frame);
    const u8 sw         = sprites::width(sid);
    const u8 sh         = sprites::height(sid);
    const u32 fx        = sprites::fx_offset(sid);
    if (fx != sprites::FX_OFFSET_NONE) {
      const i16 ix = 18;
      const i16 iy = y + ((i16)ROW_H - (i16)sh) / 2;
      draw_fx_sprite(ix, iy, sw, sh, fx);
    }

    // Label: "AUTOSAVE" for slot 0, class+burden name for slots 1..7.
    char lbl[12];
    if (slot == vestigia::SLOT_UNBURDENED) {
      static const char AUTO[] PROGMEM = "AUTOSAVE";
      copy_pgm_str(AUTO, lbl, sizeof(lbl));
    } else {
      vestigia_row_label(vestige_id, burden_id, lbl);
    }
    font::draw_text(36, y + 4, lbl);
  }

  // Popup overlay (LOAD / SAVE / OVERWRITE / CANCEL) on top of the
  // slot list. Drawn last so it sits above the rows.
  if (vestigia_popup != 0) {
    draw_vestigia_popup();
  }
}

// section's title (loaded from PROGMEM into the caller-owned buf so
// no extra stack), and the manuscript header rule. Factored from two
// inline call sites that had identical 5-line headers 2026-04-27.
SCENE_FN(MAIN_MENU) void draw_text_screen_header(u8 s, char* buf) {
  fb::clear();
  text_title_to_ram(s, buf);
  font::draw_text(2, 1, buf);
  draw_header_rule();
}

// Common closing — optional footer from PGM. footer_pgm == nullptr
// is a no-op. Reuses the caller's buf (the one that held the title)
// so we don't allocate a second TEXT_LINE_MAX-sized stack frame.
SCENE_FN(MAIN_MENU) void draw_text_screen_footer(const char* footer_pgm, char* buf) {
  if (!footer_pgm) return;
  copy_pgm_str(footer_pgm, buf, TEXT_LINE_MAX);
  font::draw_text(2, fb::HEIGHT - 7, buf);
}

SCENE_FN(MAIN_MENU) void draw_text_two_column_body(u8 s, const char* footer_pgm) {
  char buf[TEXT_LINE_MAX];
  draw_text_screen_header(s, buf);

  // Two-column body: section 0 = COMMANDS (button/verb table), section 5 =
  // THE VIRTU (stat name/description table). These are the only two
  // sections that use this renderer.
  const char* const* heads = (s == 0) ? TEXT_CONTROLS_HEADS : TEXT_VIRTU_HEADS;
  const char* const* descs = (s == 0) ? TEXT_CONTROLS_DESCS : TEXT_VIRTU_DESCS;
  const u8 n = (s == 0) ? (u8)(sizeof(TEXT_CONTROLS_HEADS) / sizeof(TEXT_CONTROLS_HEADS[0]))
                        : (u8)(sizeof(TEXT_VIRTU_HEADS) / sizeof(TEXT_VIRTU_HEADS[0]));

  constexpr i16 ROW_H  = 9;  // rule of threes
  constexpr i16 DESC_X = 32;
  // VIRTU (section 5) gets an intro line above the table; COMMANDS (s=0)
  // dives straight in. Push rows down by one row when the intro is shown.
  i16 base_y = 12;
  if (s == 5) {
    static const char VIR_INTRO[] PROGMEM = "THUS IS THE PILGRIM ARMED:";
    copy_pgm_str(VIR_INTRO, buf, TEXT_LINE_MAX);
    font::draw_text(2, 12, buf);
    base_y = 21;
  }
  for (u8 i = 0; i < n; ++i) {
    i16 y = base_y + (i16)i * ROW_H;
    copy_pgm_table_entry(heads, i, buf);
    font::draw_text(2, y, buf);
    copy_pgm_table_entry(descs, i, buf);
    font::draw_text(DESC_X, y, buf);
  }
  draw_text_screen_footer(footer_pgm, buf);
}

// Generic body renderer for DESCENT / GUIDE. DESCENT (s=2) gets extra
// vertical gap between lines ("paragraph padding") so each sentence reads
// as its own beat; GUIDE keeps the compact row height. DESCENT also prints
// a centered closer ("thy wrath...") below the body for dramatic emphasis.
// `footer_pgm` may be null.
SCENE_FN(MAIN_MENU) void draw_text_body_generic(u8 s, const char* footer_pgm) {
  char buf[TEXT_LINE_MAX];
  draw_text_screen_header(s, buf);

  const char* const* table;
  u8 n;
  text_body(s, table, n);
  // Rule of threes: every body uses 9-px row height (3*3).
  constexpr i16 row_h  = 9;
  constexpr i16 BASE_Y = 12;
  for (u8 i = 0; i < n; ++i) {
    text_line_to_ram(s, i, buf);
    font::draw_text(2, BASE_Y + (i16)i * row_h, buf);
  }

  // DESCENT closer: left-aligned beat one row below the last body line,
  // so it reads as a 4th row of the same poem (row_h=9, 3 rows -> y=39).
  if (s == 2) {
    copy_pgm_str(T_DES_CLOSER, buf, TEXT_LINE_MAX);
    font::draw_text(2, 39, buf);
  }

  draw_text_screen_footer(footer_pgm, buf);
}

// Renders a body page given its section id. Used by both the TEXT screen
// and the TUTORIAL slideshow; footer varies per caller. Sections 0
// (COMMANDS) and 5 (THE VIRTU) use the two-column renderer; all other
// sections use the line-list renderer.
SCENE_FN(MAIN_MENU) void draw_text_body(u8 s, const char* footer_pgm) {
  if (s == 0 || s == 5)
    draw_text_two_column_body(s, footer_pgm);
  else
    draw_text_body_generic(s, footer_pgm);
}

SCENE_FN(MAIN_MENU) void draw_text_screen() {
  if (text_section == TEXT_SECTION_LIST)
    draw_text_top_list();
  else if (text_section == TEXT_SECTION_ABOUT)
    draw_text_about_list();
  else
    draw_text_body(text_section, TEXT_FOOTER_BACK);
  // Pre-Hell: light mode (Grimoire is reachable only from MAIN_MENU now).
  fb::invert_all();
}

// ---- Merchant (the Guide's pre-boss interlude) -----------------------
// Full-screen dialog. Top line is the Guide's invocation; the three option
// lines beneath it double as the selectable menu (the ">" selector + the
// poem line IS the option — no redundant "RELIC / OFFERINGS / CHALICE"
// label). Hidden options (RELIC when unaffordable or at full HP) drop off
// the screen entirely.
SCENE_FN(MAIN_MENU) void draw_guide() {
  fb::clear();

  GuideAction actions[3];
  u8 n = guide_visible_actions(actions);
  if (menu_index >= n) menu_index = 0;

  // Pilgrim portrait on the right side of the screen — narrative
  // beat asset showing who the Guide is addressing. When class-select
  // ships, this swaps via meta.class * 3 + meta.level. For now, hard-
  // wired to Penitent L1 (which aliases the touched-up idle f0 frame
  // via fx_offset). Sprite reads from FX flash; portrait dims pulled
  // from the sprite-id table so the right-anchor math stays in sync
  // with whatever bytes Penitent L1 currently aliases.
  {
    const u8 pid = sprites::SP_PENITENT_L1;
    const u8 pw  = sprites::width(pid);
    const u8 ph  = sprites::height(pid);
    const u32 fx = sprites::fx_offset(pid);
    draw_fx_sprite(fb::WIDTH - 2 - (i16)pw, 18, pw, ph, fx);
  }

  // Preamble / Guide's address.
  font::draw_text_pgm(2, 2, LIT_THE_ROAD_AHEAD_IS_DARKC);

  constexpr i16 ROW_H  = 9;   // rule of threes
  constexpr i16 BASE_Y = 18;  // rule of threes
  for (u8 i = 0; i < n; ++i) {
    i16 y = BASE_Y + (i16)i * ROW_H;
    if (i == menu_index) font::draw_char(2, y, '>');
    const char* line = nullptr;
    switch (actions[i]) {
    // Poem lines. Each is imperative + consequence, Dante-voiced:
    //   FOLLOW, AND TAKE HEART  — Canto I.112 (Virgil's own offer to lead)
    //   OFFER, AND BE UNMADE    — Canto VI.42 (Ciacco: fatto/disfatto)
    //   DRINK, AND DARE FATES   — Canto XII (Phlegethon, river of blood)
    case GA_RELIC: line = "FOLLOW, AND TAKE HEART"; break;
    case GA_OFFERINGS: line = "OFFER, AND BE UNMADE"; break;
    case GA_CHALICE: line = "DRINK, AND DARE FATES"; break;
    }
    font::draw_text(8, y, line);
  }

  // Footer: sangue + vita so the player can read context without leaving,
  // plus the A:PRESS ON prompt matching the Guide's imperative voice.
  draw_footer_pgm(LIT_ACPRESS_ON);
  font::draw_text_pgm(30, fb::HEIGHT - 7, LIT_SANGUEC);
  draw_sangue(80, fb::HEIGHT - 7, meta.sangue_vessel);
  if (player) {
    font::draw_text_pgm(82, fb::HEIGHT - 7, LIT_VITAC);
    draw_roman(fb::WIDTH, fb::HEIGHT - 7, player->hp);
  }
}

// Track what the framebuffer was last rendered for, so static screens
// (TITLE/PAUSED/SECOND_DEATH) can skip the redraw + flush when nothing the
// user can see has changed since last frame. PLAYING is never cached
// (the world is animated).
//
// Per the "Per-screen dirty caching" rule in CLAUDE.md: each screen with
// a cursor or sub-state owns ONE cache var per piece of state. Never
// bit-pack multiple screens' state into a shared proxy — that scheme
// shipped once, collided across screens, and caused silent black-screen
// bugs. Named vars are categorically collision-proof.
namespace {
u8 last_drawn_state        = 0xFF;  // sentinel: forces first draw
u8 last_press_a_phase      = 0xFF;  // TITLE blinking prompt phase
u8 last_main_menu_index    = 0xFF;
u8 last_upgrade_cursor     = 0xFF;
u8 last_shades_view        = 0xFF;
u8 last_shades_cursor      = 0xFF;
u8 last_numerals_cursor    = 0xFF;
u8 last_lexicon_cursor     = 0xFF;
u8 last_text_section       = 0xFF;
u8 last_text_cursor        = 0xFF;
u8 last_tutorial_slide     = 0xFF;
u8 last_pause_index        = 0xFF;
u8 last_pause_confirming   = 0xFF;
u8 last_second_death_index = 0xFF;
u8 last_guide_menu_index   = 0xFF;
u8 last_name_cursor        = 0xFF;
u8 last_name_blink_phase   = 0xFF;
u8 last_vestigia_cursor    = 0xFF;
// Combined popup state for dirty cache: low 4 bits = popup mode,
// high 4 bits = popup cursor. One byte avoids two .bss slots.
u8 last_vestigia_popup_state = 0xFF;
u8 last_reckoning_page       = 0xFF;
u8 last_reckoning_anim_frame = 0xFF;
}  // namespace

// ---------------------------------------------------------------- scenes ---
// Per-scene draw bodies. Each owns the dirty-check + dispatch for its
// bucket's states. Returns true if the framebuffer changed (caller
// flushes); false if the dirty cache hit and we can skip the SPI flush.
//
// The `last_*` cache vars are file-scope and shared — each scene-draw
// reads + writes only the ones relevant to its states.

SCENE_ENTRY(TITLE) bool draw_title_scene() {
  const u8 press_a_phase = (u8)((clock::frame_count >> 5) & 1);
  bool dirty             = (state != last_drawn_state) || (press_a_phase != last_press_a_phase);
  if (!dirty) return false;
  last_drawn_state   = state;
  last_press_a_phase = press_a_phase;
  draw_title();
  return true;
}

SCENE_ENTRY(GATE) bool draw_gate_scene() {
  // CIRCLE_CARD: state-only dirty (held for ~1.5 s).
  // GATE_CARD: always dirty (animated).
  bool dirty;
  if (state == CIRCLE_CARD) {
    dirty = (state != last_drawn_state);
  } else {
    dirty = true;  // GATE_CARD
  }
  if (!dirty) return false;
  last_drawn_state = state;
  switch (state) {
  case GATE_CARD: draw_gate_card(); break;
  case CIRCLE_CARD: draw_circle_card(); break;
  default: break;
  }
  return true;
}

SCENE_ENTRY(MAIN_MENU) bool draw_main_menu_scene() {
  const u8 name_blink_phase = (u8)((clock::frame_count >> 4) & 1);
  bool dirty                = false;
  switch (state) {
  case MAIN_MENU:
    dirty = (state != last_drawn_state) || (menu_index != last_main_menu_index);
    break;
  case UPGRADE_MENU:
    dirty = (state != last_drawn_state) || (upgrade_cursor != last_upgrade_cursor);
    break;
  case STATS_SCREEN:
    dirty = (state != last_drawn_state) || (reckoning_page != last_reckoning_page) ||
            (reckoning_anim_frame != last_reckoning_anim_frame);
    break;
  case SHADES_SCREEN:
    dirty = (state != last_drawn_state) || (shades_view != last_shades_view) ||
            (shades_cursor != last_shades_cursor);
    break;
  case NUMERALS_SCREEN:
    dirty = (state != last_drawn_state) || (numerals_cursor != last_numerals_cursor);
    break;
  case LEXICON_SCREEN:
    dirty = (state != last_drawn_state) || (lexicon_cursor != last_lexicon_cursor);
    break;
  case TEXT_SCREEN:
    dirty = (state != last_drawn_state) || (text_section != last_text_section) ||
            (text_cursor != last_text_cursor);
    break;
  case TUTORIAL:
    dirty = (state != last_drawn_state) || (tutorial_slide != last_tutorial_slide);
    break;
  case GUIDE_SCREEN:
    dirty = (state != last_drawn_state) || (menu_index != last_guide_menu_index);
    break;
  case NAME_ENTRY:
    dirty = (state != last_drawn_state) || (name_cursor != last_name_cursor) ||
            (name_blink_phase != last_name_blink_phase);
    break;
  case VESTIGIA_SCREEN: {
    const u8 popup_state = (u8)(vestigia_popup | (vestigia_popup_cursor << 4));
    dirty = (state != last_drawn_state) || (vestigia_cursor != last_vestigia_cursor) ||
            (reckoning_anim_frame != last_reckoning_anim_frame) ||
            (popup_state != last_vestigia_popup_state);
    break;
  }
  default: dirty = true; break;
  }
  if (!dirty) return false;

  last_drawn_state          = state;
  last_main_menu_index      = menu_index;
  last_upgrade_cursor       = upgrade_cursor;
  last_shades_view          = shades_view;
  last_shades_cursor        = shades_cursor;
  last_numerals_cursor      = numerals_cursor;
  last_lexicon_cursor       = lexicon_cursor;
  last_text_section         = text_section;
  last_text_cursor          = text_cursor;
  last_tutorial_slide       = tutorial_slide;
  last_guide_menu_index     = menu_index;
  last_name_cursor          = name_cursor;
  last_name_blink_phase     = name_blink_phase;
  last_vestigia_cursor      = vestigia_cursor;
  last_vestigia_popup_state = (u8)(vestigia_popup | (vestigia_popup_cursor << 4));
  last_reckoning_page       = reckoning_page;
  last_reckoning_anim_frame = reckoning_anim_frame;

  switch (state) {
  case NAME_ENTRY: draw_name_entry(); break;
  case MAIN_MENU: draw_main_menu(); break;
  case UPGRADE_MENU: draw_upgrade_menu(); break;
  case STATS_SCREEN: draw_stats_screen(); break;
  case SHADES_SCREEN: draw_shades(); break;
  case NUMERALS_SCREEN: draw_numerals(); break;
  case LEXICON_SCREEN: draw_lexicon(); break;
  case TEXT_SCREEN: draw_text_screen(); break;
  case TUTORIAL: draw_tutorial(); break;
  case GUIDE_SCREEN: draw_guide(); break;
  case VESTIGIA_SCREEN: draw_vestigia_screen(); break;
  default: break;
  }
  return true;
}

SCENE_ENTRY(PLAY) bool draw_play_scene() {
  bool dirty;
  switch (state) {
  case PAUSED:
    dirty = (state != last_drawn_state) || (menu_index != last_pause_index) ||
            (pause_confirming != last_pause_confirming);
    break;
  case SECOND_DEATH:
    dirty = (state != last_drawn_state) || (menu_index != last_second_death_index);
    break;
  case PLAYING:
  default:
    // PLAYING stays always-dirty (entity world animates).
    dirty = true;
    break;
  }
  if (!dirty) return false;

  last_drawn_state        = state;
  last_pause_index        = menu_index;
  last_pause_confirming   = pause_confirming;
  last_second_death_index = menu_index;

  if (state == SECOND_DEATH) {
    draw_second_death();
    return true;
  }

  // PLAYING + PAUSED: render the world, then overlay pause if needed.
  fb::clear();

  // Pass 1: blit every entity's sprite. The PROGMEM-vs-RAM dispatch
  // (raw sprites vs LZ77-decoded boss cache) lives inside
  // draw_entity_sprite, kept out-of-line so it's not duplicated 32× into
  // this loop's machine code.
  for (u8 i = 0; i < ent::POOL_SIZE; ++i) {
    const ent::Entity& e = ent::pool[i];
    if (!e.active) continue;
    if (e.kind == ent::PLAYER) {
      // Player renders the active animation frame's sprite_id, mirrored
      // horizontally if facing left. The frame index + state are driven
      // by tick_player_animation() in update_player(). Sprites for the
      // anim frames live on FX flash; portraits and PROGMEM-only paths
      // fall through to data() if fx_offset is FX_OFFSET_NONE.
      const u8 anim_id = anim_frame_sprite_id(player_anim_state, player_anim_frame);
      const i16 px     = fx_to_px(e.x);
      const i16 py     = fx_to_px(e.y);
      const u8 w       = sprites::width(anim_id);
      const u8 h       = sprites::height(anim_id);
      const u32 fx_off = sprites::fx_offset(anim_id);
      if (fx_off != sprites::FX_OFFSET_NONE) {
        if (player_anim_facing_left)
          draw_fx_sprite_mirrored(px, py, w, h, fx_off);
        else
          draw_fx_sprite(px, py, w, h, fx_off);
      } else {
        const u8* p = sprites::data(anim_id);
        if (p) {
          if (player_anim_facing_left)
            draw_progmem_sprite_mirrored(px, py, w, h, p);
          else
            draw_progmem_sprite(px, py, w, h, p);
        }
      }
      continue;
    }
    if (e.sprite_id < sprites::COUNT) {
      draw_entity_sprite(e.sprite_id, fx_to_px(e.x), fx_to_px(e.y));
    }
  }

  // Pass 2: per-kind overlays (facing pixel + HP bars). Kept separate from
  // the sprite pass so the base sprite always renders identically regardless
  // of state.
  for (u8 i = 0; i < ent::POOL_SIZE; ++i) {
    const ent::Entity& e = ent::pool[i];
    if (!e.active) continue;
    switch (e.kind) {
    case ent::PLAYER: draw_player_overlay(e); break;
    case ent::ENEMY: draw_enemy_overlay(e); break;
    default: break;
    }
  }

  draw_hud();

  if (state == PAUSED) draw_pause_menu();
  return true;
}

}  // namespace game
