#include "systems/CombatSystem.h"

#include "TileMap.h"
#include "ecs/Components.h"
#include "systems/AudioSystem.h"

#include <cmath>
#include <iostream>
#include <limits>
#include <tracy/Tracy.hpp>

// ---------------------------------------------------------------------------
// Combat formula helpers — pure functions; read FormulaConfig, no side effects.
// ---------------------------------------------------------------------------

// Swing cooldown: driven by weapon weight and a proportional stat blend.
// DEX bias = dex_scaling / (str_scaling + dex_scaling) — a weapon with higher
// DEX scaling has swing speed driven more by DEX than STR.
//   effectiveStat = STR * strBias + DEX * dexBias
//   cooldown = (weight * weightScale) / (1 + statScale * sqrt(effectiveStat) / 100)
// sqrt gives smooth, gradually diminishing returns per stat point (~3-4% at
// low stats, <1% at 50+). Min-clamped to 0.05 s (hard cap at 20 swings/sec).
float computeSwingCooldown(const Weapon& w, const Stats& s, const FormulaConfig& f)
{
    const float total = w.str_scaling + w.dex_scaling;
    const float dexBias = total > 0.0f ? w.dex_scaling / total : 0.5f;
    const float strBias = 1.0f - dexBias;
    const float effectiveStat =
        (static_cast<float>(s.str) * strBias) + (static_cast<float>(s.dex) * dexBias);
    const float numerator = w.weight * f.swing.weight_scale;
    const float reduction = f.swing.stat_scale * std::sqrt(effectiveStat) / 100.0f;
    const float denom = 1.0f + reduction;
    const float cooldown = numerator / denom;
    return std::max(0.05f, cooldown);
}

// Damage from one swing: base + both stat contributions added independently.
// Both STR and DEX always contribute; scaling floats control the per-point weight.
float computeDamage(const Weapon& w, const Stats& s, const FormulaConfig& /*f*/)
{
    return w.base_damage + std::floor(static_cast<float>(s.str) * w.str_scaling) +
           std::floor(static_cast<float>(s.dex) * w.dex_scaling);
}

// Expose helpers to other translation units (DamageSystem, tests).
// Declared in CombatSystem.h — keep the implementations here.
// Forward declarations for the test-accessible free functions live in the header.

// ---------------------------------------------------------------------------

