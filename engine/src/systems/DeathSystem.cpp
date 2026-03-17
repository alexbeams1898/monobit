#include "systems/DeathSystem.h"

#include "ecs/Components.h"

#include <iostream>
#include <tracy/Tracy.hpp>
#include <vector>

void DeathSystem::update(EntityManager& em)
{
    ZoneScopedN("DeathSystem");
    auto& reg = em.registry();

    // Collect all dead entities before destroying any (entt iterator safety).
    struct DeadEntry
    {
        entt::entity entity;
        bool is_player;
        float tx;
        float ty;
        int enemy_level; // (str+dex+end+lck) - 3: all-1s entity = level 1
        int xp_drop;
    };

    std::vector<DeadEntry> dead;
    for (auto [entity] : reg.view<Dead>().each())
    {
        const bool is_player = reg.all_of<Input>(entity);
        float tx = 0.0f;
        float ty = 0.0f;
        if (reg.all_of<Transform>(entity))
        {
            const auto& t = reg.get<Transform>(entity);
            tx = t.x;
            ty = t.y;
        }
        // Entity level = (str+dex+end+lck) - 3.
        // A baseline enemy with 1 in every stat is level 1.
        // Each stat point above 1 adds 1 to the effective level.
        int enemy_level = 1;
        if (reg.all_of<Stats>(entity))
        {
            const auto& s = reg.get<Stats>(entity);
            const int derived = s.str + s.dex + s.end + s.lck - 3;
            enemy_level = derived > 0 ? derived : 1;
        }
        int xp_drop = 20;
        if (reg.all_of<Loot>(entity))
            xp_drop = reg.get<Loot>(entity).xp_drop;

        dead.push_back({entity, is_player, tx, ty, enemy_level, xp_drop});
    }

    for (const auto& entry : dead)
    {
        if (entry.is_player)
        {
            std::cout << "[DeathSystem] Game Over. Press ESC to quit.\n";
            // Don't destroy the player — let them persist so ESC works.
            // Remove Dead tag so the log doesn't repeat every frame.
            reg.remove<Dead>(entry.entity);
            // Zero health so they can't act, but don't destroy the entity.
            if (reg.all_of<Health>(entry.entity))
                reg.get<Health>(entry.entity).current = 0;
        }
        else
        {
            // XP = xp_drop * enemy_level
            // enemy_level = stat_sum - 3 (floored at 1).
            // Base enemy (all 1s) → level 1 → xp_drop XP.
            // CO (2+3+1+1=7) → level 4 → 4× xp_drop.
            const int xpValue = entry.xp_drop * entry.enemy_level;

            const auto pickup = em.create();
            reg.emplace<Transform>(pickup, Transform{entry.tx, entry.ty, 0.0f, 1.0f});
            reg.emplace<Pickup>(pickup, Pickup{xpValue, 0, 48.0f});
            reg.emplace<Tag>(pickup, Tag{"xp_pickup"});

            std::cout << "[DeathSystem] Enemy died (lvl " << entry.enemy_level << ") - spawned "
                      << xpValue << " XP pickup.\n";

            em.destroy(entry.entity);
        }
    }
}
