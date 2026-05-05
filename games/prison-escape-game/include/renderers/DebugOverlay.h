#pragma once

#include "FontManager.h"

class Engine;
class EntityManager;

namespace DebugOverlay
{

void init(FontHandle body_font);
void toggle();
bool isVisible();
void render(Engine& engine, EntityManager& em, int window_w, int window_h);

} // namespace DebugOverlay
