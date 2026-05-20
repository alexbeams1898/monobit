#include "combat/CombatData.h"

#include "Tunables.h"

#include <cstdio>

namespace selva::combat
{

namespace
{

const std::string kWeaponClassesDir = "config/weapon_classes";
const std::string kWeaponsDir = "config/weapons";
const std::string kLoadoutPath = "config/loadout.json";

WeaponClassRegistry sWeaponClasses;
WeaponRegistry sWeapons;
PlayerEquipment sEquipment;
Weapon sFistsWeapon;

} // namespace

const std::string& weaponClassesDir()
{
    return kWeaponClassesDir;
}
const std::string& weaponsDir()
{
    return kWeaponsDir;
}
const std::string& loadoutPath()
{
    return kLoadoutPath;
}

WeaponClassRegistry& weaponClasses()
{
    return sWeaponClasses;
}
WeaponRegistry& weapons()
{
    return sWeapons;
}
PlayerEquipment& equipment()
{
    return sEquipment;
}

void loadAllCombatData(int* out_n_classes, int* out_n_weapons)
{
    const int n_classes = sWeaponClasses.loadDirectory(kWeaponClassesDir);
    const int n_weapons = sWeapons.loadDirectory(kWeaponsDir, sWeaponClasses);
    sEquipment = loadEquipment(kLoadoutPath, sWeapons);
    // Synthesize a "fists" Weapon record pointing at the unarmed
    // class; substitute it into any empty hand slot. Equipment is
    // never null after this — empty hand = fists.
    const auto unarmed_it = sWeaponClasses.by_id.find("unarmed");
    if (unarmed_it != sWeaponClasses.by_id.end())
    {
        sFistsWeapon.id = "fists";
        sFistsWeapon.name = "Fists";
        sFistsWeapon.class_id = "unarmed";
        sFistsWeapon.cls = &unarmed_it->second;
        if (sEquipment.right == nullptr)
            sEquipment.right = &sFistsWeapon;
        if (sEquipment.left == nullptr)
            sEquipment.left = &sFistsWeapon;
    }
    if (out_n_classes != nullptr)
        *out_n_classes = n_classes;
    if (out_n_weapons != nullptr)
        *out_n_weapons = n_weapons;
}

bool offHandCanBlock(const PlayerEquipment& eq)
{
    const Weapon* w = eq.left;
    if (w == nullptr || w->cls == nullptr)
        return false;
    return w->cls->id == "buckler";
}

bool isUnarmed(const PlayerEquipment& eq)
{
    auto is_fists = [](const Weapon* w)
    { return w == nullptr || (w->cls != nullptr && w->cls->id == "unarmed"); };
    return is_fists(eq.right) && is_fists(eq.left);
}

float effectiveAttackPlaybackRate(HandSide hand)
{
    const float global = selva::tuning::current().attack_playback_rate;
    const Weapon* w = (hand == HandSide::Right) ? sEquipment.right : sEquipment.left;
    if (w != nullptr && w->cls != nullptr && w->cls->attack_playback_rate > 0.0f)
        return w->cls->attack_playback_rate;
    return global;
}

} // namespace selva::combat
