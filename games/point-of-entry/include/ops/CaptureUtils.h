#pragma once

// Frame capture: dump what actually reached the framebuffer to frame.png.
//
// Worth keeping as a tool rather than a one-off probe. A "nothing is drawing"
// bug is far faster to LOOK at than to reason about -- this is what found the
// depth-test problem that made every sprite in the game invisible while every
// entity, transform and camera value read as correct.
//
// Press F12 in-game; the file lands beside the exe.
namespace capture
{

// Ask for a capture on the next rendered frame.
void request();

// Called by the render pass after the world is drawn: writes the file if one was
// asked for, and clears the request. `w`/`h` are the framebuffer size.
void writeIfRequested(int w, int h);

} // namespace capture
