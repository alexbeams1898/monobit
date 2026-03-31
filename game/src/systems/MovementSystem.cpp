#include "systems/MovementSystem.h"

#include "TileMap.h"
#include "ecs/Components.h"
#include "ecs/GameComponents.h"
#include "ecs/GameConfig.h"
#include "systems/AudioSystem.h"
#include "systems/CombatSystem.h"

#include <cmath>
#include <tracy/Tracy.hpp>
#include <vector>

// How much to shrink the entity's bounding box for movement projection checks.
// Applied as an inset on each side (so a 32x32 entity uses a 30x30 test box).
//
// Why this exists — "corner sticking": without an inset, an entity approaching
// a doorway at a slight angle gets caught on the corner of the adjacent wall
// tile. The entity's full-size box overlaps the tile's edge by 1-2 px on one
// axis, causing that axis's velocity to be zeroed even though the entity is
// clearly trying to slide past the corner. A 2 px inset absorbs that micro-
// overlap, allowing smooth corner slides while still blocking true head-on
// collisions (a 30 px box cannot pass through a 32 px wall).
static constexpr float MOVEMENT_INSET = 2.0f;

// Returns true if two center-based AABBs overlap.
static bool aabbOverlap(float ax, float ay, float aw, float ah, float bx, float by, float bw,
                        float bh)
{
    return std::abs(ax - bx) < (aw + bw) * 0.5f && std::abs(ay - by) < (ah + bh) * 0.5f;
}

