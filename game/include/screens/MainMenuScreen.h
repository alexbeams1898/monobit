#pragma once

#include "FontManager.h"

class EntityManager;

namespace MainMenuScreen
{

void init(FontHandle body_font, FontHandle title_font, FontHandle big_title_font);
void reset();

enum class Action
{
    None,
    NewGame,
    LoadGame,
    HighScores,
    Quit
};

Action render(EntityManager& em, int window_w, int window_h);

} // namespace MainMenuScreen
