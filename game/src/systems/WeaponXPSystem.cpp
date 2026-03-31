#include "systems/WeaponXPSystem.h"

#include "ecs/Components.h"
#include "ecs/GameComponents.h"
#include "ecs/GameConfig.h"
#include "systems/NotificationSystem.h"

#include <cmath>
#include <iostream>
#include <tracy/Tracy.hpp>

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

void WeaponXPSystem::grantXP(EntityManager& em, float enemy_power, float source_multiplier)
{
    auto& reg = em.registry();
    for (auto [entity, wxp, actions] : reg.view<WeaponXP, PlayerActions>().each())
    {
        const float amount = enemy_power * source_multiplier;
        if (amount <= 0.0f)
            return;
        wxp.current_xp += amount;
    }
}

void WeaponXPSystem::update(EntityManager& em)
{
    ZoneScopedN("WeaponXPSystem");
    auto& reg = em.registry();
    const auto& f = reg.ctx().get<FormulaConfig>();
    const auto& items = reg.ctx().get<ItemRegistry>();
    const auto& tiers = reg.ctx().get<WeaponTierRegistry>();

    for (auto [entity, wxp, equip] : reg.view<WeaponXP, Equipment>().each())
    {
        // Process level-ups while XP exceeds threshold.
        // Fists use default quality; real weapons use their item quality.
        const QualityTier qt =
            equip.main_hand.empty() ? QualityTier::Common : equip.main_hand.quality;
        const float qf = qualityFactor(qt);

        while (wxp.current_xp >= wxp.xp_to_next)
        {
            wxp.current_xp -= wxp.xp_to_next;
            wxp.level++;

            const ItemDef* def = items.find(equip.main_hand.config_path);

            // Apply stat growth to the live Weapon component.
            if (reg.all_of<Weapon>(entity))
            {
                auto& w = reg.get<Weapon>(entity);

                const float dmgGrowth = resolveGrowth(
                    def, tiers, def ? def->damage_per_level : -1.0f,
                    [](const WeaponTierDef& td) { return td.damage_per_level; }, 1.0f);
                const float scaleGrowth = resolveGrowth(
                    def, tiers, def ? def->scaling_per_level : -1.0f,
                    [](const WeaponTierDef& td) { return td.scaling_per_level; }, 0.02f);

                const float gf = growthFactor(wxp.level, f.weapon_xp.decay_rate, qf);
                w.base_damage += dmgGrowth * gf;
                w.str_scaling += scaleGrowth * gf;
                w.dex_scaling += scaleGrowth * gf;

                std::cout << "[WeaponXP] " << w.name << " leveled up to " << wxp.level
                          << " (dmg=" << w.base_damage << " str_s=" << w.str_scaling
                          << " dex_s=" << w.dex_scaling << ")\n";
                NotificationSystem::push(w.name + " Lv" + std::to_string(wxp.level),
                                         {0.9f, 0.78f, 0.45f, 1.0f});
            }

            wxp.xp_to_next = computeXpThreshold(wxp.level, qf, def, tiers, f);
        }
    }
}
