#include "systems/DeathSystem.h"

#include "ecs/Components.h"
#include "systems/AudioSystem.h"

#include <iostream>
#include <tracy/Tracy.hpp>
#include <vector>

// Spawn XP pickup, cascade-destroy body-part children, then destroy entity.
static void processEnemyDeath(EntityManager& em, entt::entity entity, float tx, float ty,
                              int xp_drop, int enemy_level)
{
    auto& reg = em.registry();
    const int xpValue = xp_drop * enemy_level;

    const auto pickup = em.create();
    reg.emplace<Transform>(pickup, Transform{tx, ty, 0.0f, 1.0f});
    reg.emplace<Pickup>(pickup, Pickup{xpValue, 0, 48.0f});
    reg.emplace<Tag>(pickup, Tag{"xp_pickup"});

    AudioSystem::playSfx(em.sounds.death.path, em.sounds.death.volume);
    std::cout << "[DeathSystem] Enemy died (lvl " << enemy_level << ") - spawned " << xpValue
              << " XP pickup.\n";

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

    // Tick death timers first — entities waiting for death animation to finish.
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
        float tx;
        float ty;
        int enemy_level; // (str+dex+end+lck) - 3: all-1s entity = level 1
        int xp_drop;
    };

    std::vector<DeadEntry> dead;
    for (auto [entity, deadComp] : reg.view<Dead>().each())
    {
        // Wait for death animation to finish.
        if (deadComp.timer > 0.0f)
            continue;

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
            reg.remove<Dead>(entry.entity);
            if (reg.all_of<Health>(entry.entity))
                reg.get<Health>(entry.entity).current = 0;
        }
        else
        {
            processEnemyDeath(em, entry.entity, entry.tx, entry.ty, entry.xp_drop,
                              entry.enemy_level);
        }
    }
}
