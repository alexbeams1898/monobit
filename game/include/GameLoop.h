#pragma once

class Engine;
class EntityManager;

// Game-side update function. Registered with Engine::setGameUpdate() so the
// engine calls it once per fixed-step tick without knowing any game logic.
void gameUpdate(Engine& engine, EntityManager& em, double dt);
