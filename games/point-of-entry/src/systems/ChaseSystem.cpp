#include "systems/ChaseSystem.h"

#include "ecs/BalanceConfig.h"
#include "ecs/Components.h"
#include "ecs/EntityManager.h"
#include "ecs/GameComponents.h"
#include "ops/NavUtils.h"
#include "systems/AimSystem.h"
#include "systems/CombatSystem.h"
#include "systems/PlayerSystem.h"

#include <cmath>
#include <unordered_map>
#include <vector>

#include <entt/entt.hpp>

namespace chase
{
namespace
{

// Contact hurts once every this many seconds. Vermin have no attack: touching you IS the attack,
// which is what lets hundreds of them exist without hundreds of attack animations.
constexpr float kTouchInterval = 0.6f;
constexpr float kTouchRadius = 12.0f;

// Movement character is the SPECIES' business (see Motion in GameComponents): a tremor makes an
// insect, a plain glide makes a mammal, and this system applies whatever the creature declared
// without knowing which is which. The tremor is a DRAW OFFSET, never movement -- the body
// buzzes while the creature travels straight, so it cannot shiver through a wall.

// Bodies shove each other out of overlap AFTER moving. Nothing
// ever deflects a creature's heading (steering forces make a swarm swerve and fidget); each one
// walks dead-straight at the player, and the crowd spreads only because two bodies cannot share
// a spot. That is what makes a swarm press onto you as a tide instead of milling around you.
void shoveApart(EntityManager& em)
{
    auto& reg = em.registry();
    // A uniform grid so neighbour pairs are found in the same or adjacent cells only -- the
    // swarm is meant to reach the hundreds, and an all-pairs scan is the one thing here that
    // could not afford that.
    constexpr float kCell = 8.0f;
    constexpr float kSpace = 5.0f; // two ants closer than this get pushed apart
    static std::unordered_map<uint64_t, std::vector<entt::entity>> grid;
    grid.clear();
    const auto key = [](float x, float y)
    {
        const auto cx = static_cast<uint32_t>(static_cast<int>(x / kCell) + 32768);
        const auto cy = static_cast<uint32_t>(static_cast<int>(y / kCell) + 32768);
        return (static_cast<uint64_t>(cx) << 32) | cy;
    };
    for (auto [entity, t, vermin] : reg.view<Transform, Vermin>().each())
        if (!reg.all_of<Dying>(entity))
            grid[key(t.x, t.y)].push_back(entity);

    for (auto [entity, t, vermin] : reg.view<Transform, Vermin>().each())
    {
        if (reg.all_of<Dying>(entity))
            continue;
        for (int gy = -1; gy <= 1; ++gy)
            for (int gx = -1; gx <= 1; ++gx)
            {
                const auto it = grid.find(key(t.x + static_cast<float>(gx) * kCell,
                                              t.y + static_cast<float>(gy) * kCell));
                if (it == grid.end())
                    continue;
                for (const auto other : it->second)
                {
                    if (other <= entity) // each pair once, and never self
                        continue;
                    auto& ot = reg.get<Transform>(other);
                    const float dx = t.x - ot.x;
                    const float dy = t.y - ot.y;
                    const float d2 = dx * dx + dy * dy;
                    if (d2 >= kSpace * kSpace)
                        continue;
                    const float d = std::sqrt(std::max(d2, 0.01f));
                    // Half the overlap each, through the wall check so a shove cannot push a
                    // body into the masonry.
                    const float push = (kSpace - d) * 0.5f;
                    const float nx = dx / d;
                    const float ny = dy / d;
                    world::stepBlocked(em, t.x, nx * push, true, t.y, 3.0f, 3.0f);
                    world::stepBlocked(em, t.y, ny * push, false, t.x, 3.0f, 3.0f);
                    world::stepBlocked(em, ot.x, -nx * push, true, ot.y, 3.0f, 3.0f);
                    world::stepBlocked(em, ot.y, -ny * push, false, ot.x, 3.0f, 3.0f);
                }
            }
    }
}

void integrate(EntityManager& em, float dt)
{
    const entt::entity playerEnt = player::entity();
    auto& reg = em.registry();
    if (!reg.valid(playerEnt))
        return;
    const auto& pt = reg.get<Transform>(playerEnt);

    for (auto [entity, t, vel, vermin] : reg.view<Transform, Velocity, Vermin>().each())
    {
        // One axis at a time, against the same walls the player obeys. The flow field routes
        // AROUND walls but only suggests a direction -- a creature off the field, or one that
        // separation is pushing sideways, would otherwise walk straight through a wall.
        world::stepBlocked(em, t.x, vel.dx * dt, /*horizontal=*/true, t.y, 3.0f, 3.0f);
        world::stepBlocked(em, t.y, vel.dy * dt, /*horizontal=*/false, t.x, 3.0f, 3.0f);

        // Touching him costs him. On a timer per creature rather than per frame, or standing in
        // a crowd would drain a full bar in the time it takes to notice.
        auto& touch = reg.get_or_emplace<TouchCooldown>(entity, TouchCooldown{0.0f});
        touch.remaining -= dt;
        const float tdx = pt.x - t.x;
        const float tdy = pt.y - t.y;
        if (touch.remaining <= 0.0f && tdx * tdx + tdy * tdy < kTouchRadius * kTouchRadius)
        {
            touch.remaining = kTouchInterval;
            if (auto* hp = reg.try_get<Health>(playerEnt))
            {
                // Defense is derived from the sheet and shaves contact flat; the floor of 1
                // keeps a crowd dangerous no matter how broad his shoulders get.
                const int def =
                    reg.all_of<Stats>(playerEnt) ? stats::defense(reg.get<Stats>(playerEnt)) : 0;
                const int raw = static_cast<int>(reg.get<Vermin>(entity).contact_damage);
                // The guard stands between the hit and the bar -- see absorbWithGuard.
                hp->current -= tools::absorbWithGuard(em, std::max(1, raw - def), aim::guarding());
            }
        }
    }

    shoveApart(em);
}

} // namespace

void update(EntityManager& em, float dt)
{
    const entt::entity playerEnt = player::entity();
    auto& reg = em.registry();
    if (!reg.valid(playerEnt))
        return;
    const auto& pt = reg.get<Transform>(playerEnt);
    const auto& ff = em.flow_field;

    for (auto [entity, t, vel, vermin] : reg.view<Transform, Velocity, Vermin>().each())
    {
        if (reg.all_of<Dying>(entity))
        {
            vel.dx = 0.0f;
            vel.dy = 0.0f;
            continue;
        }

        // Surfacing: the burst owns the velocity until it expires, and only then does the
        // creature start hunting. Walls still apply -- integrate() moves everything.
        if (auto* surge = reg.try_get<Surge>(entity))
        {
            surge->remaining -= dt;
            if (surge->remaining > 0.0f)
            {
                vel.dx = surge->dx * surge->speed;
                vel.dy = surge->dy * surge->speed;
                if (auto* facing = reg.try_get<FacingDirection>(entity);
                    facing != nullptr && surge->dx != 0.0f)
                {
                    facing->dx = surge->dx < 0.0f ? -1.0f : 1.0f;
                    facing->render_dx = facing->dx;
                }
                continue;
            }
            reg.remove<Surge>(entity);
        }

        // The flow field is a BFS from the player rebuilt only when he changes cell, so following
        // it is a table read -- which is what makes a thousand of these affordable. It also
        // routes around walls, so nothing here needs to know what a wall is.
        const int col = static_cast<int>(t.x / FlowField::CELL_SIZE);
        const int row = static_cast<int>(t.y / FlowField::CELL_SIZE);
        float dx = 0.0f;
        float dy = 0.0f;
        if (col >= 0 && col < FlowField::COLS && row >= 0 && row < FlowField::ROWS)
        {
            dx = ff.cells[row][col].dx;
            dy = ff.cells[row][col].dy;
        }
        // Off the field, or standing in an unreachable cell: walk straight at him. Better a
        // creature that bumps a wall than one that stands still looking broken.
        if (dx == 0.0f && dy == 0.0f)
        {
            dx = pt.x - t.x;
            dy = pt.y - t.y;
            const float len = std::sqrt(dx * dx + dy * dy);
            if (len > 0.0f)
            {
                dx /= len;
                dy /= len;
            }
        }

        auto& sk = reg.get_or_emplace<Skitter>(entity, Skitter{});
        if (sk.phase == 0.0f)
            sk.phase = static_cast<float>(entt::to_integral(entity) % 997) * 0.618f;
        sk.until += dt; // doubles as this creature's own clock

        const auto& motion = reg.get_or_emplace<Motion>(entity, Motion{});
        const float drift = std::sin((sk.until + sk.phase) * motion.drift_hz) * motion.drift_amount;
        const float gx = dx - dy * drift;
        const float gy = dy + dx * drift;

        // Speed is the species' own (see Vermin) -- generally slower than the exterminator in a
        // straight line, so positioning beats reflexes; a faster species narrows that margin.
        vel.dx = gx * vermin.speed;
        vel.dy = gy * vermin.speed;

        // Face the way it is actually heading; a creature gliding left while drawn facing right
        // is the moonwalk bug at swarm scale. Vertical-only travel keeps the last side.
        if (auto* facing = reg.try_get<FacingDirection>(entity); facing != nullptr && gx != 0.0f)
        {
            facing->dx = gx < 0.0f ? -1.0f : 1.0f;
            facing->render_dx = facing->dx;
        }

        // The tremor, if this species declared one; zero amount writes a clean zero offset, so
        // a plain walker is exactly plain rather than carrying a stale buzz from anything.
        const float buzz =
            std::sin((sk.until + sk.phase) * motion.buzz_hz * 6.2831853f) * motion.buzz_amount;
        auto& spr = reg.get<Sprite>(entity);
        spr.draw_offset_x = -dy * buzz;
        spr.draw_offset_y = dx * buzz;
    }

    integrate(em, dt);
}

} // namespace chase
