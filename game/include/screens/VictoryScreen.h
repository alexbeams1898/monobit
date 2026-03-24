#pragma once

#include "FontManager.h"

class EntityManager;

namespace VictoryScreen
{

void init(FontHandle body_font, FontHandle title_font, FontHandle big_title_font);
void reset();
bool render(EntityManager& em, int window_w, int window_h, float dt);

} // namespace VictoryScreen
