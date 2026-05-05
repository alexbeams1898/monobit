// SDL display backend. Engine code never includes this — only main.cpp
// wires it up. Mirrors the Arduboy display.h surface exactly so the game
// loop is identical across platforms.

#pragma once

namespace display {

void init();   // Open a 128x64 window (upscaled), set up the render target.
void flush();  // Push fb::buffer to the window, then present.

void shutdown();  // Tear down window/renderer. Called from main on exit.

}  // namespace display
