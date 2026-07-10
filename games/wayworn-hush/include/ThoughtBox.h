#pragma once

#include "Observations.h"

using FontHandle = int;

// Renders the inner-monologue textbox: drains observations::State.pending one
// line at a time, each fading in, holding, then fading out before the next.
// Mother-3 register -- understated, low in the frame, soft (no per-syllable
// clatter). See docs/design/OBSERVATION-SYSTEM.md.
namespace thought_box
{
// Set the font once after FontManager loads it.
void init(FontHandle font);

// Advance the display timer at wall-clock rate; pulls the next line from
// state.pending when the current one finishes.
void update(observations::State& state, float dt);

// Draw the current line (if any). Laid out in window space -- the engine's UI
// pass runs at native window resolution, after the world blit.
void render(int windowW, int windowH);
} // namespace thought_box
