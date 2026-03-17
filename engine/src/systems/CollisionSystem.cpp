#include "systems/CollisionSystem.h"

#include "TileMap.h"
#include "ecs/Components.h"

#include <cmath>
#include <tracy/Tracy.hpp>
#include <vector>

void CollisionSystem::update(EntityManager& em)
{
    ZoneScopedN("CollisionSystem");
    em.clearCollisionEvents();

    auto view = em.registry().view<Transform, Collider>();

    // Split entities into two buckets so we never iterate static-static pairs.
    // With a tile-map world (~4000 wall entities removed from ECS), this list
    // contains only dynamic entities and any future non-tile ECS collidables
    // (doors, pressure plates, etc.). Static wall data comes from the tile map.
    std::vector<entt::entity> dynamics; // have Velocity
    std::vector<entt::entity> statics;  // no Velocity (ECS collidables only)
    for (auto e : view)
    {
        if (em.registry().all_of<Velocity>(e))
            dynamics.push_back(e);
        else
            statics.push_back(e);
    }

    // Helper: AABB overlap test + optional MTV resolution.
    auto check_pair = [&](entt::entity ea, entt::entity eb, bool resolve_both)
    {
        auto& ta = view.get<Transform>(ea);
        const auto& ca = view.get<Collider>(ea);
        auto& tb = view.get<Transform>(eb);
        const auto& cb = view.get<Collider>(eb);

        const float dx = ta.x - tb.x;
        const float dy = ta.y - tb.y;
        const float overlapX = (ca.width + cb.width) * 0.5f - std::abs(dx);
        const float overlapY = (ca.height + cb.height) * 0.5f - std::abs(dy);

        if (overlapX <= 0.0f || overlapY <= 0.0f)
            return;

        em.collision_events.push_back({ea, eb});

        if (!resolve_both)
            return;
        if (!ca.is_solid || !cb.is_solid)
            return;

        // Both dynamic and both solid: split MTV evenly.
        float pushX = 0.0f;
        float pushY = 0.0f;
        if (overlapX < overlapY)
            pushX = (dx >= 0.0f) ? overlapX : -overlapX;
        else
            pushY = (dy >= 0.0f) ? overlapY : -overlapY;

        ta.x += pushX * 0.5f;
        ta.y += pushY * 0.5f;
        tb.x -= pushX * 0.5f;
        tb.y -= pushY * 0.5f;
    };

    // Dynamic-vs-dynamic pairs: resolve with split MTV.
    for (size_t i = 0; i < dynamics.size(); ++i)
        for (size_t j = i + 1; j < dynamics.size(); ++j)
            check_pair(dynamics[i], dynamics[j], true);

    // Dynamic-vs-ECS-static pairs: emit event only (for future non-tile collidables
    // such as doors or trigger zones; wall tiles are handled via tile map below).
    for (auto dyn : dynamics)
        for (auto sta : statics)
            check_pair(dyn, sta, false);

    // Static depenetration pass — correct any dynamic entity pushed into a
    // solid wall as a side effect of the dynamic-vs-dynamic resolution above.
    //
    // MovementSystem's velocity projection prevents dynamic entities from
    // entering static solids on their own. However, when two dynamics collide,
    // CollisionSystem moves them directly (bypassing MovementSystem). If a wall
    // is behind either entity the push can land them inside it; MovementSystem
    // then sees a pre-existing overlap and zeroes velocity every frame — entity
    // is permanently stuck.
    //
    // Tile map path: query the 2-3 tile neighborhood around each dynamic entity
    // — O(~4 lookups) per entity regardless of map size.
    // ECS fallback: used in unit tests that set up explicit wall entities without
    // a tile map.
    if (em.tile_map.valid())
    {
        const float ts = static_cast<float>(TileMap::TILE_SIZE);
        const float tile_half = static_cast<float>(TileMap::TILE_SIZE) * 0.5f;

        for (auto ea : dynamics)
        {
            const auto& ca = view.get<Collider>(ea);
            if (!ca.is_solid)
                continue;
            auto& ta = view.get<Transform>(ea);

            const float hw = ca.width * 0.5f;
            const float hh = ca.height * 0.5f;
            const int col_min = static_cast<int>(std::floor((ta.x - hw) / ts));
            const int col_max = static_cast<int>(std::floor((ta.x + hw) / ts));
            const int row_min = static_cast<int>(std::floor((ta.y - hh) / ts));
            const int row_max = static_cast<int>(std::floor((ta.y + hh) / ts));

            for (int r = row_min; r <= row_max; ++r)
            {
                for (int c = col_min; c <= col_max; ++c)
                {
                    if (!em.tile_map.in_bounds(c, r) || em.tile_map.at(c, r).walkable)
                        continue;

                    const float tile_cx =
                        static_cast<float>(c * TileMap::TILE_SIZE + TileMap::TILE_SIZE / 2);
                    const float tile_cy =
                        static_cast<float>(r * TileMap::TILE_SIZE + TileMap::TILE_SIZE / 2);
                    const float dx = ta.x - tile_cx;
                    const float dy = ta.y - tile_cy;
                    const float overlapX = hw + tile_half - std::abs(dx);
                    const float overlapY = hh + tile_half - std::abs(dy);

                    if (overlapX <= 0.0f || overlapY <= 0.0f)
                        continue;

                    if (overlapX < overlapY)
                        ta.x += (dx >= 0.0f) ? overlapX : -overlapX;
                    else
                        ta.y += (dy >= 0.0f) ? overlapY : -overlapY;
                }
            }
        }
    }
    else
    {
        // ECS fallback — used in unit tests without a tile map.
        for (auto ea : dynamics)
        {
            if (!em.registry().all_of<Velocity>(ea))
                continue;
            const auto& ca = view.get<Collider>(ea);
            if (!ca.is_solid)
                continue;
            auto& ta = view.get<Transform>(ea);

            for (auto eb : statics)
            {
                const auto& cb = view.get<Collider>(eb);
                if (!cb.is_solid)
                    continue;
                const auto& tb = view.get<Transform>(eb);

                const float dx = ta.x - tb.x;
                const float dy = ta.y - tb.y;
                const float overlapX = (ca.width + cb.width) * 0.5f - std::abs(dx);
                const float overlapY = (ca.height + cb.height) * 0.5f - std::abs(dy);

                if (overlapX <= 0.0f || overlapY <= 0.0f)
                    continue;

                if (overlapX < overlapY)
                    ta.x += (dx >= 0.0f) ? overlapX : -overlapX;
                else
                    ta.y += (dy >= 0.0f) ? overlapY : -overlapY;
            }
        }
    }
}
