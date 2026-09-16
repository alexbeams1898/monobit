#pragma once

class Engine;
class EntityManager;

// Where the player is pointing, and whether he is firing.
//
// Aim is DECOUPLED from movement and from which way the sprite faces: the character walks with
// WASD, flips to face the way he walks, and hits wherever the cursor is. Twin-stick convention.
//
// It lives here rather than in Player because the cursor is a shell-wide thing -- it must be
// readable by anything that wants to know where the player is pointing, and it must stop
// meaning anything while a menu is up.
namespace aim
{

// Read the cursor and mouse buttons. Call once per tick, before anything that fires.
void update(const Engine& engine, EntityManager& em);

// Ignore each mouse button until it is RELEASED and pressed again. Called when play begins: the
// click that chose "take the job" -- or the right-click that closed a menu -- is still physically
// down when the world appears, and without this it is read a second time as an order to fire or
// a raised guard: one press, two meanings.
void requireFreshPress();

// A unit vector from the player toward the cursor, in world space.
float dirX();
float dirY();

// The cursor in WORLD space -- where a tool's area lands for a tool that reaches that far.
float worldX();
float worldY();

// Held down this tick / pressed this tick. Firing on the EDGE is what makes a tool feel like a
// decision; a held trigger is for tools that are authored to stream.
bool firing();

// Is the guard held (RMB)? The block: incoming contact spends stamina instead
// of health while this is up, and the trigger is refused -- one hand.
bool guarding();
bool firePressed();

} // namespace aim
