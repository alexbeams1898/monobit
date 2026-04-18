#pragma once

class EntityManager;

// World-space visualization of combat geometry: hurtboxes (green),
// pushboxes (yellow), active hitboxes (red). Toggled with F6, cycles through
// modes: off -> hurtboxes only -> + pushboxes -> + hitboxes.
//
// Draws via DebugDraw primitives, so it integrates with the existing debug
// rendering pass (Engine::setRenderDebug callback).
namespace CombatDebugOverlay
{
enum class Mode : int
{
    Off = 0,
    Hurtboxes,
    HurtboxesPlusPushboxes,
    All,
    COUNT
};

void cycleMode();
Mode mode();
void render(EntityManager& em);
} // namespace CombatDebugOverlay
