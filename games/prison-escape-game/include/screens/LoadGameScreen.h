#pragma once

#include "FontManager.h"

class EntityManager;

namespace LoadGameScreen
{

void init(FontHandle body_font, FontHandle title_font, FontHandle big_title_font);
void reset();

enum class Action
{
    None,
    Select,
    EditLook,
    Back
};

Action render(EntityManager& em, int window_w, int window_h);
const char* getSelectedName();

} // namespace LoadGameScreen
