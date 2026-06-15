#include "ecs/ItemConfig.h"
#include "items/ItemRegistry.h"

namespace selva
{

// `selva::itemRegistry()` is the convenience alias accessor for the
// engine ItemRegistry; the real Meyer's singleton lives in
// `selva::items::itemRegistry()` so the loader (selva/src/items/
// ItemRegistry.cpp) and the namespace-`selva` aliases see the same
// instance. Avoid keeping two parallel statics.
ItemRegistry& itemRegistry()
{
    return selva::items::itemRegistry();
}

} // namespace selva
