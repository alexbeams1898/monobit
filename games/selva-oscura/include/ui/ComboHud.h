#pragma once

namespace selva::ui
{

// Always-on overlay showing chain step, next expected button, rhythm
// window (with the perfect sub-zone), last-press accuracy. Drawn each
// frame from selvaRenderImGui when combat-debug is on.
void renderComboHud();

} // namespace selva::ui
