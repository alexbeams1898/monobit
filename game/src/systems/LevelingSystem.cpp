#include "systems/LevelingSystem.h"

#include "ecs/Components.h"
#include "ecs/GameComponents.h"
#include "ecs/GameConfig.h"
#include "systems/AudioSystem.h"
#include "systems/ParticleSystem.h"

#include <cmath>
#include <tracy/Tracy.hpp>

// Resolve per-entity HP base from Body component, falling back to the global hp.base.
static int resolveBaseHP(entt::registry& reg, entt::entity entity, const FormulaConfig& f)
{
    const Body* body = reg.try_get<Body>(entity);
    return (body != nullptr) ? body->base_hp : static_cast<int>(f.hp.base);
}

// Resolve entity level: Experience.level for players, AIController.tier for enemies, else 1.
static int resolveLevel(entt::registry& reg, entt::entity entity)
{
    const Experience* exp = reg.try_get<Experience>(entity);
    if (exp != nullptr)
        return exp->level;
    const AIController* ai = reg.try_get<AIController>(entity);
    if (ai != nullptr)
        return ai->tier;
    return 1;
}

// Compute max HP: baseHP + scale * END + level_scale * level.
// baseHP comes from the entity's Body (innate toughness of its physical form).
// END provides the primary HP investment; level gives passive growth.
static int deriveMaxHP(int baseHP, int end, int level, const FormulaConfig& f)
{
    return baseHP + static_cast<int>(f.hp.scale) * end + static_cast<int>(f.hp.level_scale) * level;
}

// Compute next-level XP threshold:
//   xpToNext = xp_base * (level + xp_offset) ^ xp_exponent
// The offset flattens early levels (beginner gains) while the exponent
// drives aggressive late-game scaling (diminishing neural adaptation).
static int deriveXpToNext(int level, const FormulaConfig& f)
{
    return static_cast<int>(
        f.leveling.xp_base *
        std::pow(static_cast<float>(level) + f.leveling.xp_offset, f.leveling.xp_exponent));
}

void LevelingSystem::deriveHealth(EntityManager& em, entt::entity entity)
{
    auto& reg = em.registry();
    if (!reg.all_of<Stats>(entity))
        return;

    const FormulaConfig& f = reg.ctx().get<FormulaConfig>();
    const int baseHP = resolveBaseHP(reg, entity, f);
    const int level = resolveLevel(reg, entity);
    const int maxHP = deriveMaxHP(baseHP, reg.get<Stats>(entity).end, level, f);
    if (reg.all_of<Health>(entity))
    {
        auto& health = reg.get<Health>(entity);
        health.max = maxHP;
        health.current = maxHP;
    }
    else
    {
        reg.emplace<Health>(entity, Health{maxHP, maxHP});
    }
}

void LevelingSystem::applyInitialDerivations(EntityManager& em)
{
    auto& reg = em.registry();
    const FormulaConfig& f = reg.ctx().get<FormulaConfig>();

    // Derive Health for every entity that has Stats.
    // Walls and non-stat entities are unaffected (they have no Stats component).
    for (auto [entity, stats] : reg.view<Stats>().each())
    {
        // Enemy difficulty scalar: multiply all base stats by tier before deriving HP.
        // tier=1 (default) is identity -- no change to existing configs.
        if (reg.all_of<AIController>(entity))
        {
            const int lvl = reg.get<AIController>(entity).tier;
            if (lvl > 1)
            {
                stats.str *= lvl;
                stats.dex *= lvl;
                stats.end *= lvl;
                stats.lck *= lvl;
            }
        }

        const int baseHP = resolveBaseHP(reg, entity, f);
        const int level = resolveLevel(reg, entity);
        const int maxHP = deriveMaxHP(baseHP, stats.end, level, f);

        if (reg.all_of<Health>(entity))
        {
            auto& health = reg.get<Health>(entity);
            health.max = maxHP;
            health.current = maxHP;
        }
        else
        {
            reg.emplace<Health>(entity, Health{maxHP, maxHP});
        }

        // Initialise the XP threshold for entities that carry Experience.
        if (reg.all_of<Experience>(entity))
        {
            auto& exp = reg.get<Experience>(entity);
            exp.xp_to_next = deriveXpToNext(exp.level, f);
        }

        // poise.max = floor(END * end_scale + STR * str_scale).
        // Flat bonuses from armor/shields are added on top (not yet implemented).
        if (reg.all_of<Poise>(entity))
        {
            auto& poise = reg.get<Poise>(entity);
            poise.max = std::floor(static_cast<float>(stats.end) * f.poise.end_scale +
                                   static_cast<float>(stats.str) * f.poise.str_scale);
        }

        // Stamina pool: max = base + end_scale * log(END + 1). Starts full.
        if (reg.all_of<Stamina>(entity))
        {
            auto& sta = reg.get<Stamina>(entity);
            sta.max_stamina = f.stamina.base +
                              f.stamina.end_scale * std::log(static_cast<float>(stats.end) + 1.0f);
            sta.current = sta.max_stamina;
        }
    }
}

