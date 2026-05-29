#include "ecs/ItemConfig.h"

// ---------------------------------------------------------------------------
// Process-wide singleton storage for Selva's item registry.
//
// The registry TYPE lives in engine::ecs (see ecs/Items.h) but engine doesn't
// instantiate a process-wide singleton -- that's a game-side decision. Selva
// uses the same Meyer's-static pattern as its other registries
// (selva::anim::clips(), selva::gameplay::archetypes()) and loads it from
// config/items/*.json at boot.
// ---------------------------------------------------------------------------

namespace selva
{

ItemRegistry& itemRegistry()
{
    static ItemRegistry s_registry;
    return s_registry;
}

} // namespace selva
