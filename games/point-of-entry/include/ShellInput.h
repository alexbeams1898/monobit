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

struct Frame
{
    bool up = false;
    bool down = false;
    bool left = false; // tab switching on tabbed screens
    bool right = false;
    bool confirm = false;
    bool back = false;
    float mouse_x = 0.0f;
    float mouse_y = 0.0f;
    bool clicked = false;
};

// Read the keyboard and mouse for this frame.
Frame read();

} // namespace shell_input
