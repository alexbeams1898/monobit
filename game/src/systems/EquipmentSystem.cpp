#include "systems/EquipmentSystem.h"

#include "ecs/Components.h"
#include "ecs/EntityManager.h"
#include "ecs/GameComponents.h"
#include "ecs/GameConfig.h"
#include "ops/InventoryOps.h"
#include "systems/AudioSystem.h"
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

    w.visual_weapon = def.visual_weapon;
    w.weapon_icon = def.weapon_icon.empty() ? def.icon_path : def.weapon_icon;
    w.grip_x = def.grip_x;
    w.grip_y = def.grip_y;
    w.fore_grip_x = def.fore_grip_x;
    w.fore_grip_y = def.fore_grip_y;
    w.weapon_scale = def.weapon_scale;
    w.base_rotation = def.base_rotation;
    w.two_handed = def.two_handed;
    // Preserve two_handed_active across re-syncs. Only the toggle (Alt)
    // and explicit weapon changes (cycling) should flip this.
    if (!def.two_handed)
        w.two_handed_active = false;
    w.attack_anim = def.attack_anim;
    w.shoot_frames = def.shoot_frames;
}

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

    w.visual_weapon.clear();
    w.weapon_icon.clear();
    w.attack_anim.clear();
    w.shoot_frames.clear();
    w.fore_grip_x = 0.0f;
    w.fore_grip_y = 0.0f;
    w.weapon_scale = 1.0f;
    w.base_rotation = 0.0f;
    w.two_handed = false;
    w.two_handed_active = false;
}

// Cycle weapons for a hand. Items stay in inventory — cycling just moves the
// equipment index to the next weapon. Cycle order is alphabetical by
// config_path so it's always deterministic.
static void cycleWeapon(const ItemRegistry& items, const Inventory& inv, Equipment& equip,
                        EquipSlot hand, int dir = 1)
{
    // Build sorted list of inventory indices that hold weapons and are not
    // equipped in the OTHER hand.
    const EquipSlot otherHand =
        (hand == EquipSlot::RightHand) ? EquipSlot::LeftHand : EquipSlot::RightHand;
    const int otherIdx = InventoryOps::slotIndexConst(equip, otherHand);

    struct WeaponEntry
    {
        std::string path;
        int inv_index;
    };
    std::vector<WeaponEntry> weapons;
    for (int i = 0; i < static_cast<int>(inv.items.size()); ++i)
    {
        if (i == otherIdx)
            continue;
        const ItemDef* def = items.find(inv.items[i].config_path);
        if (def != nullptr && def->category == ItemCategory::Weapon)
            weapons.push_back({inv.items[i].config_path, i});
    }

    std::sort(weapons.begin(), weapons.end(),
              [](const WeaponEntry& a, const WeaponEntry& b) { return a.path < b.path; });

    const int currentIdx = InventoryOps::slotIndexConst(equip, hand);

    if (weapons.empty())
    {
        InventoryOps::unequipSlot(equip, hand);
        const char* label = (hand == EquipSlot::LeftHand) ? "Left: Unarmed" : "Right: Unarmed";
        NotificationSystem::push(label, {0.8f, 0.8f, 0.8f, 1.0f});
        TracyMessageL("WeaponSwitch");
        return;
    }

    // Find where the currently equipped weapon is in the sorted list.
    int curWeaponPos = -1;
    if (currentIdx >= 0)
    {
        for (int i = 0; i < static_cast<int>(weapons.size()); ++i)
        {
            if (weapons[i].inv_index == currentIdx)
            {
                curWeaponPos = i;
                break;
            }
        }
    }

    // Cycle: fists(0) -> weapon[0](1) -> ... -> weapon[N-1](N) -> fists(0).
    const int curPos = (curWeaponPos >= 0) ? (curWeaponPos + 1) : 0;
    const int total = 1 + static_cast<int>(weapons.size());
    const int nextPos = (curPos + dir + total) % total;

    if (nextPos == 0)
    {
        InventoryOps::unequipSlot(equip, hand);
        const char* label = (hand == EquipSlot::LeftHand) ? "Left: Unarmed" : "Right: Unarmed";
        NotificationSystem::push(label, {0.8f, 0.8f, 0.8f, 1.0f});
        TracyMessageL("WeaponSwitch");
        return;
    }

    const auto& pick = weapons[nextPos - 1];
    InventoryOps::equipItemToSlot(equip, pick.inv_index, hand);

    const ItemDef* def = items.find(pick.path);
    const std::string name = (def != nullptr) ? def->name : "Unknown";
    const char* prefix = (hand == EquipSlot::LeftHand) ? "Left: " : "Right: ";
    NotificationSystem::push(prefix + name, {0.8f, 0.8f, 0.8f, 1.0f});
    TracyMessageL("WeaponSwitch");
}

