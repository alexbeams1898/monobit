#pragma once

#include "FontManager.h"
#include "ecs/EntityManager.h"

// ---------------------------------------------------------------------------
// InteractionPromptRenderer -- shows "[F] Pick up <item>" near targeted items.
// Reads InteractTarget from the player entity and renders a label in screen
// space above the highlighted pickup.
// ---------------------------------------------------------------------------

class InteractionPromptRenderer
{
  public:
    static void init(FontHandle font);
    static void render(EntityManager& em, float cam_x, float cam_y, int window_w, int window_h);
};
