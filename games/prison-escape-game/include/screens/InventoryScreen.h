#pragma once

#include "FontManager.h"
#include "ecs/EntityManager.h"

// ---------------------------------------------------------------------------
// InventoryScreen -- full-screen overlay showing inventory grid + equipment.
// Opens on I key. Game world pauses while open.
// ---------------------------------------------------------------------------

class InventoryScreen
{
  public:
    static void init(FontHandle body_font, FontHandle title_font);
    static void render(EntityManager& em, int window_w, int window_h);
    static void reset();
};
