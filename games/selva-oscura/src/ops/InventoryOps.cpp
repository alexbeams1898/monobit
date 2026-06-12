#include "ecs/ItemConfig.h"

namespace selva
{

// Process-wide ItemRegistry singleton. Engine ships the TYPE; the
// instance is game-side, same Meyer's pattern as clips() / archetypes().
ItemRegistry& itemRegistry()
{
    static ItemRegistry s_registry;
    return s_registry;
}

} // namespace selva
