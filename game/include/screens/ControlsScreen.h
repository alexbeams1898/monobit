#pragma once

#include "FontManager.h"

class EntityManager;
class TextureManager;

namespace ControlsScreen
{
void init(FontHandle body_font, FontHandle title_font, TextureManager* tm);
void reset();
void render(EntityManager& em, int window_w, int window_h);
} // namespace ControlsScreen
