#pragma once

#include "FontManager.h"
#include "TextureManager.h"
#include "ecs/EntityManager.h"

// ---------------------------------------------------------------------------
// HudRenderer -- draws persistent on-screen HUD elements (HP, stamina, XP,
// money, wave info, weapon name). Called every frame from the renderUI
// callback. Uses UIRenderer draw calls internally.
// ---------------------------------------------------------------------------

class HudRenderer
{
  public:
    static void init(FontHandle body_font, FontHandle title_font, TextureManager* tm);
    static void render(EntityManager& em, int window_w, int window_h);

    // Draws a clickable "Menu" button. Returns true if clicked.
    static bool renderMenuButton(EntityManager& em, int window_w, int window_h);
};