// NOLINTNEXTLINE(readability-function-cognitive-complexity)
void CombatSystem::update(EntityManager& em, double dt)
{
    ZoneScopedN("CombatSystem");
    const float fdt = static_cast<float>(dt);
    const FormulaConfig& f = em.formulas;

    // --- 1. Destroy hitboxes spawned last frame ----------------------------
    // Collect first; entt iterators are invalidated by destroy() mid-sweep.
    std::vector<entt::entity> toDestroy;
    for (auto entity : em.registry().view<Hitbox>())
        toDestroy.push_back(entity);
    for (auto e : toDestroy)
        em.destroy(e);

    // --- 2. Tick all combat timers -----------------------------------------

    // Weapon cooldowns (all entities that carry a weapon).
    for (auto [entity, weapon] : em.registry().view<Weapon>().each())
    {
        weapon.swing_cooldown_remaining = std::max(0.0f, weapon.swing_cooldown_remaining - fdt);
        weapon.skill_cooldown_remaining = std::max(0.0f, weapon.skill_cooldown_remaining - fdt);
    }

    // AttackLocked — remove when expired.
    {
        std::vector<entt::entity> expired;
        for (auto [entity, al] : em.registry().view<AttackLocked>().each())
        {
            al.remaining = std::max(0.0f, al.remaining - fdt);
            if (al.remaining <= 0.0f)
                expired.push_back(entity);
        }
        for (auto e : expired)
            em.registry().remove<AttackLocked>(e);
    }

    // Dodging — remove when expired.
    {
        std::vector<entt::entity> expired;
        for (auto [entity, d] : em.registry().view<Dodging>().each())
        {
            d.remaining = std::max(0.0f, d.remaining - fdt);
            if (d.remaining <= 0.0f)
                expired.push_back(entity);
        }
        for (auto e : expired)
            em.registry().remove<Dodging>(e);
    }

    // Staggered — remove when expired.
    {
        std::vector<entt::entity> expired;
        for (auto [entity, sg] : em.registry().view<Staggered>().each())
        {
            sg.remaining = std::max(0.0f, sg.remaining - fdt);
            if (sg.remaining <= 0.0f)
                expired.push_back(entity);
        }
        for (auto e : expired)
            em.registry().remove<Staggered>(e);
    }

    // Parrying — remove when expired.
    {
        std::vector<entt::entity> expired;
        for (auto [entity, p] : em.registry().view<Parrying>().each())
        {
            p.remaining = std::max(0.0f, p.remaining - fdt);
            if (p.remaining <= 0.0f)
                expired.push_back(entity);
        }
        for (auto e : expired)
            em.registry().remove<Parrying>(e);
    }

    // Poise decay — after decay_window seconds with no hits, reset accumulated poise damage.
    for (auto [entity, poise] : em.registry().view<Poise>().each())
    {
        if (poise.current > 0.0f)
        {
            poise.decay_timer += fdt;
            if (poise.decay_timer >= f.poise.decay_window)
            {
                const std::string name = em.registry().all_of<Tag>(entity)
                                             ? em.registry().get<Tag>(entity).name
                                             : "entity";
                std::cout << "[Poise] " << name << ": decayed (reset to 0/" << poise.max << ")\n";
                poise.current = 0.0f;
                poise.decay_timer = 0.0f;
            }
        }
    }

    // Dodge cooldown (stored on Input component — player only).
    for (auto [entity, input] : em.registry().view<Input>().each())
        input.dodge_cooldown_remaining = std::max(0.0f, input.dodge_cooldown_remaining - fdt);

    // DamageFeedback — red flash; remove when expired.
    {
        std::vector<entt::entity> expired;
        for (auto [entity, df] : em.registry().view<DamageFeedback>().each())
        {
            df.remaining = std::max(0.0f, df.remaining - fdt);
            if (df.remaining <= 0.0f)
                expired.push_back(entity);
        }
        for (auto e : expired)
            em.registry().remove<DamageFeedback>(e);
    }

    // AttackFeedback — yellow flash; remove when expired.
    {
        std::vector<entt::entity> expired;
        for (auto [entity, af] : em.registry().view<AttackFeedback>().each())
        {
            af.remaining = std::max(0.0f, af.remaining - fdt);
            if (af.remaining <= 0.0f)
                expired.push_back(entity);
        }
        for (auto e : expired)
            em.registry().remove<AttackFeedback>(e);
    }

    // --- 3. Auto-attack mode toggle (P key, edge-detect on Input) ----------
    for (auto [entity, input, autoMode] : em.registry().view<Input, AutoAttackMode>().each())
    {
        if (input.auto_toggle_just_pressed)
        {
            autoMode.enabled = !autoMode.enabled;
            std::cout << "[CombatSystem] Auto-attack mode: " << (autoMode.enabled ? "ON" : "OFF")
                      << "\n";
        }
    }

    // --- 4. Parry window — opened by block_just_pressed + Shield -------------
    for (auto [entity, input] : em.registry().view<Input>().each())
    {
        if (input.block_just_pressed && em.registry().all_of<Shield>(entity) &&
            !em.registry().all_of<Parrying>(entity))
        {
            em.registry().emplace<Parrying>(entity, Parrying{0.15f});
        }
    }

    // --- 5. Player attack logic -------------------------------------------
    // Shared hitbox spawner lambda — used for normal attack and skill.
    auto spawnHitbox = [&](entt::entity owner, float ox, float oy, float size, float damage)
    {
        const auto hitboxEnt = em.create();
        em.registry().emplace<Transform>(hitboxEnt, Transform{ox, oy, 0.0f, 1.0f});
        em.registry().emplace<Collider>(hitboxEnt, Collider{size, size, false});
        em.registry().emplace<Hitbox>(hitboxEnt, Hitbox{damage, owner});
    };

    for (auto [entity, input, weapon, transform, facing] :
         em.registry().view<Input, Weapon, Transform, FacingDirection>().each())
    {
        const bool isAttackLocked = em.registry().all_of<AttackLocked>(entity);
        const bool isStaggered = em.registry().all_of<Staggered>(entity);

        // ---- Normal attack ------------------------------------------------
        bool fireAttack = false;
        if (em.registry().all_of<AutoAttackMode>(entity))
        {
            const auto& autoMode = em.registry().get<AutoAttackMode>(entity);
            if (autoMode.enabled)
                fireAttack =
                    (weapon.swing_cooldown_remaining <= 0.0f && !isAttackLocked && !isStaggered);
        }
        if (!fireAttack)
            fireAttack = (input.attack && weapon.swing_cooldown_remaining <= 0.0f &&
                          !isAttackLocked && !isStaggered);

        if (fireAttack)
        {
            // In auto mode, aim toward the nearest chasing/attacking enemy.
            float facingX = facing.dx;
            float facingY = facing.dy;

            if (em.registry().all_of<AutoAttackMode>(entity) &&
                em.registry().get<AutoAttackMode>(entity).enabled)
            {
                float bestDist = std::numeric_limits<float>::max();
                for (auto [eEnemy, ai, tEnemy] :
                     em.registry().view<AIController, Transform>().each())
                {
                    if (ai.state == AIController::State::Chase ||
                        ai.state == AIController::State::Attack)
                    {
                        const float dx = tEnemy.x - transform.x;
                        const float dy = tEnemy.y - transform.y;
                        const float d = std::sqrt(dx * dx + dy * dy);
                        if (d < bestDist)
                        {
                            bestDist = d;
                            const float inv = (d > 0.0f) ? (1.0f / d) : 0.0f;
                            facingX = dx * inv;
                            facingY = dy * inv;
                        }
                    }
                }
                // Update stored facing so MovementSystem sees the right direction.
                facing.dx = (facingX != 0.0f || facingY != 0.0f) ? facingX : facing.dx;
                facing.dy = (facingX != 0.0f || facingY != 0.0f) ? facingY : facing.dy;
            }

            // Spawn hitbox one half-width in front of the player.
            // LOS check: don't spawn if a wall or obstacle sits between the
            // player and the hitbox position — prevents hitting through obstacles.
            const float reach = 16.0f + 20.0f; // half collider + reach
            const float hx = transform.x + facingX * reach;
            const float hy = transform.y + facingY * reach;

            const bool hitboxLos = !em.tile_map.valid() ||
                                   em.tile_map.hasLineOfSight(transform.x, transform.y, hx, hy);
            if (hitboxLos)
            {
                if (em.registry().all_of<Stats>(entity))
                {
                    const auto& stats = em.registry().get<Stats>(entity);
                    const float dmg = computeDamage(weapon, stats, f);
                    spawnHitbox(entity, hx, hy, 32.0f, dmg);
                }
                else
                {
                    spawnHitbox(entity, hx, hy, 32.0f, weapon.base_damage);
                }
            }

            const float cooldown =
                em.registry().all_of<Stats>(entity)
                    ? computeSwingCooldown(weapon, em.registry().get<Stats>(entity), f)
                    : weapon.weight * f.swing.weight_scale / 1.0f;
            weapon.swing_cooldown_remaining = cooldown;

            // Attack commitment: locks new attacks/dodges for 60% of the cooldown.
            em.registry().emplace_or_replace<AttackLocked>(entity, AttackLocked{cooldown * 0.6f});

            // Yellow swing flash on the attacker (0.5s so it's clearly visible).
            em.registry().emplace_or_replace<AttackFeedback>(entity, AttackFeedback{0.5f});

            TracyMessageL("PlayerAttack");
            AudioSystem::playSfx(em.sounds.player_attack.path, em.sounds.player_attack.volume);
            std::cout << "[CombatSystem] Attack! dmg=";
            if (em.registry().all_of<Stats>(entity))
                std::cout << computeDamage(weapon, em.registry().get<Stats>(entity), f);
            else
                std::cout << weapon.base_damage;
            std::cout << " cooldown=" << cooldown << "s\n";
        }

        // ---- Weapon skill ------------------------------------------------
        if (input.skill && weapon.skill_cooldown_remaining <= 0.0f && !isAttackLocked &&
            !isStaggered)
        {
            const float reach = 16.0f + 40.0f; // bigger reach for skill
            const float hx = transform.x + facing.dx * reach;
            const float hy = transform.y + facing.dy * reach;
            float dmg = weapon.base_damage * 1.5f;
            if (em.registry().all_of<Stats>(entity))
                dmg = computeDamage(weapon, em.registry().get<Stats>(entity), f) * 1.5f;

            const bool skillLos = !em.tile_map.valid() ||
                                  em.tile_map.hasLineOfSight(transform.x, transform.y, hx, hy);
            if (skillLos)
                spawnHitbox(entity, hx, hy, 64.0f, dmg); // 64×64 hitbox
            weapon.skill_cooldown_remaining = 5.0f;
            em.registry().emplace_or_replace<AttackLocked>(entity, AttackLocked{0.4f});

            TracyMessageL("PlayerSkill");
            AudioSystem::playSfx(em.sounds.player_skill.path, em.sounds.player_skill.volume);
            std::cout << "[CombatSystem] Haymaker! dmg=" << dmg << " skill cooldown=5.0s\n";
        }

        // ---- Dodge -------------------------------------------------------
        const bool canDodge = (input.dodge_cooldown_remaining <= 0.0f && !isAttackLocked &&
                               !isStaggered && !em.registry().all_of<Dodging>(entity));
        if (input.dodge && canDodge)
        {
            // Context-sensitive direction:
            // If any active enemy is within engagement range → backstep
            // (step opposite to current facing, away from the threat).
            // Otherwise → normal roll in last movement/facing direction.
            static constexpr float kEngagementRadius = 150.0f;
            static constexpr float kEngagementRadiusSq = kEngagementRadius * kEngagementRadius;

            bool nearEnemy = false;
            for (auto [eEnemy, ai, tEnemy] : em.registry().view<AIController, Transform>().each())
            {
                if (ai.state != AIController::State::Chase &&
                    ai.state != AIController::State::Attack)
                    continue;
                const float ex = tEnemy.x - transform.x;
                const float ey = tEnemy.y - transform.y;
                if (ex * ex + ey * ey <= kEngagementRadiusSq)
                {
                    nearEnemy = true;
                    break;
                }
            }

            float dodgeX, dodgeY;
            if (nearEnemy)
            {
                // Backstep: opposite of facing.
                dodgeX = -facing.dx;
                dodgeY = -facing.dy;
            }
            else
            {
                // Normal roll: last movement direction, or facing fallback.
                dodgeX = (input.last_facing_x != 0.0f || input.last_facing_y != 0.0f)
                             ? input.last_facing_x
                             : facing.dx;
                dodgeY = (input.last_facing_x != 0.0f || input.last_facing_y != 0.0f)
                             ? input.last_facing_y
                             : facing.dy;
            }

            if (em.registry().all_of<Velocity>(entity))
            {
                auto& vel = em.registry().get<Velocity>(entity);
                vel.dx = dodgeX * 300.0f;
                vel.dy = dodgeY * 300.0f;
            }

            TracyMessageL("PlayerDodge");
            AudioSystem::playSfx(em.sounds.player_dodge.path, em.sounds.player_dodge.volume);
            em.registry().emplace<Dodging>(entity, Dodging{f.dodge.duration});
            input.dodge_cooldown_remaining = f.dodge.cooldown;

            std::cout << "[CombatSystem] " << (nearEnemy ? "Backstep!\n" : "Dodge roll!\n");
        }
    }

    // --- 6. Enemy attacks — hitbox-based, range-gated by attack_radius ------
    // Enemies spawn a hitbox toward the player when in range, just like the
    // player does. Weapon reach = 24px offset from enemy center.
    // DamageSystem Path 1 (hitbox→health) handles the actual damage application.
    {
        entt::entity playerEnt = entt::null;
        Transform playerTransform{};
        for (auto e : em.registry().view<Input>())
        {
            playerEnt = e;
            playerTransform = em.registry().get<Transform>(e);
            break;
        }

        if (playerEnt != entt::null && !em.registry().all_of<Dead>(playerEnt))
        {
            for (auto [entity, ai, weapon, transform] :
                 em.registry().view<AIController, Weapon, Transform>().each())
            {
                if (em.registry().all_of<Dead>(entity))
                    continue;
                if (em.registry().all_of<Staggered>(entity))
                    continue;
                if (weapon.swing_cooldown_remaining > 0.0f)
                    continue;
                if (ai.state != AIController::State::Chase &&
                    ai.state != AIController::State::Attack)
                    continue;

                const float dx = playerTransform.x - transform.x;
                const float dy = playerTransform.y - transform.y;
                const float dist = std::sqrt(dx * dx + dy * dy);

                if (dist > ai.attack_radius)
                    continue;

                // Direction toward player.
                const float nx = (dist > 0.f) ? dx / dist : 1.f;
                const float ny = (dist > 0.f) ? dy / dist : 0.f;

                // Spawn hitbox at weapon reach in front of the enemy.
                const float dmg = em.registry().all_of<Stats>(entity)
                                      ? computeDamage(weapon, em.registry().get<Stats>(entity), f)
                                      : weapon.base_damage;
                spawnHitbox(entity, transform.x + nx * 24.f, transform.y + ny * 24.f, 32.f, dmg);

                // Reset swing cooldown.
                weapon.swing_cooldown_remaining =
                    em.registry().all_of<Stats>(entity)
                        ? computeSwingCooldown(weapon, em.registry().get<Stats>(entity), f)
                        : 1.0f;

                // Update facing direction toward player.
                if (em.registry().all_of<FacingDirection>(entity))
                {
                    auto& fd = em.registry().get<FacingDirection>(entity);
                    fd.dx = nx;
                    fd.dy = ny;
                }

                // Yellow swing flash.
                em.registry().emplace_or_replace<AttackFeedback>(entity, AttackFeedback{0.5f});
            }
        }
    }
}
