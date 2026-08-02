#pragma once

#include "Growth.h"
#include "PlayerConfig.h"
#include "Psyche.h"
#include "WorldClock.h"

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
// encounters, thoughts, inputs/yields, runtime status). Save buttons patch
// player.json / atmosphere.json; stat edits are live-only. Call from the ImGui hook.
// `clock` is EDITABLE: the World tab sets the hour outright, because content is
// increasingly gated on time (a door that shuts at eight, a line that only exists
// on day two) and waiting out an in-world evening to test it is not testing.
void render(PlayerConfig& player_config, growth::GrowthState& growth, const psyche::State& obs,
            worldclock::WorldClock& clock);

} // namespace tune_panel
