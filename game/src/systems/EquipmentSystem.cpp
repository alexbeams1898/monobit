#include "systems/EquipmentSystem.h"

#include "ecs/Components.h"
#include "ecs/EntityManager.h"
#include "ecs/GameComponents.h"
#include "ecs/GameConfig.h"
#include "systems/NotificationSystem.h"

#include <algorithm>
#include <cmath>
#include <tracy/Tracy.hpp>
#include <vector>

// Populate a Weapon component from an ItemDef.
static void weaponFromDef(Weapon& w, const ItemDef& def)
{
    w.name = def.name;
    w.weight = def.weight;
    w.base_damage = def.base_damage;
    w.str_scaling = def.str_scaling;
    w.dex_scaling = def.dex_scaling;
    w.str_requirement = def.str_requirement;
    w.dex_requirement = def.dex_requirement;
    w.swing_cooldown_remaining = 0.0f;
    w.skill_cooldown_remaining = 0.0f;

    w.ranged = def.ranged;
    w.projectile_speed = def.projectile_speed;
    w.effective_range = def.effective_range;
    w.spread = def.spread;
    w.projectile_count = def.projectile_count;
    w.projectile_size = def.projectile_size;
    w.pierce = def.pierce;
    w.projectile_sprite = def.projectile_sprite;
    w.ammo_type = def.ammo_type;
    w.fire_sound = def.fire_sound;
    w.fire_rate = def.fire_rate;
    w.stamina_cost = def.stamina_cost;
}

// Populate a Weapon component with unarmed defaults.
// Body's natural weapon takes priority; FormulaConfig::fist is the fallback.
static void weaponFromFist(Weapon& w, const Body* body, const FormulaConfig& f)
{
    w.name = "Unarmed";
    if (body != nullptr)
    {
        w.weight = body->unarmed_weight;
        w.base_damage = body->unarmed_damage;
        w.str_scaling = body->unarmed_str_scaling;
        w.dex_scaling = body->unarmed_dex_scaling;
    }
    else
    {
        w.weight = f.fist.weight;
        w.base_damage = f.fist.base_damage;
        w.str_scaling = f.fist.str_scaling;
        w.dex_scaling = f.fist.dex_scaling;
    }
    w.str_requirement = 0;
    w.dex_requirement = 0;
    w.swing_cooldown_remaining = 0.0f;
    w.skill_cooldown_remaining = 0.0f;
    w.ranged = false;
}

// Cycle: fists -> weapon 0 -> weapon 1 -> ... -> fists (dir=+1).
// Z reverses: fists -> last weapon -> ... -> weapon 0 -> fists (dir=-1).
// Returns current weapon to its original inventory slot first, rebuilds the
// weapon list, finds where it landed, and advances to the next position.
static void cycleWeapon(const ItemRegistry& items, Inventory& inv, Equipment& equip, int dir = 1)
{
    // Put current weapon back at its original inventory position.
    const int returnSlot =
        equip.main_hand.empty()
            ? -1
            : std::max(0, std::min(equip.main_hand_slot, static_cast<int>(inv.items.size())));
    if (returnSlot >= 0)
    {
        inv.items.insert(inv.items.begin() + returnSlot, std::move(equip.main_hand));
        equip.main_hand = {};
        equip.main_hand_slot = -1;
    }

    // Gather inventory indices of all weapons (stable order).
    std::vector<int> weaponSlots;
    for (int i = 0; i < static_cast<int>(inv.items.size()); ++i)
    {
        const ItemDef* def = items.find(inv.items[i].config_path);
        if (def != nullptr && def->category == ItemCategory::Weapon)
            weaponSlots.push_back(i);
    }

    if (weaponSlots.empty())
        return;

    // Find which weapon-position the returned weapon is at (or -1 for fists).
    int curWeaponPos = -1;
    if (returnSlot >= 0)
    {
        for (int i = 0; i < static_cast<int>(weaponSlots.size()); ++i)
        {
            if (weaponSlots[i] == returnSlot)
            {
                curWeaponPos = i;
                break;
            }
        }
    }

    // Cycle positions: 0 = fists, 1..N = weapons in inventory order.
    // curPos: 0 if fists, curWeaponPos+1 if a weapon was equipped.
    const int curPos = (curWeaponPos >= 0) ? (curWeaponPos + 1) : 0;
    const int total = 1 + static_cast<int>(weaponSlots.size());
    const int nextPos = (curPos + dir + total) % total;

    if (nextPos == 0)
    {
        NotificationSystem::push("Unarmed", {0.8f, 0.8f, 0.8f, 1.0f});
        TracyMessageL("WeaponSwitch");
        return;
    }

    const int invIdx = weaponSlots[nextPos - 1];
    equip.main_hand = std::move(inv.items[invIdx]);
    equip.main_hand.quantity = 1;
    equip.main_hand_slot = invIdx;
    inv.items.erase(inv.items.begin() + invIdx);

    const ItemDef* def = items.find(equip.main_hand.config_path);
    const std::string name = (def != nullptr) ? def->name : "Unknown";
    NotificationSystem::push("Equipped " + name, {0.8f, 0.8f, 0.8f, 1.0f});
    TracyMessageL("WeaponSwitch");
}

