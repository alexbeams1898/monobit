#pragma once

class Engine;
class EntityManager;

// The dev panel: F1. Live knobs for the things that can only be judged by playing --
// walk speed, and whatever joins it as the game grows.
//
// It exists because tuning by rebuild is slow and tuning by argument is worse. A value
// that has to be FELT (how fast a man should walk, how far a lantern reaches) gets a
// slider here first and becomes a constant or a config entry once it has settled.
namespace debug_panel
{

// Show / hide. Wired to F1.
void toggle();

// Draw it. Called from the engine's ImGui pass; a no-op while hidden.
void render(Engine& engine, EntityManager& em);

// Is it open? The cursor needs to know: a panel you cannot point at is not a panel.
bool visible();

// The walk speed the panel is currently holding, in px/s. The player reads this rather
// than a constant so the slider is live. Whole pixels per tick keeps the world scrolling
// evenly -- see Player.cpp.
float walkSpeed();

// How many screen pixels one drawn pixel occupies. INTEGER, always: a fractional upscale
// makes some pixels bigger than others, which is the one thing pixel art cannot survive.
// The internal render size is the window divided by this, so the same character is the same
// physical size on any display.
int zoom();

} // namespace debug_panel
