#pragma once

#include "screens/ScreenInput.h"
#include "screens/ScreenStyle.h"

class EntityManager;
class TextureManager;

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

// Put the cursor and tab back to the start. Call when the screen opens.
void reset();

// One frame of keyboard. Left/right change tab; `back` steps the ladder -- an open field-guide
// page to its listing, anywhere else out to play.
Action step(bool up, bool down, bool left, bool right, bool confirm, bool back);

// Draw the current tab, and resolve the mouse against what was drawn. Needs the world because
// the Sheet reads the player's stats out of it, and textures because the field guide draws
// each species' own art on its page.
Action render(EntityManager& em, TextureManager& tm, const shell_input::Mouse& mouse, int windowW,
              int windowH);

} // namespace pause_screen
