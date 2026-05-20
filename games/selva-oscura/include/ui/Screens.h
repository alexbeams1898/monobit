#pragma once

class Engine;

namespace selva::ui
{

// Render the screen for the current GameState::phase, or the pause overlay
// if Playing and the pause menu is open. Called once per frame from
// selvaRenderImGui. Handles input/transitions internally - main.cpp does
// not need to know which screen is active.
//
// Returns true if the game should quit (Quit selected from main menu or
// pause menu). Caller (main.cpp) is responsible for breaking the engine
// loop on true.
bool renderScreens(Engine& engine);

} // namespace selva::ui