void allocateStat(entt::registry& reg, entt::entity entity, int& stat, const FormulaConfig& f,
                  const SoundConfig& snd)
{
    stat++;
    auto& exp = reg.get<Experience>(entity);
    exp.stat_points--;
    TracyMessageL("StatAllocated");
    AudioSystem::playSfx(snd.stat_allocate.path, snd.stat_allocate.volume);

    auto& stats = reg.get<Stats>(entity);

    if (reg.all_of<Health>(entity))
    {
        auto& health = reg.get<Health>(entity);
        const int baseHP = resolveBaseHP(reg, entity, f);
        const int level = resolveLevel(reg, entity);
        const int newMax = deriveMaxHP(baseHP, stats.end, level, f);
        const int delta = newMax - health.max;
        health.max = newMax;
        health.current = std::min(health.current + delta, health.max);
    }

    if (reg.all_of<Stamina>(entity))
    {
        auto& sta = reg.get<Stamina>(entity);
        const float newMax =
            f.stamina.base + f.stamina.end_scale * std::log(static_cast<float>(stats.end) + 1.0f);
        const float delta = newMax - sta.max_stamina;
        sta.max_stamina = newMax;
        sta.current = std::min(sta.current + delta, sta.max_stamina);
    }

    if (reg.all_of<Poise>(entity))
    {
        auto& p = reg.get<Poise>(entity);
        p.max = std::floor(static_cast<float>(stats.end) * f.poise.end_scale +
                           static_cast<float>(stats.str) * f.poise.str_scale);
    }
}

void LevelingSystem::deriveInitialStats(EntityManager& em, entt::entity entity)
{
    auto& reg = em.registry();
    if (!reg.all_of<Stats>(entity))
        return;

    const FormulaConfig& f = reg.ctx().get<FormulaConfig>();
    const auto& stats = reg.get<Stats>(entity);

    // Health
    const int baseHP = resolveBaseHP(reg, entity, f);
    const int level = resolveLevel(reg, entity);
    const int maxHP = deriveMaxHP(baseHP, stats.end, level, f);
    if (reg.all_of<Health>(entity))
    {
        auto& health = reg.get<Health>(entity);
        health.max = maxHP;
        health.current = maxHP;
    }
    else
    {
        reg.emplace<Health>(entity, Health{maxHP, maxHP});
    }

    // Poise
    if (reg.all_of<Poise>(entity))
    {
        auto& poise = reg.get<Poise>(entity);
        poise.max = std::floor(static_cast<float>(stats.end) * f.poise.end_scale +
                               static_cast<float>(stats.str) * f.poise.str_scale);
    }

    // Stamina
    if (reg.all_of<Stamina>(entity))
    {
        auto& sta = reg.get<Stamina>(entity);
        sta.max_stamina =
            f.stamina.base + f.stamina.end_scale * std::log(static_cast<float>(stats.end) + 1.0f);
        sta.current = sta.max_stamina;
    }
}

void LevelingSystem::update(EntityManager& em)
{
    ZoneScopedN("LevelingSystem");
    auto& reg = em.registry();
    const FormulaConfig& f = reg.ctx().get<FormulaConfig>();
    const SoundConfig& snd = reg.ctx().get<SoundConfig>();

    // --- XP overflow / level-up -------------------------------------------
    for (auto [entity, exp] : reg.view<Experience>().each())
    {
        while (exp.current_xp >= exp.xp_to_next)
        {
            exp.current_xp -= exp.xp_to_next;
            exp.level++;
            exp.stat_points += static_cast<int>(f.leveling.points_per_level);
            exp.xp_to_next = deriveXpToNext(exp.level, f);

            TracyMessageL("LevelUp");
            AudioSystem::playSfx(snd.level_up.path, snd.level_up.volume);
            if (reg.all_of<Transform>(entity))
            {
                const auto& t = reg.get<Transform>(entity);
                ParticleSystem::spawnEmberBurst(em, t.x, t.y, 5);
            }
        }
    }

    // --- Stat allocation (PlayerActions.alloc_str/Dex/End/Lck) ---------------
    for (auto [entity, actions, stats, exp] : reg.view<PlayerActions, Stats, Experience>().each())
    {
        if (exp.stat_points <= 0)
        {
            if (actions.alloc_str || actions.alloc_dex || actions.alloc_end || actions.alloc_lck)
            {
                actions.alloc_str = false;
                actions.alloc_dex = false;
                actions.alloc_end = false;
                actions.alloc_lck = false;
            }
            continue;
        }

        if (actions.alloc_str)
        {
            allocateStat(reg, entity, stats.str, f, snd);
            actions.alloc_str = false;
        }
        if (actions.alloc_dex)
        {
            allocateStat(reg, entity, stats.dex, f, snd);
            actions.alloc_dex = false;
        }
        if (actions.alloc_end)
        {
            allocateStat(reg, entity, stats.end, f, snd);
            actions.alloc_end = false;
        }
        if (actions.alloc_lck)
        {
            allocateStat(reg, entity, stats.lck, f, snd);
            actions.alloc_lck = false;
        }
    }
}