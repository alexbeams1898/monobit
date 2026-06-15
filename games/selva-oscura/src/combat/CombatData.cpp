#include "combat/CombatData.h"

#include "AppState.h"
#include "AppStateGlobal.h"
#include "Tunables.h"
#include "ecs/Items.h"
#include "items/ItemRegistry.h"
#include "ops/InventoryOps.h"

#include <cstdio>
#include <unordered_map>

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

// Synthesized combat::Weapon per equipped ItemInstance, keyed by the
// item's config_path. The bridge populates these on first equip and
// reuses them on later syncs. Pointer stability matches the
// WeaponRegistry contract (entries live for the registry's lifetime).
std::unordered_map<std::string, Weapon> sSynthesizedWeapons;

// Last-synced ItemInstanceId per hand; the per-frame sync skips work
// when this matches the current inventory state.
engine::ecs::ItemInstanceId sLastSyncedRight = engine::ecs::kInvalidItemInstanceId;
engine::ecs::ItemInstanceId sLastSyncedLeft = engine::ecs::kInvalidItemInstanceId;

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

const Weapon* resolveHandWeaponFor(engine::ecs::ItemInstanceId id,
                                   const engine::ecs::Inventory& inv,
                                   const engine::ecs::ItemRegistry& items,
                                   const WeaponClassRegistry& classes)
{
    if (id == engine::ecs::kInvalidItemInstanceId)
        return nullptr;
    const engine::ecs::ItemInstance* inst = engine::ops::inventory::findById(inv, id);
    if (inst == nullptr)
        return nullptr;
    const engine::ecs::ItemDef* def = items.find(inst->config_path);
    if (def == nullptr || def->weapon_class_id.empty())
        return nullptr;
    const auto class_it = classes.by_id.find(def->weapon_class_id);
    if (class_it == classes.by_id.end())
        return nullptr;

    auto& synth = sSynthesizedWeapons[inst->config_path];
    synth.id = inst->config_path;
    synth.name = def->name;
    synth.class_id = def->weapon_class_id;
    synth.cls = &class_it->second;
    return &synth;
}

void resetSyncCacheForTesting()
{
    sLastSyncedRight = engine::ecs::kInvalidItemInstanceId;
    sLastSyncedLeft = engine::ecs::kInvalidItemInstanceId;
    sSynthesizedWeapons.clear();
}

void syncEquipmentFromInventory()
{
    const selva::PlayerProfile* profile = selva::activePlayerProfile();
    if (profile == nullptr)
        return;

    const auto right_id = profile->equipment.right_hand;
    const auto left_id = profile->equipment.left_hand;
    if (right_id == sLastSyncedRight && left_id == sLastSyncedLeft)
        return;

    sLastSyncedRight = right_id;
    sLastSyncedLeft = left_id;

    const auto& items = selva::items::itemRegistry();
    const Weapon* right = resolveHandWeaponFor(right_id, profile->inventory, items, sWeaponClasses);
    const Weapon* left = resolveHandWeaponFor(left_id, profile->inventory, items, sWeaponClasses);
    sEquipment.right = (right != nullptr) ? right : &sFistsWeapon;
    sEquipment.left = (left != nullptr) ? left : &sFistsWeapon;
}

} // namespace selva::combat
