// SDL2 keyboard -> engine Button mapping. Poll-driven, same model as the
// Arduboy input: each call to poll() reads the keyboard snapshot once and
// computes edge-triggered "pressed" by comparing with the previous frame.
//
// Keybinds (can evolve later; these match common chiptune-emulator defaults):
//   Arrow keys -> UP / DOWN / LEFT / RIGHT
//   Z          -> A
//   X          -> B
//
// Window-close (SDL_QUIT) and Escape both request the engine to exit via
// should_quit(); main.cpp polls that.

#include "input.h"

#include <SDL.h>

namespace input {

namespace {
u8 current          = 0;
u8 previous         = 0;
bool quit_requested = false;
}  // namespace

void poll() {
  // Drain the event queue so window-close / focus messages reach SDL's
  // internal state. We don't build 'pressed' off events — the keyboard
  // state array below is simpler and matches the Arduboy poll model.
  SDL_Event e;
  while (SDL_PollEvent(&e)) {
    if (e.type == SDL_QUIT) quit_requested = true;
    if (e.type == SDL_KEYDOWN && e.key.keysym.sym == SDLK_ESCAPE) quit_requested = true;
  }
  const Uint8* ks = SDL_GetKeyboardState(nullptr);
  u8 s            = 0;
  if (ks[SDL_SCANCODE_UP]) s |= (u8)(1u << UP);
  if (ks[SDL_SCANCODE_DOWN]) s |= (u8)(1u << DOWN);
  if (ks[SDL_SCANCODE_LEFT]) s |= (u8)(1u << LEFT);
  if (ks[SDL_SCANCODE_RIGHT]) s |= (u8)(1u << RIGHT);
  if (ks[SDL_SCANCODE_Z]) s |= (u8)(1u << A);
  if (ks[SDL_SCANCODE_X]) s |= (u8)(1u << B);
  previous = current;
  current  = s;
}

bool held(Button b) {
  return (current >> b) & 1u;
}
bool pressed(Button b) {
  return ((current & ~previous) >> b) & 1u;
}

bool should_quit() {
  return quit_requested;
}

}  // namespace input
