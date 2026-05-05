#pragma once

class EntityManager;

// ---------------------------------------------------------------------------
// FacingDotRenderer -- draws a small dot in front of any entity that has
// FacingDirection + Sprite but no Animation, indicating its visual facing.
// Skips entities whose aim_override_blend is mostly engaged (lock-on) since
// the crosshair already conveys direction in that case.
// Called every frame from the renderUI callback.
// ---------------------------------------------------------------------------

namespace FacingDotRenderer
{

void render(EntityManager& em, int window_w, int window_h, float cam_x, float cam_y, float zoom);

} // namespace FacingDotRenderer
