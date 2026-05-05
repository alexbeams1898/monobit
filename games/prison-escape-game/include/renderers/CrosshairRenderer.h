#pragma once

class EntityManager;

// ---------------------------------------------------------------------------
// CrosshairRenderer -- draws the twin-stick aim reticle at the mouse position.
// Handles lock-on blend: when a FacingDirection has aim_override_blend > 0,
// the reticle lerps toward the world-space override target.
// Called every frame from the renderUI callback.
// ---------------------------------------------------------------------------

namespace CrosshairRenderer
{

void render(EntityManager& em, int window_w, int window_h, float cam_x, float cam_y, float zoom);

} // namespace CrosshairRenderer
