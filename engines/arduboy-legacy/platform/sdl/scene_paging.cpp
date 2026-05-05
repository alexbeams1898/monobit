// Platform-side scene paging — SDL implementation.
//
// SDL has no SPM, no scene paging — banks are all resident in the
// linked binary. The only reason this file exists is to satisfy the
// engine's three platform hooks (cover / page_in / uncover) so that
// the player-facing transition sequence runs identically to Arduboy
// per docs/platform-parity.md.
//
//   - cover():  delegate to shared games/rpg/transitions.cpp
//   - page_in(): no-op (SDL doesn't define SCENE_PAGING_ENABLED, so
//                engine/scene.cpp doesn't even call this — included
//                only for symmetry / future-proofing if SDL ever needs
//                to do per-scene init).
//   - uncover(): delegate to shared transitions.cpp
//
// The cover/uncover routines are shared verbatim with Arduboy. They
// call only engine-layer interfaces (audio, clock, data_flash, fb,
// input) so they compile and behave identically here.

#include "scene.h"        // scene::Id values
#include "transitions.h"  // shared cover/uncover content
#include "types.h"

extern "C" void platform_scene_loading_cover(u8 from_id, u8 to_id) {
  transitions::cover(from_id, to_id);
}

extern "C" void platform_scene_loading_uncover(u8 from_id, u8 to_id) {
  transitions::uncover(from_id, to_id);
}