// Accumulate weight from a single equipped slot.
static float slotWeight(const Inventory& inv, const Equipment& equip, EquipSlot slot,
                        const ItemRegistry& items)
{
    const auto* item = InventoryOps::equippedItem(inv, equip, slot);
    if (item == nullptr)
        return 0.0f;
    const ItemDef* def = items.find(item->config_path);
    return (def != nullptr) ? def->weight : 0.0f;
}

static void recomputeArmorStats(entt::registry& reg, entt::entity entity, const Inventory& inv,
                                const Equipment& equip, const ItemRegistry& items,
                                const FormulaConfig& f)
{
    auto& armor = reg.get_or_emplace<ArmorStats>(entity);
    armor.total_defense = 0.0f;
    armor.total_poise_bonus = 0.0f;
    armor.total_weight = 0.0f;

    for (const auto armorSlot : {EquipSlot::Head, EquipSlot::Chest, EquipSlot::Legs, EquipSlot::Feet})
    {
        const auto* item = InventoryOps::equippedItem(inv, equip, armorSlot);
        if (item == nullptr)
            continue;
        const ItemDef* def = items.find(item->config_path);
        if (def == nullptr)
            continue;
        armor.total_defense += def->defense_bonus;
        armor.total_poise_bonus += def->poise_bonus;
    }

    static constexpr EquipSlot ALL[] = {EquipSlot::RightHand, EquipSlot::LeftHand, EquipSlot::Head,
                                        EquipSlot::Chest,     EquipSlot::Legs,     EquipSlot::Feet,
                                        EquipSlot::Accessory1, EquipSlot::Accessory2};
    for (const auto s : ALL)
        armor.total_weight += slotWeight(inv, equip, s, items);

    float capacity = f.equip_load.base_capacity;
    if (reg.all_of<Stats>(entity))
    {
        const auto& s = reg.get<Stats>(entity);
        capacity += static_cast<float>(s.str) * f.equip_load.str_scale +
                    static_cast<float>(s.end) * f.equip_load.end_scale;
    }
    armor.equip_load_ratio = (capacity > 0.0f) ? (armor.total_weight / capacity) : 1.0f;

    if (armor.equip_load_ratio > f.equip_load.heavy_threshold)
        armor.load_tier = 3;
    else if (armor.equip_load_ratio > f.equip_load.medium_threshold)
        armor.load_tier = 2;
    else if (armor.equip_load_ratio > f.equip_load.light_threshold)
        armor.load_tier = 1;
    else
        armor.load_tier = 0;

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

// Save weapon XP from the live Weapon struct back to inventory item or Body.
static void saveWeaponXP(entt::registry& reg, entt::entity entity, const Weapon& w,
                         const std::string& oldConfigPath, EquipSlot slot)
{
    if (!reg.all_of<PlayerActions>(entity) || oldConfigPath == "__unsynced__")
        return;

    if (oldConfigPath.empty())
    {
        auto* body = reg.try_get<Body>(entity);
        if (body != nullptr)
        {
            const bool isLeft = (slot == EquipSlot::LeftHand);
            if (isLeft)
            {
                body->unarmed_xp_level_left = w.wxp_level;
                body->unarmed_xp_current_left = w.wxp_current;
            }
            else
            {
                body->unarmed_xp_level_right = w.wxp_level;
                body->unarmed_xp_current_right = w.wxp_current;
            }
        }
    }
    else
    {
        auto* inv = reg.try_get<Inventory>(entity);
        if (inv != nullptr)
        {
            for (auto& item : inv->items)
            {
                if (item.config_path == oldConfigPath)
                {
                    item.weapon_xp_level = w.wxp_level;
                    item.weapon_xp_current = w.wxp_current;
                    break;
                }
            }
        }
    }
}

// Load weapon XP from inventory item or Body into the live Weapon struct.
static void loadWeaponXP(entt::registry& reg, entt::entity entity, Weapon& w,
                         const Inventory& inv, const Equipment& equip, EquipSlot slot,
                         const FormulaConfig& f)
{
    if (!reg.all_of<PlayerActions>(entity))
        return;

    const auto* item = InventoryOps::equippedItem(inv, equip, slot);
    if (item == nullptr)
    {
        const auto* body = reg.try_get<Body>(entity);
        const bool isLeft = (slot == EquipSlot::LeftHand);
        if (isLeft)
        {
            w.wxp_level = body ? body->unarmed_xp_level_left : 1;
            w.wxp_current = body ? body->unarmed_xp_current_left : 0.0f;
        }
        else
        {
            w.wxp_level = body ? body->unarmed_xp_level_right : 1;
            w.wxp_current = body ? body->unarmed_xp_current_right : 0.0f;
        }
    }
    else
    {
        w.wxp_level = item->weapon_xp_level;
        w.wxp_current = item->weapon_xp_current;
    }
    w.wxp_to_next =
        f.weapon_xp.base_xp * std::pow(static_cast<float>(w.wxp_level), f.weapon_xp.exponent);
}

static void syncEquipmentSlots(entt::registry& reg, entt::entity entity, const Inventory& inv,
                               Equipment& equip, const ItemRegistry& items, const FormulaConfig& f)
{
    const std::string rhPath = InventoryOps::equippedPath(inv, equip, EquipSlot::RightHand);

    if (rhPath != equip.synced_right_hand)
    {
        auto& w = reg.get_or_emplace<Weapon>(entity);
        saveWeaponXP(reg, entity, w, equip.synced_right_hand, EquipSlot::RightHand);

        equip.synced_right_hand = rhPath;

        const ItemDef* def = rhPath.empty() ? nullptr : items.find(rhPath);
        if (def != nullptr)
            weaponFromDef(w, *def);
        else
        {
            const Body* body = reg.try_get<Body>(entity);
            weaponFromFist(w, body, f);
        }

        loadWeaponXP(reg, entity, w, inv, equip, EquipSlot::RightHand, f);

        if (w.ranged)
        {
            auto& rs = reg.get_or_emplace<RangedState>(entity);
            rs.magazine_size = 0;
            rs.reload_time = 1.0f;
            rs.reloading = false;
            rs.reload_timer = 0.0f;
            if (def != nullptr)
            {
                rs.magazine_size = def->magazine_size;
                rs.reload_time = def->reload_time;
            }
            rs.ammo_in_magazine = rs.magazine_size;
        }
        else
        {
            reg.remove<RangedState>(entity);
        }
    }

    const std::string lhPath = InventoryOps::equippedPath(inv, equip, EquipSlot::LeftHand);

    if (lhPath != equip.synced_left_hand)
    {
        auto& lw = reg.get_or_emplace<LeftWeapon>(entity);
        saveWeaponXP(reg, entity, lw, equip.synced_left_hand, EquipSlot::LeftHand);

        equip.synced_left_hand = lhPath;

        const ItemDef* def = lhPath.empty() ? nullptr : items.find(lhPath);

        if (def != nullptr && def->max_guard > 0.0f)
        {
            auto& s = reg.get_or_emplace<Shield>(entity);
            s.max_guard = def->max_guard;
            s.guard_health = def->max_guard;
            s.blocking = false;
            const Body* body = reg.try_get<Body>(entity);
            weaponFromFist(lw, body, f);
        }
        else if (def != nullptr && def->category == ItemCategory::Weapon)
        {
            weaponFromDef(lw, *def);
            reg.remove<Shield>(entity);
        }
        else
        {
            reg.remove<Shield>(entity);
            const Body* body = reg.try_get<Body>(entity);
            weaponFromFist(lw, body, f);
        }

        loadWeaponXP(reg, entity, lw, inv, equip, EquipSlot::LeftHand, f);
    }
}

void EquipmentSystem::update(EntityManager& em)
{
    ZoneScopedN("EquipmentSystem");
    auto& reg = em.registry();
    const auto& items = reg.ctx().get<ItemRegistry>();
    const auto& f = reg.ctx().get<FormulaConfig>();
    const auto& snd = reg.ctx().get<SoundConfig>();

    for (auto [entity, actions, inv, equip] :
         reg.view<PlayerActions, Inventory, Equipment>().each())
    {
        const bool rightSwitched = actions.cycle_weapon || actions.cycle_weapon_prev;
        if (actions.cycle_weapon)
            cycleWeapon(items, inv, equip, EquipSlot::RightHand, 1);
        else if (actions.cycle_weapon_prev)
            cycleWeapon(items, inv, equip, EquipSlot::RightHand, -1);

        const bool leftSwitched = actions.cycle_left_weapon || actions.cycle_left_weapon_prev;
        if (actions.cycle_left_weapon)
            cycleWeapon(items, inv, equip, EquipSlot::LeftHand, 1);
        else if (actions.cycle_left_weapon_prev)
            cycleWeapon(items, inv, equip, EquipSlot::LeftHand, -1);

        if (rightSwitched || leftSwitched)
        {
            const auto& reload = snd.get("reload");
            if (!reload.path.empty())
                AudioSystem::playSfx(reload.path, reload.volume);
        }
    }

    for (auto [entity, inv, equip] : reg.view<Inventory, Equipment>().each())
    {
        syncEquipmentSlots(reg, entity, inv, equip, items, f);
        recomputeArmorStats(reg, entity, inv, equip, items, f);
    }
}
