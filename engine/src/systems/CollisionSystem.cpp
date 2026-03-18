#include "systems/CollisionSystem.h"

#include "TileMap.h"
#include "ecs/Components.h"

#include <cmath>
#include <tracy/Tracy.hpp>
#include <vector>

// ---------------------------------------------------------------------------
// Internal helpers — static free functions to keep update() readable.
// ---------------------------------------------------------------------------

static void checkAndResolvePair(EntityManager& em, entt::entity ea, Transform& ta,
                                const Collider& ca, entt::entity eb, Transform& tb,
                                const Collider& cb, bool resolve_both)
{
    const float dx = ta.x - tb.x;
    const float dy = ta.y - tb.y;
    const float overlapX = (ca.width + cb.width) * 0.5f - std::abs(dx);
    const float overlapY = (ca.height + cb.height) * 0.5f - std::abs(dy);

    if (overlapX <= 0.0f || overlapY <= 0.0f)
        return;

    em.collision_events.push_back({ea, eb});

    if (!resolve_both || !ca.is_solid || !cb.is_solid)
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
}

// Depenetrate entity against solid wall tiles in the tile map.
static void depenetrateVsTileMap(const TileMap& tile_map, Transform& ta, const Collider& ca)
{
    if (!ca.is_solid)
        return;

    const float ts = static_cast<float>(TileMap::TILE_SIZE);
    const float tile_half = static_cast<float>(TileMap::TILE_SIZE) * 0.5f;
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
            if (!tile_map.in_bounds(c, r) || tile_map.at(c, r).walkable)
                continue;

            const float tile_cx = static_cast<float>(c * TileMap::TILE_SIZE) + tile_half;
            const float tile_cy = static_cast<float>(r * TileMap::TILE_SIZE) + tile_half;
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

// Depenetrate entity against ECS static solids (unit-test fallback — no tile map).
static void depenetrateVsECSStatics(EntityManager& em, entt::entity ea,
                                    const std::vector<entt::entity>& statics)
{
    const auto& ca = em.registry().get<Collider>(ea);
    if (!ca.is_solid)
        return;
    auto& ta = em.registry().get<Transform>(ea);

    for (auto eb : statics)
    {
        const auto& cb = em.registry().get<Collider>(eb);
        if (!cb.is_solid)
            continue;
        const auto& tb = em.registry().get<Transform>(eb);

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

// ---------------------------------------------------------------------------
// Public API
// ---------------------------------------------------------------------------

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

    // Static-vs-static pairs: emit event only — neither entity can move.
    // Covers trigger zones, doors, and unit-test wall entities.
    // O(n²) on the statics bucket — acceptable because n is provably tiny:
    // wall tiles live in the tile map (not ECS), so statics only ever contains
    // non-tile objects like rest spots, doors, or trigger zones (~1–10 entities).
    for (size_t i = 0; i < statics.size(); ++i)
        for (size_t j = i + 1; j < statics.size(); ++j)
            checkAndResolvePair(
                em, statics[i], view.get<Transform>(statics[i]), view.get<Collider>(statics[i]),
                statics[j], view.get<Transform>(statics[j]), view.get<Collider>(statics[j]), false);

    // Dynamic-vs-ECS-static pairs: emit event only (doors, trigger zones).
    // Must run BEFORE dynamic-vs-dynamic so we check initial positions.  If it ran
    // after, the dyn-vs-dyn push can land a dynamic inside a static — generating a
    // phantom wall event that the depenetration pass then silently corrects.
    for (auto dyn : dynamics)
        for (auto sta : statics)
            checkAndResolvePair(em, dyn, view.get<Transform>(dyn), view.get<Collider>(dyn), sta,
                                view.get<Transform>(sta), view.get<Collider>(sta), false);

    // Dynamic-vs-dynamic pairs: resolve with split MTV.
    for (size_t i = 0; i < dynamics.size(); ++i)
        for (size_t j = i + 1; j < dynamics.size(); ++j)
            checkAndResolvePair(em, dynamics[i], view.get<Transform>(dynamics[i]),
                                view.get<Collider>(dynamics[i]), dynamics[j],
                                view.get<Transform>(dynamics[j]), view.get<Collider>(dynamics[j]),
                                true);

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
        for (auto ea : dynamics)
            depenetrateVsTileMap(em.tile_map, view.get<Transform>(ea), view.get<Collider>(ea));
    }
    else
    {
        for (auto ea : dynamics)
            depenetrateVsECSStatics(em, ea, statics);
    }
}
