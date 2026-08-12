#pragma once

// One frame of menu input, decoded away from main.cpp.
//
// It lives here for a build reason worth knowing: SDL's headers rewrite `main` into
// `SDL_main`, so a main.cpp that includes <SDL.h> fails to link on Windows looking for a
// WinMain nobody wrote. Every game in this workspace keeps SDL out of main.cpp.
//
// Edge-triggered throughout: a menu wants "was pressed", or a held key walks the cursor sixty
// times a second.
namespace shell_input
{

// The pointer, as a screen sees it. `moved` is what lets a screen tell a cursor resting
// somewhere harmless from one the player is actually steering.
struct Mouse
{
    float x = 0.0f;
    float y = 0.0f;
    bool clicked = false;
    bool moved = false;
    int wheel = 0; // notches this frame, positive = away from the hand
};

struct Frame
{
    bool up = false;
    bool down = false;
    bool left = false; // tab switching on tabbed screens
    bool right = false;
    bool confirm = false;
    bool back = false; // Esc or right-click: one step out, wherever a screen is up
    bool menu = false; // Esc only -- opening a menu is not a back-step, and the mouse never opens
    Mouse mouse;
};

// Read the keyboard and mouse for this frame. The wheel arrives from the caller because it is
// an EVENT, counted by the engine's pump rather than readable as a state.
Frame read(int wheel);

// NOTHING CHOSEN IS A REAL STATE. Every list starts at -1 and stays there until the player
// says otherwise, so a screen never opens with an answer already under the cursor -- and a
// confirm that lands on -1 takes nothing.
inline constexpr int kNoChoice = -1;

// Walk a list by key. From nothing, the first press lands on an end rather than resuming a
// choice that was never made.
int step(int cursor, int count, bool up, bool down);

// Walk a list whose entries are not all takeable -- an unearned guide page, a menu row with
// nothing behind it. The cursor steps OVER them rather than landing and showing nothing.
template <typename Takeable>
int stepOver(int cursor, int count, bool up, bool down, Takeable takeable)
{
    if (!up && !down)
        return cursor;
    int c = cursor;
    for (int guard = 0; guard < count; ++guard)
    {
        c = step(c, count, up, down);
        if (c == kNoChoice || takeable(c))
            return c;
    }
    return kNoChoice;
}

// Fold the pointer into the same cursor: what it is over wins, and pointing at nothing clears
// the choice -- but only once the pointer has actually moved, so a mouse left sitting still
// cannot wipe a choice made at the keyboard.
int hover(int cursor, int hovered, bool moved);

// The first row a scrolling list shows. The wheel moves the window freely; `follow` drags it
// back to a row that must stay in sight (a cursor just walked by key), or kNoChoice to let the
// view sit where the hand left it.
int scrollTop(int top, int rows, int visible, int wheel, int follow);

} // namespace shell_input
