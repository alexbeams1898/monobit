#pragma once

#include "ScreenStyle.h"

// The pause screen: what the world does when you stop it, laid over the frozen house.
//
// Deliberately a bare list today. There is no satchel, no record and no self to read yet, and
// a strip of empty tabs is worse than no strip at all -- a lone tab label is just a stray word
// on the screen. Tabs arrive with the first thing worth putting behind one.
namespace pause_screen
{

enum class Action
{
    None,
    Resume,
    Settings,
    Leave, // back to the title
    Quit   // straight out to the desktop
};

struct Mouse
{
    float x = 0.0f;
    float y = 0.0f;
    bool clicked = false;
};

// Put the cursor back to the top. Call when the screen opens.
void reset();

// One frame of keyboard. `back` (Esc) resumes from anywhere on the page.
Action step(bool up, bool down, bool confirm, bool back);

// Draw, and resolve the mouse against what was drawn. Window-space, native resolution.
Action render(const Mouse& mouse, int windowW, int windowH);

} // namespace pause_screen
