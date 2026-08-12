#pragma once

#include "screens/ScreenInput.h"
#include "screens/ScreenStyle.h"

class EntityManager;

// The staging area's own screen -- opened by interacting with the spot, the way the genre's
// fire opens its menu. Every TRANSACTION lives here: choosing the brew, buying stat points.
// The pause screen's sheet stays a readout; this is where things change hands.
namespace staging_screen
{

enum class Action
{
    None,
    Close // back to work
};

// Back to the top page. Call when the screen opens.
void reset();

// One frame of keyboard. Esc backs out of a page, then out of the screen.
Action step(bool up, bool down, bool confirm, bool back);

// Draw the current page over the dimmed world; resolve the mouse against what was drawn.
Action render(EntityManager& em, const shell_input::Mouse& mouse, int windowW, int windowH);

} // namespace staging_screen
