#include "systems/WeaponXPSystem.h"

#include "ecs/Components.h"
#include "ecs/GameComponents.h"
#include "ecs/GameConfig.h"
#include "ops/InventoryOps.h"
#include "systems/NotificationSystem.h"

#include <tracy/Tracy.hpp>

#include <cmath>

namespace
{

// Quality factor: higher quality = slower decay + gentler XP curve.
float qualityFactor(QualityTier q)
{
    return 1.0f + static_cast<float>(static_cast<int>(q)) * 0.1f;
}

// XP required to reach the next level. No hard cap -- curve steepens with level.
// quality_factor softens the exponent: higher quality = gentler curve.
float xpToNext(int level, float base_xp, float exponent, float qf)
{
    return base_xp * std::pow(static_cast<float>(level), exponent / qf);
}

// Resolve a per-level growth value: item def override -> tier default -> hardcoded fallback.
// item_value < 0 means "not overridden, use tier default".
float resolveGrowth(const ItemDef* def, const WeaponTierRegistry& tiers, float item_value,
                    float (*tier_field)(const WeaponTierDef&), float fallback)
{
    if (def != nullptr && item_value >= 0.0f)
        return item_value;
    if (def != nullptr)
    {
        const WeaponTierDef* td = tiers.find(def->weapon_tier);
        if (td != nullptr)
            return tier_field(*td);
    }
    return fallback;
}

// Compute XP threshold for next level, accounting for tier rate and weapon power.
float computeXpThreshold(int level, float qf, const ItemDef* def, const WeaponTierRegistry& tiers,
                         const FormulaConfig& f)
{
    float tierRate = 1.0f;
    float powerRate = 1.0f;
    if (def != nullptr)
    {
        const WeaponTierDef* td = tiers.find(def->weapon_tier);
        if (td != nullptr)
            tierRate = td->xp_rate;
        powerRate =
            f.weapon_xp.power_base + def->base_damage * f.weapon_xp.power_dmg_factor +
            static_cast<float>(static_cast<int>(def->rarity)) * f.weapon_xp.power_rarity_factor;
    }
    return xpToNext(level, f.weapon_xp.base_xp, f.weapon_xp.exponent, qf) * tierRate * powerRate;
}

// Stat growth per level, with quality-driven decay.
// Returns a multiplier in (0, 1] that decays toward zero as level rises.
// Higher quality = slower decay.
float growthFactor(int level, float decay_rate, float qf)
{
    return qf / (1.0f + static_cast<float>(level) * decay_rate / qf);
}

} // namespace

float WeaponXPSystem::computeEnemyPower(int level, int max_hp, float base_damage, int total_stats,
                                        const FormulaConfig& f)
{
    const auto& w = f.weapon_xp;
    return w.power_level_weight * static_cast<float>(level) +
           w.power_hp_weight * static_cast<float>(max_hp) + w.power_dmg_weight * base_damage +
           w.power_stat_weight * static_cast<float>(total_stats);
}

void WeaponXPSystem::grantXP(EntityManager& em, float enemy_power, float source_multiplier,
                             EquipSlot hand)
{
    auto& reg = em.registry();
    const float amount = enemy_power * source_multiplier;
    if (amount <= 0.0f)
        return;

    for (auto [entity, weapon, actions] : reg.view<Weapon, PlayerActions>().each())
    {
        if (hand == EquipSlot::LeftHand)
        {
            if (auto* lw = reg.try_get<LeftWeapon>(entity))
                lw->wxp_current += amount;
        }
        else
        {
            weapon.wxp_current += amount;
        }
    }
}

// Process level-ups for a single weapon's embedded XP fields.
static void processWeaponLevelUps(Weapon& w, const ItemInstance* invItem, const ItemRegistry& items,
                                  const WeaponTierRegistry& tiers, const FormulaConfig& f)
{
    const QualityTier qt = invItem ? invItem->quality : QualityTier::Common;
    const float qf = qualityFactor(qt);
    const std::string path = invItem ? invItem->config_path : std::string{};
    const ItemDef* def = items.find(path);

    while (w.wxp_current >= w.wxp_to_next)
    {
        w.wxp_current -= w.wxp_to_next;
        w.wxp_level++;

        const float dmgGrowth = resolveGrowth(
            def, tiers, def ? def->damage_per_level : -1.0f,
            [](const WeaponTierDef& td) { return td.damage_per_level; }, 1.0f);
        const float scaleGrowth = resolveGrowth(
            def, tiers, def ? def->scaling_per_level : -1.0f,
            [](const WeaponTierDef& td) { return td.scaling_per_level; }, 0.02f);

        const float gf = growthFactor(w.wxp_level, f.weapon_xp.decay_rate, qf);
        w.base_damage += dmgGrowth * gf;
        w.str_scaling += scaleGrowth * gf;
        w.dex_scaling += scaleGrowth * gf;

        NotificationSystem::push(w.name + " Lv" + std::to_string(w.wxp_level),
                                 {0.9f, 0.78f, 0.45f, 1.0f});

        w.wxp_to_next = computeXpThreshold(w.wxp_level, qf, def, tiers, f);
    }
}

void WeaponXPSystem::update(EntityManager& em)
{
    ZoneScopedN("WeaponXPSystem");
    auto& reg = em.registry();
    const auto& f = reg.ctx().get<FormulaConfig>();
    const auto& items = reg.ctx().get<ItemRegistry>();
    const auto& tiers = reg.ctx().get<WeaponTierRegistry>();

    for (auto [entity, weapon, equip] : reg.view<Weapon, Equipment>().each())
    {
        const auto* inv = reg.try_get<Inventory>(entity);
        const auto* rhItem =
            inv ? InventoryOps::equippedItem(*inv, equip, EquipSlot::RightHand) : nullptr;
        processWeaponLevelUps(weapon, rhItem, items, tiers, f);

        if (auto* lw = reg.try_get<LeftWeapon>(entity))
        {
            const auto* lhItem =
                inv ? InventoryOps::equippedItem(*inv, equip, EquipSlot::LeftHand) : nullptr;
            processWeaponLevelUps(*lw, lhItem, items, tiers, f);
        }
    }
}