namespace
{

// Pass 0: Stagger lock -- zero velocity for any entity that can't move.
// Must run before Pass 1 (player input) and after ChaseSystem/SteeringSystem
// (which set enemy velocity earlier this frame) so both are covered.
void zeroStaggeredVelocities(entt::registry& reg)
{
    for (auto entity : reg.view<Staggered, Velocity>())
    {
        auto& vel = reg.get<Velocity>(entity);
        vel.dx = 0.0f;
        vel.dy = 0.0f;
    }
}

// Pass 1: PlayerActions intent -> Velocity.
// Walk speed: base * (1 + floor(dex_scale * log(DEX + 1)) / 100)
// Sprint: walk speed x sprint_multiplier while actions.sprint is true.
// Entities without Stats fall back to f.movement.base (150 px/s default).
//
// Skip velocity update if the entity is Dodging (dodge impulse carries through)
// or Staggered (guard break / parry result locks movement briefly).
void applyPlayerInput(entt::registry& reg, float fdt, const FormulaConfig& f,
                      const SoundConfig& snd)
{
    for (auto [entity, actions, vel] : reg.view<PlayerActions, Velocity>().each())
    {
        // Dodging and Staggered states override normal movement.
        if (reg.all_of<Dodging>(entity) || reg.all_of<Staggered>(entity))
            continue;

        float speed = f.movement.base;
        if (reg.all_of<Stats>(entity))
        {
            const int dex = reg.get<Stats>(entity).dex;
            speed *= (1.0f +
                      std::floor(f.movement.dex_scale * std::log(static_cast<float>(dex) + 1.0f)) /
                          100.0f);
        }

        // Equip load speed modifier.
        if (reg.all_of<ArmorStats>(entity))
        {
            const int tier = reg.get<ArmorStats>(entity).load_tier;
            if (tier == 3)
                speed *= f.equip_load.overloaded_speed;
            else if (tier == 2)
                speed *= f.equip_load.heavy_speed;
            else if (tier == 1)
                speed *= f.equip_load.medium_speed;
            // tier 0 (light) = full speed, no modifier
        }

        // Sprint drains stamina continuously; blocked when empty.
        if (actions.sprint && reg.all_of<Stamina>(entity))
        {
            auto& sta = reg.get<Stamina>(entity);
            const float wWeight =
                reg.all_of<Weapon>(entity) ? reg.get<Weapon>(entity).weight : 1.0f;
            const int dexSprint = reg.all_of<Stats>(entity) ? reg.get<Stats>(entity).dex : 1;
            const float drain =
                wWeight * f.stamina.sprint_effort /
                (1.0f + static_cast<float>(dexSprint) * f.stamina.sprint_dex_scale) * fdt;
            if (sta.current > 0.0f)
            {
                deductStamina(reg, entity, drain, f);
            }
            else
            {
                actions.sprint = false;
            }
        }

        // Backpedal: moving opposite to aim direction slows the player.
        bool backpedal = false;
        if (auto* facing = reg.try_get<FacingDirection>(entity))
        {
            const bool moving = (actions.move_x != 0.0f || actions.move_y != 0.0f);
            const float dot = actions.move_x * facing->dx + actions.move_y * facing->dy;
            backpedal = moving && (dot < 0.0f);
            facing->backpedaling = backpedal;
            facing->sprinting = actions.sprint;

            // Walk animation speed: sprint+backpedal = jog (sprint slowed by backpedal ratio).
            if (actions.sprint && backpedal)
                facing->walk_anim_speed =
                    f.movement.sprint_anim_speed * f.movement.backpedal_anim_speed;
            else if (actions.sprint)
                facing->walk_anim_speed = f.movement.sprint_anim_speed;
            else if (backpedal)
                facing->walk_anim_speed = f.movement.backpedal_anim_speed;
            else
                facing->walk_anim_speed = 1.0f;
        }

        if (actions.sprint)
            speed *= f.movement.sprint_multiplier;
        if (backpedal)
            speed *= f.movement.backpedal_multiplier;

        // Blend toward target velocity for a natural acceleration feel.
        // Sprint ramps up slowly (sprint_blend); backpedal and walk snap faster (walk_blend).
        const float blend =
            (actions.sprint && !backpedal) ? f.movement.sprint_blend : f.movement.walk_blend;
        const float t = 1.0f - std::exp(-blend * fdt);
        vel.dx += (actions.move_x * speed - vel.dx) * t;
        vel.dy += (actions.move_y * speed - vel.dy) * t;

        // Footstep cadence: play a step sound at regular intervals while moving.
        const bool moving = (actions.move_x != 0.0f || actions.move_y != 0.0f);
        if (moving)
        {
            actions.step_timer -= fdt;
            if (actions.step_timer <= 0.0f)
            {
                const float cadence = actions.sprint ? 0.25f : 0.4f;
                actions.step_timer = cadence;
                const auto& sfx = actions.sprint ? snd.footstep_run : snd.footstep_walk;
                AudioSystem::playSfx(sfx.path, sfx.volume);
            }
        }
        else
        {
            actions.step_timer = 0.0f;
        }

        // Tick wall bump cooldown.
        actions.wall_bump_cooldown = std::max(0.0f, actions.wall_bump_cooldown - fdt);
    }
}

// Pass 2: Velocity -> Transform with static wall projection.
//
// When the tile map is present, walls are looked up by tile coordinate --
// O(~4 tile lookups) per axis check regardless of map size. This replaces
// the old O(n_static) scan over thousands of wall ECS entities.
//
// Fall back to ECS statics when no tile map is available (unit tests that
// set up explicit wall entities without generating a full tile map).
void integrateVelocity(EntityManager& em, float fdt, const SoundConfig& snd)
{
    auto& reg = em.registry();
    const bool use_tile_map = em.tile_map.valid();
    const float ts = static_cast<float>(TileMap::TILE_SIZE);

    auto allColliders = reg.view<Transform, Collider>();
    std::vector<entt::entity> statics;
    if (!use_tile_map)
    {
        statics.reserve(64);
        for (auto e : allColliders)
        {
            if (!reg.all_of<Velocity>(e) && allColliders.get<Collider>(e).is_solid)
                statics.push_back(e);
        }
    }

    for (auto [entity, vel, transform] : reg.view<Velocity, Transform>().each())
    {
        const Collider* col = reg.try_get<Collider>(entity);
        if (!col)
        {
            // No collider -- integrate unconditionally.
            transform.x += vel.dx * fdt;
            transform.y += vel.dy * fdt;
            continue;
        }

        // Inset box dimensions used for movement projection.
        // Slightly smaller than the entity's true collider -- see MOVEMENT_INSET.
        const float mw = col->width - MOVEMENT_INSET;
        const float mh = col->height - MOVEMENT_INSET;
        const float hw = mw * 0.5f;
        const float hh = mh * 0.5f;

        // Returns true if the inset box centered at (cx, cy) overlaps any solid wall.
        // Tile map path: O(~4) direct array lookups.
        // ECS fallback path: O(n_statics) AABB scan (for tests without a tile map).
        auto touches_wall = [&](float cx, float cy) -> bool
        {
            if (use_tile_map)
            {
                const int cmin = static_cast<int>(std::floor((cx - hw) / ts));
                const int cmax = static_cast<int>(std::floor((cx + hw) / ts));
                const int rmin = static_cast<int>(std::floor((cy - hh) / ts));
                const int rmax = static_cast<int>(std::floor((cy + hh) / ts));
                for (int r = rmin; r <= rmax; ++r)
                    for (int c = cmin; c <= cmax; ++c)
                        if (em.tile_map.in_bounds(c, r) && !em.tile_map.at(c, r).walkable)
                            return true;
                return false;
            }
            for (auto se : statics)
            {
                const auto& st = allColliders.get<Transform>(se);
                const auto& sc = allColliders.get<Collider>(se);
                if (aabbOverlap(cx, cy, mw, mh, st.x, st.y, sc.width, sc.height))
                    return true;
            }
            return false;
        };

        // X-axis projection: candidate position after horizontal movement.
        float nx = transform.x + vel.dx * fdt;
        bool hitWallX = false;
        if (touches_wall(nx, transform.y))
        {
            hitWallX = (vel.dx != 0.0f);
            vel.dx = 0.0f;
            nx = transform.x;
        }

        // Y-axis projection: candidate position after vertical movement.
        // Uses resolved X (nx) so a corner slide doesn't re-trigger the X wall.
        float ny = transform.y + vel.dy * fdt;
        bool hitWallY = false;
        if (touches_wall(nx, ny))
        {
            hitWallY = (vel.dy != 0.0f);
            vel.dy = 0.0f;
            ny = transform.y;
        }

        // Wall bump sound -- player only, with cooldown to prevent spam.
        if ((hitWallX || hitWallY) && reg.all_of<PlayerActions>(entity))
        {
            auto& actions = reg.get<PlayerActions>(entity);
            if (actions.wall_bump_cooldown <= 0.0f)
            {
                AudioSystem::playSfx(snd.wall_bump.path, snd.wall_bump.volume);
                actions.wall_bump_cooldown = 0.2f;
            }
        }

        transform.x = nx;
        transform.y = ny;
    }
}

// Pass 3: Update FacingDirection for AI entities only.
// Player facing is updated per-frame in Engine::processEvents() (mouse-derived,
// not physics -- must not be tied to the fixed-step tick rate).
// AI entities derive facing from velocity with turn-speed blending.
// Exception: skip while Dodging so facing stays locked during a dodge.
void updateAIFacing(entt::registry& reg, float fdt)
{
    for (auto [entity, vel, facing] : reg.view<Velocity, FacingDirection>().each())
    {
        if (reg.all_of<Dodging>(entity))
            continue;
        if (reg.all_of<PlayerActions>(entity))
            continue; // player -- handled in gamePerFrame()

        const float dlen = std::sqrt(vel.dx * vel.dx + vel.dy * vel.dy);
        if (dlen > 0.0f)
        {
            const float targetX = vel.dx / dlen;
            const float targetY = vel.dy / dlen;

            if (const AIController* aic = reg.try_get<AIController>(entity);
                aic && aic->turn_speed > 0.0f)
            {
                const float blend = 1.0f - std::exp(-aic->turn_speed * fdt);
                facing.dx += (targetX - facing.dx) * blend;
                facing.dy += (targetY - facing.dy) * blend;
                const float fl = std::sqrt(facing.dx * facing.dx + facing.dy * facing.dy);
                if (fl > 0.0f)
                {
                    facing.dx /= fl;
                    facing.dy /= fl;
                }
            }
            else
            {
                facing.dx = targetX;
                facing.dy = targetY;
            }
        }

        // AI visual facing = gameplay facing (already smoothed by turn_speed).
        facing.render_dx = facing.dx;
        facing.render_dy = facing.dy;
    }
}

} // anonymous namespace

void MovementSystem::update(EntityManager& em, double dt)
{
    ZoneScopedN("MovementSystem");
    const float fdt = static_cast<float>(dt);
    const FormulaConfig& f = em.registry().ctx().get<FormulaConfig>();
    const SoundConfig& snd = em.registry().ctx().get<SoundConfig>();

    zeroStaggeredVelocities(em.registry());
    applyPlayerInput(em.registry(), fdt, f, snd);
    integrateVelocity(em, fdt, snd);
    updateAIFacing(em.registry(), fdt);
}
