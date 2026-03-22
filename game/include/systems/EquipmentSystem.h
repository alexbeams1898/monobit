#pragma once

class EntityManager;

namespace EquipmentSystem
{

// Syncs Equipment slots to Weapon/Shield components.
// Run after InputMappingSystem, before CombatSystem.
void update(EntityManager& em);

} // namespace EquipmentSystem
