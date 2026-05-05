// SDL entry point. Mirrors the Arduboy main.cpp loop shape exactly so the
// engine sees the same update/draw/flush cadence. The only PC-specific
// additions: pump SDL events, honor window-close / Escape via input::
// should_quit(), tear down cleanly on exit.

#include "audio.h"
#include "cli.h"
#include "clock.h"
#include "display.h"
#include "framebuffer.h"
#include "game.h"
#include "input.h"
#include "perf.h"
#include "uart_log.h"

#include <SDL.h>
#include <cstdlib>
#include <cstring>

namespace input {
bool should_quit();
}
namespace display {
void shutdown();
}
namespace audio {
namespace platform {
void capture_start(const char* path);
void capture_stop();
}  // namespace platform
}  // namespace audio

int main(int argc, char* argv[]) {
  // SDL-only platform flag scan: --record-audio=PATH dumps every sample
  // the SDL audio callback emits to a 16-bit mono 44.1kHz WAV file.
  // Useful for inspecting SFX timing (e.g. tone duration mismatches,
  // truncated tails) outside of "I think it sounds wrong." Pulled out
  // of cli::parse because audio capture is platform-side, not game.
  const char* record_audio_path = nullptr;
  // Strip --record-audio=PATH out of argv so the game-side cli::parse
  // doesn't see it. Compact remaining args in place rather than zeroing
  // the slot — leaving "--" in argv made the parser reject it as an
  // unknown flag.
  int new_argc = 0;
  for (int i = 0; i < argc; ++i) {
    const char* a = argv[i];
    if (i > 0 && std::strncmp(a, "--record-audio=", 15) == 0) {
      record_audio_path = a + 15;
      continue;
    }
    argv[new_argc++] = argv[i];
  }
  argc = new_argc;

  // Parse CLI flags BEFORE initializing SDL — --help should exit fast
  // without opening a window.
  game::DebugCfg dbg;
  if (!cli::parse(argc, argv, dbg)) {
    return 1;
  }

  // FIRST — sets stdout to binary mode on Windows. Must run before any
  // perf::frame_end() / write_byte() so the perf-trace stream isn't
  // corrupted by CRLF translation. (Caught a 4-byte misalignment per
  // 0x0a in the stream last session — see commit 7beb26c.)
  uart_log::init();

  display::init();
  clock::init();
  audio::init();
  if (record_audio_path) {
    audio::platform::capture_start(record_audio_path);
    // atexit fires on normal returns AND on std::exit / abort paths,
    // so the WAV header gets patched even if the process bails before
    // reaching capture_stop() in the normal teardown below.
    std::atexit([] { audio::platform::capture_stop(); });
  }

  game::init();
  game::apply_debug_overrides(dbg);

  for (;;) {
    perf::frame_begin();

    input::poll();
    if (input::should_quit()) break;

    audio::tick();

    game::update();

    const bool needs_flush = game::draw();
    (void)needs_flush;  // SDL flushes every frame via vsync; no skip for now.
    display::flush();

    perf::frame_end();
    clock::wait_for_next_frame();
  }

  if (record_audio_path) {
    audio::platform::capture_stop();
  }
  display::shutdown();
  return 0;
}
