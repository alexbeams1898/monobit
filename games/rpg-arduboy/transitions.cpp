// Scene-transition content (cover/uncover). Shared across platforms
// per docs/platform-parity.md — the player-facing sequence here must
// run identically on Arduboy and SDL. The platform layer wraps these
// around the page-in mechanism (Arduboy: SPM; SDL: no-op).
//
// All routines call only engine-layer interfaces (audio, clock,
// data_flash, display, framebuffer, input, scene). No platform
// includes; safe to compile on any target.

#include "transitions.h"

#include "animations.h"  // generated: per-anim FRAME_COUNT/BASE/STRIDE
#include "audio.h"
#include "clock.h"
#include "data_flash.h"
#include "data_offsets.h"  // FX asset offsets (OFFSET_FH_IDLE_F0 etc.)
#include "display.h"
#include "framebuffer.h"
#include "input.h"
#include "scene.h"  // scene::Id values

namespace transitions {

namespace {

// Frame wait that also advances the song. The cover/uncover loops live
// inside set_active and never let main() run, so the per-frame
// audio::tick() in main is starved for the duration. Without pumping
// it here, the speaker keeps emitting the last note's frequency until
// SPM silences it (Arduboy cover) or until the destination scene's
// first frame (uncover). The Arduboy 16 kHz audio ISR is unaffected —
// it only halts during SPM page-write itself.
inline void wait_with_audio() {
  audio::tick();
  clock::wait_for_next_frame();
}

// ---- Sigil curtain (Bayer dither) -----------------------------------
//
// Loading-cover: a single-phase Bayer curtain that reveals the fallen
// halo sigil. The sigil is the mark of recognition — the moment Hell
// successfully imposes structure on a soul (see docs/design/, "The
// halo: recognition, not holiness"). The sigil is conceptually behind
// the world; the dither is the curtain pulling away to show it. Each
// frame, pixels whose Bayer threshold equals that frame's level switch
// from "outgoing scene" to "sigil pixel." Over CURTAIN_FRAMES the
// sigil is fully revealed — no intermediate black flash, no two-phase
// dissolve.
//
// SPM freezes on the fully-revealed sigil (held there during page-in);
// destination's first draw replaces it seamlessly.
//
// 16 levels of Bayer × 1 frame each = 16 frames ≈ 267 ms visible
// curtain; +~400 ms SPM freeze on the final frame on Arduboy (no-op
// on SDL); +post-SPM hold + exit animation handled in uncover().
constexpr u8 CURTAIN_FRAMES = 16;

// Post-SPM sigil-hold duration. The destination scene's bytes are now
// resident, but we hold f0 on screen for this many additional frames
// before the exit animation kicks in. 60 frames ≈ 1.0 s at 60 Hz.
// Combined with the ~400 ms SPM freeze (Arduboy only — SDL skips that
// pause but the visible sequence still includes the hold), the user
// sees the static sigil for ~1.0–1.4 s before it begins collapsing.
constexpr u8 SIGIL_HOLD_FRAMES = 60;

// Exit animation: play f1..f15 at 12 fps. 12 fps = 5 game frames per
// anim frame. 15 anim frames × 5 game frames = 75 game frames ≈ 1.25 s.
// A-press during either phase cuts to the destination scene immediately.
constexpr u8 EXIT_ANIM_FRAMES          = 15;  // f1..f15
constexpr u8 EXIT_GAME_FRAMES_PER_ANIM = 5;   // 60 Hz / 12 fps

// Render the sigil curtain. `from_id`/`to_id` unused — the sigil is
// invariant (the lore argues Hell processes the Pilgrim regardless of
// direction).
//
// Algorithm: each frame, read the sigil bytes from FX, then for each
// pixel decide whether the screen should show the SIGIL pixel or the
// OUTGOING pixel based on Bayer threshold vs current level. As level
// walks 16 → 0, more pixels switch from outgoing to sigil.
//
// Why FX read every frame: we need both the original outgoing pixels
// AND the sigil pixels, but only have one 1024 B framebuffer. We read
// the sigil into a 1-byte stack scratch per byte-position, compose it
// with the live outgoing fb byte at that position, write back. The
// "outgoing" bits at any given byte are *whatever's in fb right now* —
// which on frame 1 is the outgoing scene, and on frame N reflects all
// prior-frame switches. This works as long as the level walk is
// monotonic AND the threshold comparison is monotonic too: once a bit
// switches to sigil, it stays sigil for all subsequent frames.
//
// Cost: ~1024 B FX read per frame × 16 frames ≈ 16 KB of FX I/O over
// 267 ms wall-clock. SPI at ~8 Mbit/s = ~16 KB in ~16 ms; spread over
// 16 frames that's ~1 ms of FX time per frame. Negligible.
void render_dither_cover(u8 /*from_id*/, u8 /*to_id*/) {
  for (u8 i = 0; i < CURTAIN_FRAMES; ++i) {
    const u8 level      = (u8)(CURTAIN_FRAMES - i);  // 16, 15, ..., 1
    constexpr u16 CHUNK = 128;
    u8 scratch[CHUNK];
    for (u16 off = 0; off < fb::SIZE; off += CHUNK) {
      data_flash::read(fxdata::OFFSET_FH_IDLE_F0 + off, scratch, CHUNK);
      for (u16 j = 0; j < CHUNK; ++j) {
        const u16 fb_idx = off + j;
        const u8 sigil_b = scratch[j];
        u8 fb_b          = fb::buffer[fb_idx];
        const u8 col     = (u8)(fb_idx % fb::WIDTH);
        const u8 page    = (u8)(fb_idx / fb::WIDTH);
        for (u8 bit = 0; bit < 8; ++bit) {
          const u8 y = (u8)(page * 8 + bit);
          if (fb::bayer_threshold(col, y) >= level) {
            const u8 sigil_bit = (u8)((sigil_b >> bit) & 1u);
            fb_b               = (u8)((fb_b & ~(u8)(1u << bit)) | (u8)(sigil_bit << bit));
          }
        }
        fb::buffer[fb_idx] = fb_b;
      }
    }
    display::flush();
    wait_with_audio();
  }
  // Final guarantee: overwrite with sigil bytes once more so SPM
  // freeze (or the SDL no-op equivalent) lands on the fully-revealed
  // sigil with no dither residue.
  data_flash::read(fxdata::OFFSET_FH_IDLE_F0, fb::buffer, fb::SIZE);
  display::flush();
}

// ---- Beatrice crying exit (TITLE → any) -----------------------------
//
// A-press on title triggers an immediate full-frame invert: the title
// was rendered light-mode (black on white); inversion snaps it to
// white on black in one frame. SPM holds the inverted frame ~400 ms
// (Arduboy only), and post-SPM the crying animation plays at 10 fps
// in place of the sigil cover. The first animation frame replaces the
// inverted title — the LOGO_TITLE and PRESS A overlays vanish on the
// same beat the animation begins.
//
// Frame count, base offset, and stride come from the build-pipeline-
// generated animations.h — saving more frames in the editor and
// re-baking automatically extends the runtime animation.
constexpr u8 BEA_EXIT_ANIM_FRAMES     = anim::BEATRICE_DATA_IDLE_FRAME_COUNT - 1;
constexpr u8 BEA_GAME_FRAMES_PER_ANIM = 6;  // 10 fps (60 Hz / 6)
constexpr u8 BEA_W                    = 58;
constexpr u8 BEA_H                    = 58;
constexpr i16 BEA_X                   = (fb::WIDTH - BEA_W) / 2;   // 35
constexpr i16 BEA_Y                   = (fb::HEIGHT - BEA_H) / 2;  // 3

// Read frame N of Beatrice's idle from FX, blit to fb at the centered
// position over a fully-black background.
void draw_beatrice_frame(u8 idx) {
  // FX offset = base + idx * stride. Stride invariant is enforced by
  // static_asserts in animations.h.
  const u32 fx =
      anim::BEATRICE_DATA_IDLE_FRAME_BASE + (u32)idx * anim::BEATRICE_DATA_IDLE_FRAME_STRIDE;
  fb::clear();
  // 58×58 = 8 pages × 58 cols/page. Read each page row (58 bytes) into
  // a stack scratch and blit. Width fits within the page-row buffer.
  u8 row_buf[BEA_W];
  constexpr u8 PAGES = (BEA_H + 7) / 8;
  for (u8 p = 0; p < PAGES; ++p) {
    data_flash::read(fx + (u32)p * BEA_W, row_buf, BEA_W);
    fb::draw_sprite(BEA_X, BEA_Y + (i16)p * 8, BEA_W, row_buf);
  }
}

// Beatrice cover (pre-page-in): two-beat sequence —
//   1. Redraw with only Beatrice (no logo, no PRESS A) in light-mode.
//      Reads as "the prompts vanish; she's still there."
//   2. Next frame, invert to white-on-black. Reads as the polarity
//      snap that signals departure from the surface.
// SPM (or SDL no-op) then holds on the inverted Beatrice while the
// destination scene pages in. Total visible cover ≈ 33 ms (2 frames
// at 60 Hz).
void render_beatrice_cover() {
  draw_beatrice_frame(0);
  fb::invert_all();
  display::flush();
  wait_with_audio();
  fb::invert_all();
  display::flush();
}

// ---- White-bar wipe (default fallback) ------------------------------
//
// Mirrors the original procedural cover from before the sigil overhaul:
// a horizontal bar slides down from the top over a few frames, ending
// in a fully-white screen that the page-in (or SDL no-op) holds. Used
// for transitions that don't carry sigil lore weight.
constexpr u8 WIPE_FRAMES = 6;

void render_white_wipe() {
  for (u8 f = 1; f <= WIPE_FRAMES; ++f) {
    const u8 bottom = (u8)(((u16)fb::HEIGHT * f) / WIPE_FRAMES);
    fb::clear();
    for (u8 page = 0; page < fb::HEIGHT / 8; ++page) {
      const u8 page_top = (u8)(page * 8);
      if (page_top >= bottom) break;
      const u8 page_bot = (u8)(page_top + 8);
      u8 mask;
      if (page_bot <= bottom) {
        mask = 0xFF;  // entire page row is inside the bar
      } else {
        mask = (u8)((1u << (bottom - page_top)) - 1u);
      }
      for (u8 col = 0; col < fb::WIDTH; ++col) {
        fb::buffer[(u16)page * fb::WIDTH + col] = mask;
      }
    }
    display::flush();
    wait_with_audio();
  }
}

// Sigil cover applies only to two narratively loaded transitions:
//   G→P  (CIRCLE_CARD → PLAYING) — the "Hell processes the Pilgrim"
//         beat after the circle card is revealed.
//   P→M  (SECOND_DEATH or pause-abandon → MAIN_MENU) — coming back up
//         to the wood after the run ends.
// DESCEND (M→G) is covered by the Gate scene's own fade-in; FORSAKE
// (M→T) and other crossings get a simple white-bar wipe instead.
bool wants_sigil(u8 from_id, u8 to_id) {
  return (from_id == scene::ID_GATE && to_id == scene::ID_PLAY) ||
         (from_id == scene::ID_PLAY && to_id == scene::ID_MAIN_MENU);
}

}  // namespace

// ---- Public entry points --------------------------------------------

void cover(u8 from_id, u8 to_id) {
  if (from_id == scene::ID_TITLE) {
    render_beatrice_cover();
    return;
  }
  if (wants_sigil(from_id, to_id)) {
    render_dither_cover(from_id, to_id);
    return;
  }
  // Other transitions (DESCEND, FORSAKE, etc.): plain white-bar wipe.
  render_white_wipe();
}

void uncover(u8 from_id, u8 to_id) {
  if (from_id == scene::ID_TITLE) {
    // Beatrice exit: play f1..fN-1 at 10 fps. A-press during playback
    // cuts to the destination scene immediately.
    for (u8 a = 1; a <= BEA_EXIT_ANIM_FRAMES; ++a) {
      draw_beatrice_frame(a);
      display::flush();
      for (u8 i = 0; i < BEA_GAME_FRAMES_PER_ANIM; ++i) {
        input::poll();
        if (input::pressed(input::A)) return;
        wait_with_audio();
      }
    }
    return;
  }
  // Sigil-eligible transitions only: G→P and P→M. Every other
  // transition skips the post-SPM hold + collapse so the destination's
  // first draw takes over immediately.
  if (!wants_sigil(from_id, to_id)) return;
  // Sigil exit: hold f0 for SIGIL_HOLD_FRAMES, then play f1..f15 at
  // 12 fps as the collapse. A-press during either phase cuts to the
  // destination scene immediately.
  display::flush();
  for (u8 i = 0; i < SIGIL_HOLD_FRAMES; ++i) {
    input::poll();
    if (input::pressed(input::A)) return;
    wait_with_audio();
  }
  for (u8 a = 1; a <= EXIT_ANIM_FRAMES; ++a) {
    const u32 fx_offset = fxdata::OFFSET_FH_IDLE_F0 + (u32)a * fb::SIZE;
    data_flash::read(fx_offset, fb::buffer, fb::SIZE);
    display::flush();
    for (u8 i = 0; i < EXIT_GAME_FRAMES_PER_ANIM; ++i) {
      input::poll();
      if (input::pressed(input::A)) return;
      wait_with_audio();
    }
  }
}

}  // namespace transitions
