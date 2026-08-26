#pragma once

#include "ecs/ItemConfig.h"

class EntityManager;

// Things on the floor, and the walking-onto that collects them.
//
// Deliberately NO magnet and no radius worth naming: currency is automatic because it is
// abstract, but goods enter the satchel by a man stepping where they fell. The difference is
// the difference between income and work, and it is what makes leaving drops behind a choice.
namespace pickup
{

// Put an item on the floor where something died.
void spawnDrop(EntityManager& em, float x, float y, const ItemInstance& inst);

// Collect whatever he is standing on into the satchel, stacking by (item, quality).
void update(EntityManager& em);

} // namespace pickup
