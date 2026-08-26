#pragma once

#include "UIRenderer.h"

#include <string>

class Engine;
class EntityManager;

// Numbers that rise off a thing and fade.
//
// Damage you cannot see is damage you cannot judge -- without it, a weak tool and a resistant
// enemy look identical, and the player has no way to tell that a choice mattered. The number IS
// the feedback loop.
//
// They are drawn in WINDOW space over the finished world, so text stays crisp rather than being
// built out of world pixels; the world position is projected each frame.
namespace floaters
{

// Spawn one at a world position. Colour is the caller's business (a palette constant -- a kill
// reads differently from a graze, and neither is this system's decision).
void add(float worldX, float worldY, const std::string& text, const Color& c);

void update(float dt);

// Project and draw. Needs the camera, since these live in the world rather than on the HUD.
void render(Engine& engine, float camX, float camY, int zoom);

// Drop everything -- a new chamber should not inherit the last one's numbers.
void clear();

} // namespace floaters
