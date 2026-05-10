#include "systems/CollisionSystem.h"

#include "TileMap.h"
#include "ecs/Components.h"

#include <tracy/Tracy.hpp>

#include <algorithm>
#include <cmath>
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

    // Same non-zero collision group: soft overlap (no MTV push).
    if (ca.collision_group != 0 && ca.collision_group == cb.collision_group)
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
// Broad-phase spatial grid for dynamic-vs-dynamic pair pruning.
// Two-pass (count + scatter) into a flat array avoids per-cell heap
// allocation. Vectors persist across frames via static local in update().
// ---------------------------------------------------------------------------
namespace
{

struct SpatialGrid
{
    static constexpr float CELL_SIZE = 64.0f;
    static constexpr float INV_CELL_SIZE = 1.0f / CELL_SIZE;

    int cols = 0;
    int rows = 0;

    std::vector<int> cell_counts;
    std::vector<int> offsets;
    std::vector<int> entries;

    struct CellRange
    {
        int col_min = 0;
        int col_max = 0;
        int row_min = 0;
        int row_max = 0;
    };
    std::vector<CellRange> ranges;

    size_t numCells() const
    {
        return static_cast<size_t>(cols) * static_cast<size_t>(rows);
    }

    void init(float world_w, float world_h)
    {
        cols = static_cast<int>(world_w * INV_CELL_SIZE) + 1;
        rows = static_cast<int>(world_h * INV_CELL_SIZE) + 1;
        cell_counts.assign(numCells(), 0);
        offsets.resize(numCells());
    }

    void countPass(const std::vector<entt::entity>& dynamics, const entt::registry& reg)
    {
        ranges.resize(dynamics.size());
        for (size_t i = 0; i < dynamics.size(); ++i)
        {
            const auto& t = reg.get<Transform>(dynamics[i]);
            const auto& c = reg.get<Collider>(dynamics[i]);
            const float hw = c.width * 0.5f;
            const float hh = c.height * 0.5f;

            auto& r = ranges[i];
            r.col_min = std::max(0, static_cast<int>((t.x - hw) * INV_CELL_SIZE));
            r.col_max = std::min(cols - 1, static_cast<int>((t.x + hw) * INV_CELL_SIZE));
            r.row_min = std::max(0, static_cast<int>((t.y - hh) * INV_CELL_SIZE));
            r.row_max = std::min(rows - 1, static_cast<int>((t.y + hh) * INV_CELL_SIZE));

            for (int row = r.row_min; row <= r.row_max; ++row)
                for (int col = r.col_min; col <= r.col_max; ++col)
                    ++cell_counts[static_cast<size_t>(row) * static_cast<size_t>(cols) +
                                  static_cast<size_t>(col)];
        }
    }

    void buildPass()
    {
        const size_t nc = numCells();
        int total = 0;
        for (size_t i = 0; i < nc; ++i)
        {
            offsets[i] = total;
            total += cell_counts[i];
        }

        entries.resize(static_cast<size_t>(total));

        // Reset counts to use as write cursors.
        for (size_t i = 0; i < nc; ++i)
            cell_counts[i] = 0;

        for (size_t i = 0; i < ranges.size(); ++i)
        {
            const auto& r = ranges[i];
            for (int row = r.row_min; row <= r.row_max; ++row)
            {
                for (int col = r.col_min; col <= r.col_max; ++col)
                {
                    const size_t cell = static_cast<size_t>(row) * static_cast<size_t>(cols) +
                                        static_cast<size_t>(col);
                    entries[static_cast<size_t>(offsets[cell]) +
                            static_cast<size_t>(cell_counts[cell])] = static_cast<int>(i);
                    ++cell_counts[cell];
                }
            }
        }
    }
};

} // anonymous namespace

// Broad-phase grid query: test only pairs sharing a grid cell.
static void resolveDynVsDyn(EntityManager& em, const std::vector<entt::entity>& dynamics,
                            SpatialGrid& grid)
{
    auto view = em.registry().view<Transform, Collider>();
    const size_t nc = grid.numCells();
    for (size_t cell = 0; cell < nc; ++cell)
    {
        const int start = grid.offsets[cell];
        const int count = grid.cell_counts[cell];
        if (count < 2)
            continue;

        for (int a = start; a < start + count; ++a)
        {
            const int idx_i = grid.entries[static_cast<size_t>(a)];
            for (int b = a + 1; b < start + count; ++b)
            {
                const int idx_j = grid.entries[static_cast<size_t>(b)];

                // Deduplicate: only process pair in its first shared cell.
                const auto& ri = grid.ranges[static_cast<size_t>(idx_i)];
                const auto& rj = grid.ranges[static_cast<size_t>(idx_j)];
                const size_t shared = static_cast<size_t>(std::max(ri.row_min, rj.row_min)) *
                                          static_cast<size_t>(grid.cols) +
                                      static_cast<size_t>(std::max(ri.col_min, rj.col_min));
                if (cell != shared)
                    continue;

                checkAndResolvePair(em, dynamics[static_cast<size_t>(idx_i)],
                                    view.get<Transform>(dynamics[static_cast<size_t>(idx_i)]),
                                    view.get<Collider>(dynamics[static_cast<size_t>(idx_i)]),
                                    dynamics[static_cast<size_t>(idx_j)],
                                    view.get<Transform>(dynamics[static_cast<size_t>(idx_j)]),
                                    view.get<Collider>(dynamics[static_cast<size_t>(idx_j)]), true);
            }
        }
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

    // Dynamic-vs-dynamic pairs: grid-accelerated broad phase + MTV.
    if (dynamics.size() > 1)
    {
        float world_w = 0.0f;
        float world_h = 0.0f;
        if (em.tile_map.valid())
        {
            world_w = static_cast<float>(em.tile_map.width * TileMap::TILE_SIZE);
            world_h = static_cast<float>(em.tile_map.height * TileMap::TILE_SIZE);
        }
        else
        {
            // Unit tests: derive bounds from entity positions.
            for (auto e : view)
            {
                const auto& t = view.get<Transform>(e);
                const auto& c = view.get<Collider>(e);
                world_w = std::max(world_w, t.x + c.width);
                world_h = std::max(world_h, t.y + c.height);
            }
        }

        static SpatialGrid grid;
        grid.init(world_w, world_h);
        grid.countPass(dynamics, em.registry());
        grid.buildPass();
        resolveDynVsDyn(em, dynamics, grid);
    }

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
