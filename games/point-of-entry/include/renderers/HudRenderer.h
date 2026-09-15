#pragma once

class Engine;
class EntityManager;

// What the player needs to see while working: where he is pointing, what he is holding, and
// how much effort he has left.
//
// Drawn in WINDOW space over the finished world, not into the pixel buffer -- the reticle
// follows the mouse exactly, and a bar that reads clearly matters more than one that is made of
// world-sized pixels.
namespace hud
{

void render(const Engine& engine, const EntityManager& em);

// Things anchored to the WORLD rather than the screen -- enemy health, and anything else that
// belongs over a pest. Separate because it needs the camera the world was drawn with.
void renderWorldOverlays(const Engine& engine, const EntityManager& em, float camX, float camY,
                         int zoom);

// Show the system pointer on menus, hide it while playing. Called every frame with the current
// state -- see the note in the implementation for why this is derived rather than toggled.
// Hide the system cursor exactly where this game draws its own. Every other state -- a menu,
// the dev panel, a room where nothing can reach him -- gets the pointer back.
void cursorForPhase(bool crosshair);

// Forget what the HUD remembers between sittings. Called when a job is put up, so a total
// loaded from disk is a total rather than a payday.
void reset();

} // namespace hud
