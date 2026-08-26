#pragma once

#include "screens/ScreenInput.h"
#include "screens/ScreenStyle.h"

// What greets you. ONE way in: whether a job is waiting is the save's business, not a question
// to put to the player -- a menu that makes him choose between starting and continuing is a
// menu asking him to know what the disk knows.
//
// Pure over its inputs: it draws, and it reports what was chosen. The caller decides what
// that means (build the world, load a save, quit). Keyboard and mouse are interchangeable.
namespace title_screen
{

enum class Action
{
    None,
    Work, // back into the job -- resumed if there is one, begun if there is not
    Settings,
    Quit
};

// Put the cursor back to the top. Call on entering the phase, so it never reopens mid-list.
void reset();

// One frame of keyboard.
Action step(bool up, bool down, bool confirm);

// Draw, and resolve the mouse against what was drawn (hover moves the cursor, click commits).
// Window-space, native resolution.
Action render(const shell_input::Mouse& mouse, int windowW, int windowH);

} // namespace title_screen
