#pragma once

#include "TextField.h"

#include <string>

// ---------------------------------------------------------------------------
// NameScreen -- who is walking. Shown when a new pilgrim sets out.
//
// Pure over its inputs like the other surfaces: it draws and reports what was
// chosen; the caller creates the pilgrim and enters the world.
// ---------------------------------------------------------------------------

namespace name_screen
{

enum class Action
{
    None,
    Confirm, // the name in name() is theirs
    Back     // never mind
};

// Mouse state for one frame, decoded by the caller.
struct Mouse
{
    float x = 0.0f;
    float y = 0.0f;
    bool clicked = false;
};

// The keys the screen acts on, decoded by the caller (SDL types stay out of
// here). `typed` is this frame's text input; the edit keys are held states, not
// edges, because the field's key-repeat needs the hold.
struct Keys
{
    std::string typed;
    bool backspace = false;
    bool del = false;
    bool left = false;
    bool right = false;
    bool home = false;
    bool end = false;
    bool confirm = false; // Enter
    bool back = false;    // Escape
};

// Start with an empty field. Call when entering the screen.
void reset();

// The name as typed so far.
const std::string& name();

// One frame of keyboard input. Confirm is refused while the name is unusable
// (see text_field::acceptable), so the screen can't hand back a blank pilgrim.
Action step(const Keys& keys, double dt);

// Draw the prompt + field and resolve the mouse against it. Window-space.
Action render(const Mouse& mouse, int windowW, int windowH);

} // namespace name_screen