// Accumulate weight from a single equipped slot.
static float slotWeight(const ItemInstance& slot, const ItemRegistry& items)
{
    if (slot.empty())
        return 0.0f;
    const ItemDef* def = items.find(slot.config_path);
    return (def != nullptr) ? def->weight : 0.0f;
}

// Recompute ArmorStats from all equipped armor, shield, weapon, and accessories.
static void recomputeArmorStats(entt::registry& reg, entt::entity entity, const Equipment& equip,
                                const ItemRegistry& items, const FormulaConfig& f)
{
    auto& armor = reg.get_or_emplace<ArmorStats>(entity);
    armor.total_defense = 0.0f;
    armor.total_poise_bonus = 0.0f;
    armor.total_weight = 0.0f;

    // Sum armor defense + poise from armor slots.
    for (const auto* slot : {&equip.head, &equip.chest, &equip.legs, &equip.feet})
    {
        if (slot->empty())
            continue;
        const ItemDef* def = items.find(slot->config_path);
        if (def == nullptr)
            continue;
        armor.total_defense += def->defense_bonus;
        armor.total_poise_bonus += def->poise_bonus;
    }

    // Sum weight from ALL equipped slots (weapons, armor, shield, accessories).
    armor.total_weight += slotWeight(equip.main_hand, items);
    armor.total_weight += slotWeight(equip.off_hand, items);
    armor.total_weight += slotWeight(equip.head, items);
    armor.total_weight += slotWeight(equip.chest, items);
    armor.total_weight += slotWeight(equip.legs, items);
    armor.total_weight += slotWeight(equip.feet, items);
    armor.total_weight += slotWeight(equip.accessory_1, items);
    armor.total_weight += slotWeight(equip.accessory_2, items);

    // Compute equip load ratio and tier.
    float capacity = f.equip_load.base_capacity;
    if (reg.all_of<Stats>(entity))
    {
        const auto& s = reg.get<Stats>(entity);
        capacity += static_cast<float>(s.str) * f.equip_load.str_scale +
                    static_cast<float>(s.end) * f.equip_load.end_scale;
    }
    armor.equip_load_ratio = (capacity > 0.0f) ? (armor.total_weight / capacity) : 1.0f;

    if (armor.equip_load_ratio > f.equip_load.heavy_threshold)
        armor.load_tier = 3; // overloaded
    else if (armor.equip_load_ratio > f.equip_load.medium_threshold)
        armor.load_tier = 2; // heavy
    else if (armor.equip_load_ratio > f.equip_load.light_threshold)
        armor.load_tier = 1; // medium
    else
        armor.load_tier = 0; // light

    // Base poise from stats + flat bonus from equipped armor.
    if (reg.all_of<Poise>(entity))
    {
        float base = 0.0f;
        if (reg.all_of<Stats>(entity))
        {
            const auto& s = reg.get<Stats>(entity);
            base = std::floor(static_cast<float>(s.end) * f.poise.end_scale +
                              static_cast<float>(s.str) * f.poise.str_scale);
        }
        reg.get<Poise>(entity).max = base + armor.total_poise_bonus;
    }
}

// Save current weapon XP back to inventory slot (real weapon) or Body (unarmed).
static void saveWeaponXP(entt::registry& reg, entt::entity entity, const std::string& oldConfigPath)
{
    if (!reg.all_of<PlayerActions>(entity) || !reg.all_of<WeaponXP>(entity) ||
        oldConfigPath == "__unsynced__")
        return;

    const auto& wxp = reg.get<WeaponXP>(entity);
    if (oldConfigPath.empty())
    {
        // Was unarmed -- save to Body.
        auto* body = reg.try_get<Body>(entity);
        if (body != nullptr)
        {
            body->unarmed_xp_level = wxp.level;
            body->unarmed_xp_current = wxp.current_xp;
        }
    }
    else
    {
        // Was a real weapon -- find it in inventory by config_path and save.
        auto* inv = reg.try_get<Inventory>(entity);
        if (inv != nullptr)
        {
            for (auto& item : inv->items)
            {
                if (item.config_path == oldConfigPath)
                {
                    item.weapon_xp_level = wxp.level;
                    item.weapon_xp_current = wxp.current_xp;
                    break;
                }
            }
        }
    }
}

