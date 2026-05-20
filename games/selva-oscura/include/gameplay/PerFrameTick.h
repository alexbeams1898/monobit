#pragma once

class Engine;
class EntityManager;

namespace selva::gameplay
{

// Per-frame entry point invoked by Engine. Owns input handling,
// combat state machines, dodge/block, locomotion clip pick, sampler
// update, root-motion application. Implementation lives in
// src/gameplay/PerFrameTick.cpp.
void selvaPerFrame(::Engine& engine, ::EntityManager& em, double dt);

// Render-world entry point. Frame capture readback lives here too
// (uses tickstate::frameCapture* flags).
void selvaRenderWorld(::Engine& engine, ::EntityManager& em, float camX, float camY, float alpha);

} // namespace selva::gameplay
