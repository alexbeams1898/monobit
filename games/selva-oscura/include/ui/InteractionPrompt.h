#pragma once

namespace selva::ui
{

// Render the "[E] {verb} {label}" prompt if there's a current
// interaction target. Center-bottom HUD position (worldspace-
// floating prompt is a future polish pass). No-op when nothing to
// interact with.
void renderInteractionPrompt();

} // namespace selva::ui
