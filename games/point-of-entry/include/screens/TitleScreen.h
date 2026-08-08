#pragma once

#include "screens/ScreenStyle.h"

// What greets you. Take the job, carry on with one already started, settings, or leave.
//
// Pure over its inputs: it draws, and it reports what was chosen. The caller decides what
// that means (build the world, load a save, quit). Keyboard and mouse are interchangeable.
namespace title_screen
{

enum class Action
{
    None,
    NewJob,   // build a fresh house and go in
    Continue, // pick up where the last one left off
    Settings,
    Quit
};

// Mouse for one frame, decoded by the caller so this module stays free of engine input types.
struct Mouse
{
    float x = 0.0f;
    float y = 0.0f;
    bool clicked = false;
};

// Put the cursor back to the top. Call on entering the phase, so it never reopens mid-list.
void reset();

// One frame of keyboard. `has_save` false DISABLES Continue -- the entry stays where it is,
// visibly not for you, so the menu never changes shape under the hand.
Action step(bool up, bool down, bool confirm, bool has_save);

// Draw, and resolve the mouse against what was drawn (hover moves the cursor, click commits).
// Window-space, native resolution.
Action render(const Mouse& mouse, bool has_save, int windowW, int windowH);

} // namespace title_screen
