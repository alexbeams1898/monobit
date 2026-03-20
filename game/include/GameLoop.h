#pragma once

class Engine;
class EntityManager;

// Game-side update function. Registered with Engine::setGameUpdate() so the
// engine calls it once per fixed-step tick without knowing any game logic.
void gameUpdate(Engine& engine, EntityManager& em, double dt);

// Per-frame callback. Runs once per render frame after SDL event polling,
// before the fixed-step loop. Handles mouse-aim facing at display rate.
void gamePerFrame(Engine& engine, EntityManager& em, double dt);
