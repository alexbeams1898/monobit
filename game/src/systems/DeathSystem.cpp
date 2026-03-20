#include "systems/DeathSystem.h"

#include "ecs/Components.h"
#include "ecs/GameComponents.h"
#include "ecs/GameConfig.h"
#include "systems/AudioSystem.h"

#include <cmath>
#include <iostream>
#include <tracy/Tracy.hpp>
#include <vector>

// XP = base * (1 + log_scale * ln(enemy_level)) * max(min_fraction, enemy_level/player_level)
// Level multiplier scales down XP when the player outlevels the enemy.
// Log scaling: level 1 = base, level 5 ~ 2x, level 10 ~ 2.4x, level 20 ~ 2.8x.
static int computeXpDrop(int base_xp, int enemy_level, int player_level, float log_scale,
                         float min_fraction)
{
    const float scale = 1.0f + log_scale * std::log(static_cast<float>(enemy_level));
    const float level_mult =
        std::max(min_fraction, static_cast<float>(enemy_level) / static_cast<float>(player_level));
    return static_cast<int>(std::floor(static_cast<float>(base_xp) * scale * level_mult));
}

// Grant XP directly to the player, cascade-destroy body-part children, then
// destroy entity.
static void processEnemyDeath(EntityManager& em, entt::entity entity, const Loot& loot,
                              const std::string& name)
{
    auto& reg = em.registry();
    const FormulaConfig& f = reg.ctx().get<FormulaConfig>();

    int playerLevel = 1;
    for (auto pe : reg.view<PlayerActions>())
    {
        if (reg.all_of<Experience>(pe))
            playerLevel = reg.get<Experience>(pe).level;
        break;
    }

    const int xpValue = computeXpDrop(loot.xp_drop, loot.level, playerLevel, f.xp_drop.log_scale,
                                      f.xp_drop.min_fraction);

    // Grant XP directly to the player.
    for (auto pe : reg.view<PlayerActions>())
    {
        if (reg.all_of<Experience>(pe))
        {
            reg.get<Experience>(pe).current_xp += xpValue;
            break;
        }
    }

    // Log kill with stats + essence for balance visibility.
    std::cout << "[DeathSystem] " << name << " killed (Lv" << loot.level << ")";
    if (reg.all_of<Stats>(entity))
    {
        const auto& s = reg.get<Stats>(entity);
        std::cout << "  STR=" << s.str << " DEX=" << s.dex << " END=" << s.end << " LCK=" << s.lck;
        if (reg.all_of<Essence>(entity))
        {
            const auto& e = reg.get<Essence>(entity);
            std::cout << "  ess[" << e.str << "," << e.dex << "," << e.end << "," << e.lck << "]";
        }
    }
    std::cout << "  -> +" << xpValue << " XP\n";

    std::vector<entt::entity> children;
    for (auto [child, bp] : reg.view<BodyPart>().each())
        if (bp.parent == entity)
            children.push_back(child);
    for (auto child : children)
        em.destroy(child);

    em.destroy(entity);
}

void DeathSystem::update(EntityManager& em, double dt)
{
    ZoneScopedN("DeathSystem");
    auto& reg = em.registry();
    const SoundConfig& snd = reg.ctx().get<SoundConfig>();
    auto& waveState = reg.ctx().get<WaveState>();

    // Tick death timers first -- entities waiting for death animation to finish.
    for (auto [entity, dead] : reg.view<Dead>().each())
    {
        if (dead.timer > 0.0f)
            dead.timer -= static_cast<float>(dt);
    }

    // Collect all dead entities whose timer has expired before destroying any
    // (entt iterator safety).
    struct DeadEntry
    {
        entt::entity entity;
        bool is_player;
        Loot loot;
        std::string name;
    };

    std::vector<DeadEntry> dead;
    for (auto [entity, deadComp] : reg.view<Dead>().each())
    {
        // Wait for death animation to finish.
        if (deadComp.timer > 0.0f)
            continue;

        const bool is_player = reg.all_of<PlayerActions>(entity);
        Loot loot;
        if (reg.all_of<Loot>(entity))
            loot = reg.get<Loot>(entity);

        std::string name = "???";
        if (reg.all_of<Tag>(entity))
            name = reg.get<Tag>(entity).name;

        dead.push_back({entity, is_player, loot, std::move(name)});
    }

    for (const auto& entry : dead)
    {
        if (entry.is_player)
        {
            AudioSystem::playSfx(snd.game_over.path, snd.game_over.volume);
            std::cout << "[DeathSystem] Game Over. Press R to restart.\n";

            // Destroy body-part children so the player sprite disappears.
            // Parent stays alive for GameOver state and restart.
            std::vector<entt::entity> children;
            for (auto [child, bp] : reg.view<BodyPart>().each())
                if (bp.parent == entry.entity)
                    children.push_back(child);
            for (auto child : children)
                em.destroy(child);

            reg.remove<Dead>(entry.entity);
            if (reg.all_of<Health>(entry.entity))
                reg.get<Health>(entry.entity).current = 0;
            waveState.phase = WaveState::Phase::GameOver;
        }
        else
        {
            processEnemyDeath(em, entry.entity, entry.loot, entry.name);
        }
    }
}
