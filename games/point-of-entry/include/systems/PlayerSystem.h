#pragma once

#include <entt/entt.hpp>

class Engine;
class EntityManager;

// The player: who he is, and moving him.
//
// This lives apart from main.cpp for a build reason worth knowing -- SDL's
// headers rewrite `main` into `SDL_main`, so a main.cpp that includes <SDL.h>
// fails to link on Windows (it goes looking for a WinMain nobody wrote). Every
// game in this workspace keeps SDL out of main.cpp and reads input from a system
// file like this one.
namespace player
{

// Remember who to move. Called once, after the floor exists.
void bind(entt::entity player);

// Who he is. Anything that acts on the player's behalf -- a tool, a hit area -- needs this
// rather than searching the registry for whoever looks like a player.
entt::entity entity();

// True once per E press: the interact edge, consumed by whoever acts on it.
bool consumeInteract();

// Put him somewhere the way a cut does it: the camera goes with him and his
// previous position goes too, so nothing interpolates across the gap. Every
// teleport in the game -- a way down, a way up, a resumed save -- lands here,
// because a jump that forgets one of the three shows as a smear or a lurch.
void standAt(EntityManager& em, float x, float y);

// The direction he is PRESSING this tick, normalized; zero when idle. Doors
// read this because a wall can stop his feet short of a threshold, but not
// his intent to walk through it.
void moveIntent(float& dx, float& dy);

// WASD against the tile map. Per-frame; wired to Engine::setGameUpdate.
void update(Engine& engine, EntityManager& em, double dt);

} // namespace player
