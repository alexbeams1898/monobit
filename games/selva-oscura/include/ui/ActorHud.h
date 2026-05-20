#pragma once

namespace selva::ui
{

// Player HP + stamina HUD. Two stacked horizontal bars in the
// top-left of the viewport (soulslike convention). HP red,
// stamina green. Dark background, no decoration, drawn each
// frame from selvaRenderImGui.
//
// Reads selva::gameplay::player().hp / .stamina directly. No
// state of its own; just visualization.
void renderActorHud();

// Debug toggle: when true, the world-overlay pass draws projected
// outlines of every hitbox + hurtbox in the pool. Toggled from the
// F1 panel; off by default.
bool showHitVolumes();
void setShowHitVolumes(bool enabled);

} // namespace selva::ui
