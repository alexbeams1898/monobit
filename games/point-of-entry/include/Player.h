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

// WASD against the tile map. Per-frame; wired to Engine::setGameUpdate.
void update(Engine& engine, EntityManager& em, double dt);

} // namespace player
