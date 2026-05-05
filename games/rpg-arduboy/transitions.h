// Scene-transition cover/uncover content. Shared across platforms.
//
// engine/scene.cpp's set_active calls platform_scene_loading_cover()
// before any page-in mechanism and platform_scene_loading_uncover()
// after. The Arduboy implementation does SPM page-rewrite between the
// two; SDL has no equivalent (banks are all resident). The
// player-facing visual sequence — sigil curtain, Beatrice crying
// exit, white-bar wipe — must be IDENTICAL across platforms per the
// parity doctrine (docs/platform-parity.md). So those routines live
// here, called from both platforms.
//
// Per-frame wait pumps audio::tick() so the song advances during the
// cover/uncover loops (which run inside set_active and starve main()).

#pragma once

#include "types.h"

namespace transitions {

// Render the cover for a (from_id, to_id) bank crossing. Body is a
// switch over (from_id, to_id) that picks the right closing animation:
//   TITLE → any: render_beatrice_cover
//   G→P, P→M:    render_dither_cover (sigil curtain)
//   any other:   render_white_wipe
// Caller (platform_scene_loading_cover) is responsible for routing
// engine-side calls into this function.
void cover(u8 from_id, u8 to_id);

// Render the uncover after the page-in (or no-op page-in on SDL).
// Beatrice exit animation, sigil hold + collapse, or no-op for
// transitions that don't carry a post-SPM beat.
void uncover(u8 from_id, u8 to_id);

}  // namespace transitions
