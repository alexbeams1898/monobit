#pragma once

#include "FontManager.h"
#include "ecs/EntityManager.h"

// ---------------------------------------------------------------------------
// CraftingScreen -- recipe list overlay. Opens on C key at rest spots.
// Shows recipes with ingredient availability and allows crafting.
// ---------------------------------------------------------------------------

class CraftingScreen
{
  public:
    static void init(FontHandle body_font, FontHandle title_font);
    static void render(EntityManager& em, int window_w, int window_h);
    static void reset();
};
