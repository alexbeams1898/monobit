#pragma once

#include "FontManager.h"

class EntityManager;

namespace CharCreateScreen
{

void init(FontHandle body_font, FontHandle title_font);
void reset();

enum class Action
{
    None,
    Start,
    Back
};

Action render(EntityManager& em, int window_w, int window_h);
const char* getName();

} // namespace CharCreateScreen
