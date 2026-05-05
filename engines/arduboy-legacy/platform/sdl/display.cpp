// SDL2 backend: blit the engine's 128x64 1-bit framebuffer to an
// integer-upscaled window. Pixel-perfect — no filtering, no smoothing.
// The framebuffer layout matches the SSD1306's horizontal addressing
// (1 byte = 8 vertical pixels, bit 0 = top). We unpack it into a
// 128x64 array of u32 RGBA pixels and push that as a streaming texture
// every flush. Cheap — 1024 bytes to 128*64=8192 pixels, zero allocations.

#include "display.h"
#include "framebuffer.h"

#include <SDL.h>

namespace display {

namespace {

// Initial upscale factor — 8x gives 1024x512, which fits comfortably on
// any modern monitor while preserving the 128x64 grid. Window is also
// resizable; SDL_Renderer's logical size keeps the 128x64 framebuffer
// centered and integer-scaled to the largest multiple that fits.
constexpr int UPSCALE = 8;
constexpr int WIN_W   = 128 * UPSCALE;
constexpr int WIN_H   = 64 * UPSCALE;

SDL_Window* window     = nullptr;
SDL_Renderer* renderer = nullptr;
SDL_Texture* texture   = nullptr;

// Packed RGBA pixel buffer updated each flush. 128*64 u32s = 32 KB — fine
// on PC, never touches the Arduboy side.
u32 pixels[128 * 64];

}  // namespace

void init() {
  // Init video AND audio together — audio::init() opens an audio device
  // later, which silently no-ops if the audio subsystem isn't up. We
  // shipped without this for a while; the SDL build was effectively
  // muted and the bug only surfaced when --record-audio produced an
  // empty WAV.
  if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO) != 0) {
    SDL_Log("SDL_Init failed: %s", SDL_GetError());
    return;
  }
  window = SDL_CreateWindow("mono — SELVA OSCURA", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
                            WIN_W, WIN_H, SDL_WINDOW_SHOWN | SDL_WINDOW_RESIZABLE);
  if (!window) {
    SDL_Log("SDL_CreateWindow failed: %s", SDL_GetError());
    return;
  }
  // Accelerated renderer; vsync on so we don't tear. Frame pacing is
  // still handled by clock::wait_for_next_frame(); vsync is belt-and-braces.
  renderer = SDL_CreateRenderer(window, -1, SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
  if (!renderer) {
    SDL_Log("SDL_CreateRenderer failed: %s", SDL_GetError());
    return;
  }
  // Nearest-neighbor upscale — preserves the 1-bit pixel grid.
  SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, "0");
  // Logical size: render target pretends to be 128x64. SDL handles the
  // scaling to whatever the window currently is, maintaining aspect and
  // letterboxing so pixels never go non-square.
  SDL_RenderSetLogicalSize(renderer, 128, 64);
  SDL_RenderSetIntegerScale(renderer, SDL_TRUE);
  texture =
      SDL_CreateTexture(renderer, SDL_PIXELFORMAT_RGBA8888, SDL_TEXTUREACCESS_STREAMING, 128, 64);
  if (!texture) {
    SDL_Log("SDL_CreateTexture failed: %s", SDL_GetError());
    return;
  }
}

void shutdown() {
  if (texture) SDL_DestroyTexture(texture);
  if (renderer) SDL_DestroyRenderer(renderer);
  if (window) SDL_DestroyWindow(window);
  texture  = nullptr;
  renderer = nullptr;
  window   = nullptr;
  SDL_Quit();
}

void flush() {
  if (!renderer || !texture) return;
  // Unpack the 1-bit framebuffer into the RGBA pixel array.
  // Byte layout: buffer[x + (y/8)*128], bit (y&7) is the pixel at (x,y).
  for (int y = 0; y < 64; ++y) {
    const int page = y / 8;
    const int bit  = y & 7;
    const u8 mask  = (u8)(1u << bit);
    for (int x = 0; x < 128; ++x) {
      const u8 byte = fb::buffer[(u16)x + (u16)page * 128];
      const bool on = (byte & mask) != 0;
      // RGBA8888 packs as R<<24 | G<<16 | B<<8 | A on little-endian
      // (SDL_PIXELFORMAT_RGBA8888 is native-endian by default).
      pixels[y * 128 + x] = on ? 0xFFFFFFFFu : 0x000000FFu;
    }
  }
  SDL_UpdateTexture(texture, nullptr, pixels, 128 * sizeof(u32));
  SDL_RenderClear(renderer);
  SDL_RenderCopy(renderer, texture, nullptr, nullptr);
  SDL_RenderPresent(renderer);
}

}  // namespace display
