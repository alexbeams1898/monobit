#pragma once

class EntityManager;

namespace AIDebugOverlay
{

// F4: cycle Off -> Enemies+Slots -> Enemies+Slots+FlowField -> Off.
void toggle();
bool isVisible();

// Called from the render_debug callback. DebugDraw::setCamera() is already
// set by the engine before this runs.
void render(EntityManager& em);

} // namespace AIDebugOverlay
