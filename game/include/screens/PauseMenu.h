#pragma once

#include "FontManager.h"
#include "TextureManager.h"
#include "ecs/EntityManager.h"

// ---------------------------------------------------------------------------
// PauseMenu -- ESC overlay with Resume / Quit options.
// Game world is fully paused while this is open.
// ---------------------------------------------------------------------------

class PauseMenu
{
  public:
    static void init(FontHandle body_font, FontHandle title_font, TextureManager* tm);

    // Process input (arrow keys / enter) and render the menu.
    // Returns: 0 = nothing, 1 = quit app, 2 = escape run.
    static int render(EntityManager& em, int window_w, int window_h);

    static void reset();
};
