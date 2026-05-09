#pragma once

#include <string>

#include "combat/PlayerEquipment.h"
#include "combat/Weapon.h"
#include "combat/WeaponClass.h"

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

} // namespace selva::combat
