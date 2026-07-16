#pragma once

// ---------------------------------------------------------------------------
// TitleScreen -- what greets you. Continue the walk (only when there is one to
// continue), begin again, or leave.
//
// Pure over its inputs like the pause page: it draws and reports what was
// chosen; the caller decides what that means (build the world, wipe the save,
// quit). Keyboard and mouse are interchangeable -- the same parity the rest of
// the game's surfaces keep.
// ---------------------------------------------------------------------------

namespace title_screen
{

// What a frame of input committed.
enum class Action
{
    None,
    NewGame,  // name a new pilgrim and set out
    LoadGame, // pick from the pilgrims already walked
    Quit
};

// Mouse state for one frame, decoded by the caller (position + a left-press this
// frame), so this module stays free of engine/input types.
struct Mouse
{
    float x = 0.0f;
    float y = 0.0f;
    bool clicked = false;
};

// Reset the cursor. Call when entering the phase so it doesn't reopen mid-list.
void reset();

// One frame of keyboard input. up/down move the cursor; confirm commits.
// `has_save` hides Load Game when nobody has walked -- the entry does not exist
// rather than sitting there greyed.
Action step(bool up, bool down, bool confirm, bool has_save);

// Draw the title + entries and resolve the mouse against what was drawn
// (hovering moves the cursor, clicking commits). Returns what a click committed.
// Window-space, native resolution.
Action render(const Mouse& mouse, bool has_save, int windowW, int windowH);

} // namespace title_screen
