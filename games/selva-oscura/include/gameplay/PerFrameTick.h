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

// Sample the current SDL mouse-button state into the file-static
// "previous button state" used for edge detection in selvaPerFrame.
// Call this from the gated-perframe path on frames where selvaPerFrame
// is SKIPPED (pause menu open). Without it, sPrevLMB / sPrevRMB go
// stale during pause and the resume frame sees a fresh press-edge for
// any button that was held when the menu opened or closed, triggering
// a spurious combat action (e.g. RMB-block firing when RMB closes the
// pause menu).
void syncInputEdgesFromCurrentState();

// Render-world entry point. Frame capture readback lives here too
// (uses tickstate::frameCapture* flags).
void selvaRenderWorld(::Engine& engine, ::EntityManager& em, float camX, float camY, float alpha);

} // namespace selva::gameplay
