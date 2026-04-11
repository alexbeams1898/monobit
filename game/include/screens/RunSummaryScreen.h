#pragma once

#include "FontManager.h"

class EntityManager;

namespace RunSummaryScreen
{

void init(FontHandle body_font, FontHandle title_font);
void reset(bool escaped, int score, bool is_high_score, bool god_mode = false);
bool render(EntityManager& em, int window_w, int window_h);

} // namespace RunSummaryScreen
