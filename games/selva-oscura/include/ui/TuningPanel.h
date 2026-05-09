#pragma once

class Engine;
class EntityManager;

namespace selva::ui
{

// F1 ImGui tuning panel. Sliders for every tunable, debug-clip preview,
// CSV bone recorder arm, frame capture arm, save tunables button.
void selvaRenderImGui(::Engine& engine, ::EntityManager& em);

} // namespace selva::ui
