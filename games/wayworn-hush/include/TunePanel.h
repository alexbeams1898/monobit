#pragma once

#include "Growth.h"
#include "Observations.h"
#include "PlayerConfig.h"

// The F1 tunables panel -- a dev overlay for live-tweaking feel values without a
// rebuild. Config feel-values (movement, grade) edit in place with Save-to-JSON;
// stat values edit live only (they are progression/save-game state, not authored
// config). Built on the engine's ImGui hook (Engine::setRenderImGui). Dev-only;
// gated behind F1 and never shown by default.
namespace tune_panel
{

// Toggle visibility (bound to F1 by the caller).
void toggle();

// Draw the panel (no-op when hidden). Edits `player_config` (movement/cadence)
// and `growth` stat values in place; the grade is read from / written to the
// pixel target. `obs` is shown read-only in the Cognition tab (the live tree:
// observables, thoughts, inputs/yields, runtime status). Save buttons patch
// player.json / atmosphere.json; stat edits are live-only. Call from the ImGui hook.
void render(PlayerConfig& player_config, growth::GrowthState& growth,
            const observations::State& obs);

} // namespace tune_panel
