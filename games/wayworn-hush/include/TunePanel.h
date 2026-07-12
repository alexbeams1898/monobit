#pragma once

#include "PlayerConfig.h"

// The F1 tunables panel -- a dev overlay for live-tweaking feel values without a
// rebuild. Edits apply to the in-memory config immediately (the sole live
// truth); a Save button patches the value back into its JSON so the change
// persists. Built on the engine's ImGui hook (Engine::setRenderImGui). Dev-only;
// gated behind F1 and never shown by default.
namespace tune_panel
{

// Toggle visibility (bound to F1 by the caller).
void toggle();

// True while the panel is showing (the caller can gate other input on this).
bool visible();

// Draw the panel (no-op when hidden). Edits `player_config` in place (movement
// reads it live). The grade is read from / written to the pixel target directly.
// Save buttons patch player.json / atmosphere.json. Call from the ImGui hook.
void render(PlayerConfig& player_config);

} // namespace tune_panel
