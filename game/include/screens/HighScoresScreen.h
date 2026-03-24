#pragma once

#include "FontManager.h"

class EntityManager;

namespace HighScoresScreen
{

void init(FontHandle body_font, FontHandle title_font);
void reset();
bool render(EntityManager& em, int window_w, int window_h);

} // namespace HighScoresScreen
