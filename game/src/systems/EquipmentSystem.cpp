#include "systems/EquipmentSystem.h"

#include "ecs/Components.h"
#include "ecs/EntityManager.h"
#include "ecs/GameComponents.h"
#include "ecs/GameConfig.h"

#include <iostream>
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
}

// Populate a Weapon component with unarmed defaults.
// Body's natural weapon takes priority; FormulaConfig::fist is the fallback.
static void weaponFromFist(Weapon& w, const Body* body, const FormulaConfig& f)
{
    w.name = "Fist";
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
}

// Tab cycling: advance to the next weapon slot (or fists).
static void cycleWeapon(const ItemRegistry& items, const Inventory& inv, Equipment& equip)
{
    std::vector<int> weaponSlots;
    weaponSlots.push_back(-1); // fists
    for (int i = 0; i < static_cast<int>(inv.items.size()); ++i)
    {
        const ItemDef* def = items.find(inv.items[i].config_path);
        if (def != nullptr && def->category == ItemCategory::Weapon)
            weaponSlots.push_back(i);
    }
    if (weaponSlots.size() <= 1)
        return;

    int cur = 0;
    for (int i = 0; i < static_cast<int>(weaponSlots.size()); ++i)
    {
        if (weaponSlots[i] == equip.main_hand_slot)
        {
            cur = i;
            break;
        }
    }
    const int nextIdx = (cur + 1) % static_cast<int>(weaponSlots.size());
    const int nextSlot = weaponSlots[nextIdx];

    equip.main_hand_slot = nextSlot;
    if (nextSlot == -1)
    {
        equip.main_hand = {};
    }
    else
    {
        equip.main_hand = inv.items[nextSlot];
        equip.main_hand.quantity = 1;
    }

    const ItemDef* nextDef =
        equip.main_hand.empty() ? nullptr : items.find(equip.main_hand.config_path);
    std::string name = "Fists";
    if (nextDef != nullptr)
        name = std::string(qualityName(equip.main_hand.quality)) + " " + nextDef->name;
    TracyMessageL("WeaponSwitch");
    std::cout << "[Equipment] Switched to " << name;
    if (nextDef != nullptr && (nextDef->str_requirement > 0 || nextDef->dex_requirement > 0))
    {
        std::cout << "  (Requires: STR " << nextDef->str_requirement << " / DEX "
                  << nextDef->dex_requirement << ")";
    }
    std::cout << "\n";
}

// Sync Equipment slot → Weapon/Shield components when the equipped item changes.
static void syncEquipmentSlots(entt::registry& reg, entt::entity entity, Equipment& equip,
                               const ItemRegistry& items, const FormulaConfig& f)
{
    if (equip.main_hand.config_path != equip.synced_main_hand)
    {
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
            cycleWeapon(items, inv, equip);
    }

    for (auto [entity, equip] : reg.view<Equipment>().each())
        syncEquipmentSlots(reg, entity, equip, items, f);
}