// Load weapon XP from the new inventory slot (real weapon) or Body (unarmed).
static void loadWeaponXP(entt::registry& reg, entt::entity entity, const Equipment& equip,
                         const FormulaConfig& f)
{
    auto& wxp = reg.get_or_emplace<WeaponXP>(entity);
    if (!reg.all_of<PlayerActions>(entity))
        return;

    if (equip.main_hand.empty())
    {
        const auto* body = reg.try_get<Body>(entity);
        wxp.level = body ? body->unarmed_xp_level : 1;
        wxp.current_xp = body ? body->unarmed_xp_current : 0.0f;
    }
    else
    {
        wxp.level = equip.main_hand.weapon_xp_level;
        wxp.current_xp = equip.main_hand.weapon_xp_current;
    }
    wxp.xp_to_next =
        f.weapon_xp.base_xp * std::pow(static_cast<float>(wxp.level), f.weapon_xp.exponent);
}

// Sync Equipment slot → Weapon/Shield components when the equipped item changes.
static void syncEquipmentSlots(entt::registry& reg, entt::entity entity, Equipment& equip,
                               const ItemRegistry& items, const FormulaConfig& f)
{
    if (equip.main_hand.config_path != equip.synced_main_hand)
    {
        saveWeaponXP(reg, entity, equip.synced_main_hand);

        equip.synced_main_hand = equip.main_hand.config_path;
        auto& w = reg.get_or_emplace<Weapon>(entity);

        if (equip.main_hand.empty())
        {
            const Body* body = reg.try_get<Body>(entity);
            weaponFromFist(w, body, f);
        }
        else
        {
            const ItemDef* def = items.find(equip.main_hand.config_path);
            if (def != nullptr)
                weaponFromDef(w, *def);
            else
            {
                const Body* body = reg.try_get<Body>(entity);
                weaponFromFist(w, body, f);
            }
        }

        loadWeaponXP(reg, entity, equip, f);

        if (w.ranged)
        {
            auto& rs = reg.get_or_emplace<RangedState>(entity);
            rs.magazine_size = 0;
            rs.reload_time = 1.0f;
            rs.reloading = false;
            rs.reload_timer = 0.0f;
            const ItemDef* rdef = items.find(equip.main_hand.config_path);
            if (rdef != nullptr)
            {
                rs.magazine_size = rdef->magazine_size;
                rs.reload_time = rdef->reload_time;
            }
            rs.ammo_in_magazine = rs.magazine_size;
        }
        else
        {
            reg.remove<RangedState>(entity);
        }
    }

    if (equip.off_hand.config_path != equip.synced_off_hand)
    {
        equip.synced_off_hand = equip.off_hand.config_path;

        if (equip.off_hand.empty())
        {
            reg.remove<Shield>(entity);
        }
        else
        {
            const ItemDef* def = items.find(equip.off_hand.config_path);
            if (def != nullptr && def->max_guard > 0.0f)
            {
                auto& s = reg.get_or_emplace<Shield>(entity);
                s.max_guard = def->max_guard;
                s.guard_health = def->max_guard;
                s.blocking = false;
            }
        }
    }
}

void EquipmentSystem::update(EntityManager& em)
{
    ZoneScopedN("EquipmentSystem");
    auto& reg = em.registry();
    const auto& items = reg.ctx().get<ItemRegistry>();
    const auto& f = reg.ctx().get<FormulaConfig>();

    for (auto [entity, actions, inv, equip] :
         reg.view<PlayerActions, Inventory, Equipment>().each())
    {
        if (actions.cycle_weapon)
            cycleWeapon(items, inv, equip, 1);
        else if (actions.cycle_weapon_prev)
            cycleWeapon(items, inv, equip, -1);
    }

    for (auto [entity, equip] : reg.view<Equipment>().each())
    {
        syncEquipmentSlots(reg, entity, equip, items, f);
        recomputeArmorStats(reg, entity, equip, items, f);
    }
}
