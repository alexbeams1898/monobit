#pragma once

class Engine;
class EntityManager;

// Game-side per-frame callbacks the engine invokes. The engine owns the frame
// (window, GL, fixed-step loop, clear, swap); these are where the game does its
// work. Scaffold stage: stubs only — the world is empty and nothing is drawn
// yet. Systems fill in as the vertical slice is built.

void gameUpdate(Engine& engine, EntityManager& em, double dt);
void gameRenderWorld(Engine& engine, EntityManager& em, float camX, float camY, float alpha);
void gameRenderUI(Engine& engine, EntityManager& em);
