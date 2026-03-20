#include "systems/LevelingSystem.h"

#include "ecs/Components.h"
#include "ecs/GameComponents.h"
#include "ecs/GameConfig.h"
#include "systems/AudioSystem.h"
#include "systems/ParticleSystem.h"

#include <cmath>
#include <iostream>

// Compute max HP from END stat using the formula:
//   maxHP = base + floor(scale * log(END + 1))
static int deriveMaxHP(int end, const FormulaConfig& f)
{
    return static_cast<int>(f.hp.base +
                            std::floor(f.hp.scale * std::log(static_cast<float>(end) + 1.0f)));
}

// Compute next-level XP threshold:
//   xpToNext = xp_base * level ^ xp_exponent
static int deriveXpToNext(int level, const FormulaConfig& f)
{
    return static_cast<int>(f.leveling.xp_base *
                            std::pow(static_cast<float>(level), f.leveling.xp_exponent));
}

void LevelingSystem::deriveHealth(EntityManager& em, entt::entity entity)
{
    auto& reg = em.registry();
    if (!reg.all_of<Stats>(entity))
        return;

    const FormulaConfig& f = reg.ctx().get<FormulaConfig>();
    const int maxHP = deriveMaxHP(reg.get<Stats>(entity).end, f);
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
        // tier=1 (default) is identity — no change to existing configs.
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

        const int maxHP = deriveMaxHP(stats.end, f);

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

void LevelingSystem::update(EntityManager& em)
{
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

            AudioSystem::playSfx(snd.level_up.path, snd.level_up.volume);
            if (reg.all_of<Transform>(entity))
            {
                const auto& t = reg.get<Transform>(entity);
                ParticleSystem::spawnEmberBurst(em, t.x, t.y, 5);
            }
            std::cout << "[LevelingSystem] Level up! Now level " << exp.level << ". Next level at "
                      << exp.xp_to_next << " XP.\n";

            if (reg.all_of<Stats>(entity))
            {
                const auto& s = reg.get<Stats>(entity);
                std::cout << "  Current stats:  STR " << s.str << "  DEX " << s.dex << "  END "
                          << s.end << "  LCK " << s.lck << "\n"
                          << "  Choose a stat to upgrade: [1] STR  [2] DEX  [3] END  [4] LCK\n";
            }
            else
            {
                std::cout << "  Choose a stat to upgrade: [1] STR  [2] DEX  [3] END  [4] LCK\n";
            }
        }
    }

    // --- Debug stat allocation (PlayerActions.alloc_str/Dex/End/Lck) ---------------
    for (auto [entity, actions, stats, exp] : reg.view<PlayerActions, Stats, Experience>().each())
    {
        if (exp.stat_points <= 0)
        {
            if (actions.alloc_str || actions.alloc_dex || actions.alloc_end || actions.alloc_lck)
            {
                std::cout << "[LevelingSystem] No stat points available.\n";
                actions.alloc_str = false;
                actions.alloc_dex = false;
                actions.alloc_end = false;
                actions.alloc_lck = false;
            }
            continue;
        }

        // Extract local refs — C++17 lambdas cannot capture structured bindings directly.
        auto& expRef = exp;
        auto& statsRef = stats;
        entt::entity ent = entity;

        auto allocate = [&](int& stat, const char* name)
        {
            stat++;
            expRef.stat_points--;
            AudioSystem::playSfx(snd.stat_allocate.path, snd.stat_allocate.volume);

            // Recalculate HP if END changed.
            if (reg.all_of<Health>(ent))
            {
                auto& health = reg.get<Health>(ent);
                const int newMax = deriveMaxHP(statsRef.end, f);
                const int delta = newMax - health.max;
                health.max = newMax;
                health.current = std::min(health.current + delta, health.max);
            }

            // Recalculate stamina pool if END changed.
            if (reg.all_of<Stamina>(ent))
            {
                auto& sta = reg.get<Stamina>(ent);
                const float newMax =
                    f.stamina.base +
                    f.stamina.end_scale * std::log(static_cast<float>(statsRef.end) + 1.0f);
                const float delta = newMax - sta.max_stamina;
                sta.max_stamina = newMax;
                sta.current = std::min(sta.current + delta, sta.max_stamina);
            }

            // Recalculate poise threshold from STR + END.
            if (reg.all_of<Poise>(ent))
            {
                auto& p = reg.get<Poise>(ent);
                p.max = std::floor(static_cast<float>(statsRef.end) * f.poise.end_scale +
                                   static_cast<float>(statsRef.str) * f.poise.str_scale);
            }

            const int hp_cur = reg.all_of<Health>(ent) ? reg.get<Health>(ent).current : 0;
            const int hp_max = reg.all_of<Health>(ent) ? reg.get<Health>(ent).max : 0;
            std::cout << "[Stats] " << name << " is now " << stat
                      << "  (pts left: " << expRef.stat_points << ")\n"
                      << "  STR " << statsRef.str << "  DEX " << statsRef.dex << "  END "
                      << statsRef.end << "  LCK " << statsRef.lck << "  |  HP " << hp_cur << "/"
                      << hp_max << "\n";
        };

        if (actions.alloc_str)
        {
            allocate(stats.str, "STR");
            actions.alloc_str = false;
        }
        if (actions.alloc_dex)
        {
            allocate(stats.dex, "DEX");
            actions.alloc_dex = false;
        }
        if (actions.alloc_end)
        {
            allocate(stats.end, "END");
            actions.alloc_end = false;
        }
        if (actions.alloc_lck)
        {
            allocate(stats.lck, "LCK");
            actions.alloc_lck = false;
        }
    }
}
