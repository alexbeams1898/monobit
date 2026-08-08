#pragma once

#include "screens/ScreenStyle.h"

class EntityManager;

// The pause screen: what the world does when you stop it, laid over the frozen house.
//
// TABBED, now that there is something to put behind a tab: the Sheet (who he is) beside System
// (the machine's business). More tabs join as their systems arrive -- the kit, the satchel --
// and each earns its place by having real content, never as an empty placeholder label.
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

// Put the cursor and tab back to the start. Call when the screen opens.
void reset();

// One frame of keyboard. Left/right change tab; `back` (Esc) resumes from anywhere.
Action step(bool up, bool down, bool left, bool right, bool confirm, bool back);

// Draw the current tab, and resolve the mouse against what was drawn. Needs the world because
// the Sheet reads the player's stats out of it.
Action render(EntityManager& em, const Mouse& mouse, int windowW, int windowH);

} // namespace pause_screen
