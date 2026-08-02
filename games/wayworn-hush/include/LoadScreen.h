#pragma once

#include <string>
#include <vector>

// ---------------------------------------------------------------------------
// LoadScreen -- who you have been. Lists the pilgrims on the roster; pick one to
// walk as, or forget one.
//
// Pure over its inputs: the caller passes the roster in and enacts what comes
// back. It never touches the save itself.
// ---------------------------------------------------------------------------

namespace load_screen
{

enum class Action
{
    None,
    Walk,   // walk as the pilgrim in id()
    Forget, // forget the pilgrim in id() -- the screen has already confirmed it
    Back
};

// One row's worth of a pilgrim, built by the caller from the roster. The screen
// knows nothing of the save's shape -- only what to show and what id it means.
struct Entry
{
    std::string id;
    std::string name; // may be blank; shown as a quiet stand-in
    int day = 0;      // in-world day reached (0 = never set out)
    bool walked = false;
};

struct Mouse
{
    float x = 0.0f;
    float y = 0.0f;
    bool clicked = false;
};

// Reset the cursor (and dismiss any open confirmation). Call when entering the
// screen.
void reset();

// The id the last action referred to.
const std::string& id();

// One frame of keyboard input, for players who'd rather not reach for the mouse:
// up/down move, confirm walks, `forget` asks to forget the highlighted pilgrim,
// back leaves. Every one of these has a button too -- the keys are the shortcut,
// not the only way in.
Action step(const std::vector<Entry>& entries, bool up, bool down, bool confirm, bool forget,
            bool back);

// Draw the roster and resolve the mouse against it: each pilgrim is a row with a
// Walk and a Forget button, and Forget asks before it does anything. Window-space.
Action render(const std::vector<Entry>& entries, const Mouse& mouse, int windowW, int windowH);

} // namespace load_screen
