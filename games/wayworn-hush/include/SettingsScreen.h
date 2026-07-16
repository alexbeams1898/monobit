#pragma once

#include "Settings.h"

// ---------------------------------------------------------------------------
// SettingsScreen -- how you like the game. ONE screen, reached two ways: from the title
// (before a walk) and from the pause page's System tab (during one). Both open this; there
// is no second settings UI to drift out of step with the first.
//
// Pure over its inputs like the other screens: it mutates the Settings it is handed and
// reports when the player is done. What "done" returns to is the caller's business (see
// app::State::settings_return_to) -- the screen doesn't know where it was opened from.
// ---------------------------------------------------------------------------

namespace settings_screen
{

// What a frame of input committed.
enum class Action
{
    None,
    Back // leave the screen (the caller decides where back is)
};

// Mouse state for one frame, decoded by the caller.
struct Mouse
{
    float x = 0.0f;
    float y = 0.0f;
    bool clicked = false;
};

// Reset to the top page, cursor at the first row. Call when entering the phase so it
// doesn't reopen halfway down a category.
void reset();

// One frame of keyboard input. up/down move the cursor; confirm opens the highlighted
// category; left/right change the highlighted setting's value; back steps out of a
// category, and leaves the screen from the top page. Changes are written straight into
// `s` -- a settings screen with an apply button is a settings screen you can lie to.
//
// Returns Back only from the TOP page: backing out of a category is the screen's own
// business, not the caller's.
Action step(settings::Settings& s, bool up, bool down, bool left, bool right, bool confirm,
            bool back);

// Draw the screen and resolve the mouse against what was drawn: hovering a row moves the
// cursor, clicking one advances its value (the same thing right does), clicking Back
// leaves. Draws over whatever is behind it (the title's emptiness, or a frozen world).
// Window-space, native resolution.
Action render(settings::Settings& s, const Mouse& mouse, int windowW, int windowH);

} // namespace settings_screen
