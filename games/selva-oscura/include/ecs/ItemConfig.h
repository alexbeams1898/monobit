#pragma once

#include "ecs/Items.h"

// ---------------------------------------------------------------------------
// Selva-side item definitions are aliases over the engine layer. See the
// note in ecs/GameComponents.h.
// ---------------------------------------------------------------------------

namespace selva
{

using engine::ecs::ItemDef;
using engine::ecs::ItemRegistry;
using engine::ecs::qualityName;
using engine::ecs::Rarity;
using engine::ecs::rarityName;

// Process-wide singleton accessor for the item registry. Implementation lives
// in ops/InventoryOps.cpp (Meyer's-style singleton).
ItemRegistry& itemRegistry();

} // namespace selva
