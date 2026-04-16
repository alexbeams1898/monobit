#pragma once

#include "ecs/EntityManager.h"

namespace WeaponSpriteSystem
{
// Game-tick phase: spawn/destroy weapon entities on equip changes.
void updateEquipment(EntityManager& em);

// Render-rate phase: place the weapon sprite at the character's hand anchor
// and update flip / depth based on the character's currently-rendered column.
// Must run AFTER engine AnimationSystem::update so the character's sprite
// rect (src_x/src_y/flip_x) reflects this frame's animation state.
void syncVisuals(EntityManager& em);
}
