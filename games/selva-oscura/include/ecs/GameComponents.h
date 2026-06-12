#pragma once

#include "ecs/Items.h"
#include "ops/InventoryOps.h"

// ---------------------------------------------------------------------------
// Selva-side inventory component types are aliases over the engine layer.
// engine::ecs::Items.h is the source of truth; this header keeps existing
// includes (#include "ecs/GameComponents.h") compiling unchanged.
//
// When Selva carries its own copies of these structs they will drift from
// the engine layer. Aliasing removes the duplication permanently.
// ---------------------------------------------------------------------------

namespace selva
{

using engine::ecs::ArmorSlot;
using engine::ecs::Equipment;
using engine::ecs::EquipSlot;
using engine::ecs::Inventory;
using engine::ecs::ItemCategory;
using engine::ecs::ItemInstance;
using engine::ecs::QualityTier;

// API alias: front-end / pause-menu callers use selva::InventoryOps::xxx,
// which is just engine::ops::inventory::xxx (one definition; full unit-test
// coverage lives in engines/engine/tests/).
namespace InventoryOps = engine::ops::inventory;

} // namespace selva
