#pragma once

#include "combat/PlayerEquipment.h"
#include "combat/Weapon.h"
#include "combat/WeaponClass.h"
#include "ecs/Items.h"

#include <string>

namespace selva::combat
{

// Path constants for the JSON data files this module loads.
const std::string& weaponClassesDir();
const std::string& weaponsDir();
const std::string& loadoutPath();

// Singleton accessors for the three combat data registries. All three
// are populated by loadAllCombatData().
WeaponClassRegistry& weaponClasses();
WeaponRegistry& weapons();
PlayerEquipment& equipment();

// One-shot startup load: weapon classes, weapons, equipment. Substitutes
// the "fists" pseudo-weapon into any empty hand slot so equipment.right
// and equipment.left are never null after this returns.
// Returns the (n_classes, n_weapons) counts via out-params; logs a
// summary line to stderr.
void loadAllCombatData(int* out_n_classes = nullptr, int* out_n_weapons = nullptr);

// Capability + stance helpers. Read-only; no globals beyond equipment().
bool offHandCanBlock(const PlayerEquipment& eq);
bool isUnarmed(const PlayerEquipment& eq);

// Resolve the playback rate for the given hand's weapon class. Class
// override (>0) wins over the global tun.attack_playback_rate.
float effectiveAttackPlaybackRate(HandSide hand);

// Per-frame: push the active PlayerProfile's inventory equipment into
// combat::PlayerEquipment so the chain / animation / hitbox systems
// see what the player just equipped via the inventory UI. Resolves
// engine ItemDef.weapon_class_id -> WeaponClass -> synthesized
// combat::Weapon. Empty hand falls back to the fists pseudo-weapon.
// Cheap: skips the lookup when the equipped instance id hasn't
// changed since the last call.
void syncEquipmentFromInventory();

// Test hook: reset the dedup cache so the next syncEquipmentFromInventory
// call does the full lookup regardless of last-seen instance ids. Not
// for gameplay use; tests need it to run cases in any order.
void resetSyncCacheForTesting();

// Resolve an equipped ItemInstanceId to a combat::Weapon* via the
// given registries. Pure function: nullptr return when (id invalid)
// OR (item not in inventory) OR (item's ItemDef not in registry) OR
// (def has empty weapon_class_id) OR (class not in registry).
// Lifetime: the returned pointer is owned by a process-wide cache
// keyed by config_path -- valid for the lifetime of the process.
const Weapon* resolveHandWeaponFor(engine::ecs::ItemInstanceId id,
                                   const engine::ecs::Inventory& inv,
                                   const engine::ecs::ItemRegistry& items,
                                   const WeaponClassRegistry& classes);

} // namespace selva::combat
