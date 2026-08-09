#pragma once

#include "screens/ScreenStyle.h"

// What death asks, once the black has settled: carry on from the way in, leave the job, or
// quit. A menu rather than an automatic respawn, because standing back up should be a choice
// the player makes -- and the moment a player is most likely to want OUT is the moment the
// game just took something from them.
//
// Every label here already exists elsewhere in the shell (the title's "Carry on", the pause
// screen's "Leave the job" / "Quit") -- death introduces no new vocabulary.
namespace death_screen
{

enum class Action
{
    None,
    CarryOn, // reset the floor, wake at the way in
    Leave,   // back to the title
    Quit     // straight out to the desktop
};

struct Mouse
{
    float x = 0.0f;
    float y = 0.0f;
    bool clicked = false;
};

// Put the cursor back to the top. Call when the screen opens.
void reset();

// One frame of keyboard. Esc is NOT an exit here -- there is nothing to resume to.
Action step(bool up, bool down, bool confirm);

// Draw over the settled black, and resolve the mouse against what was drawn.
Action render(const Mouse& mouse, int windowW, int windowH);

} // namespace death_screen
