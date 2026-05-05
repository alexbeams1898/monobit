#pragma once

#include "FontManager.h"

class EntityManager;

namespace SettingsScreen
{
void init(FontHandle body_font, FontHandle title_font);
void reset();
void render(EntityManager& em, int window_w, int window_h);
} // namespace SettingsScreen
