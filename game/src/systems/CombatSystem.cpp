#include "systems/CombatSystem.h"

#include "TileMap.h"
#include "ecs/Components.h"
#include "ecs/GameComponents.h"
#include "ecs/GameConfig.h"
#include "ops/InventoryOps.h"
#include "systems/AudioSystem.h"

#include <cmath>
#include <limits>
#include <random>
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
    const float numerator = f.swing.base_swing_time + w.weight * f.swing.weight_scale;
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

void deductStamina(entt::registry& reg, entt::entity entity, float cost, const FormulaConfig& f)
{
    // God mode: player never loses stamina.
    if (reg.all_of<PlayerActions>(entity) && reg.ctx().get<DebugFlags>().god_mode)
        return;

    auto& sta = reg.get<Stamina>(entity);
    const float before = sta.current;
    sta.current = std::max(0.0f, sta.current - cost);
    sta.recovery_timer = f.stamina.recovery_delay;
    if (before > 0.0f && sta.current <= 0.0f)
    {
        reg.emplace_or_replace<Staggered>(entity, Staggered{f.stamina.exhaustion_stagger});
        // Lock sprint until stamina is fully recovered. Other stamina actions
        // (attacks, dodges, blocking) remain available.
        sta.sprint_locked = true;
    }
}

static std::mt19937& combatRng()
{
    static std::mt19937 gen{std::random_device{}()};
    return gen;
}

// Spawn a projectile entity: Transform + Collider + Hitbox + Projectile + Sprite.
// No Velocity — ProjectileSystem moves projectiles and checks wall overlap directly,
// so MovementSystem doesn't deflect them along walls.
static void spawnProjectile(EntityManager& em, entt::entity owner, float ox, float oy, float dirX,
                            float dirY, const Weapon& weapon, float damage, bool leftHand)
{
    const auto proj = em.create();
    em.registry().emplace<Transform>(proj, Transform{ox, oy, 0.0f, 1.0f});
    em.registry().emplace<Collider>(proj,
                                    Collider{weapon.projectile_size, weapon.projectile_size, true});
    em.registry().emplace<Hitbox>(proj, Hitbox{damage, owner, false, leftHand});
    em.registry().emplace<Projectile>(proj, Projectile{owner, weapon.effective_range, ox, oy,
                                                       weapon.pierce, dirX, dirY,
                                                       weapon.projectile_speed, leftHand});
    em.registry().emplace<Tag>(proj, Tag{"projectile"});

    const float sz = weapon.projectile_size;
    Sprite spr;
    spr.src_w = static_cast<int>(sz);
    spr.src_h = static_cast<int>(sz);
    spr.layer = 2;
    if (!weapon.projectile_sprite.empty())
        spr.texture_path = weapon.projectile_sprite;
    em.registry().emplace<Sprite>(proj, spr);
    if (weapon.projectile_sprite.empty())
        em.registry().emplace<SolidColor>(proj, SolidColor{1.0f, 1.0f, 0.6f});
}

// Spawn a brief muzzle flash at the fire point.
static void spawnMuzzleFlash(EntityManager& em, float x, float y)
{
    const auto flash = em.create();
    em.registry().emplace<Transform>(flash, Transform{x, y, 0.0f, 1.0f});
    Sprite spr;
    spr.src_w = 12;
    spr.src_h = 12;
    spr.layer = 2;
    em.registry().emplace<Sprite>(flash, spr);
    em.registry().emplace<SolidColor>(flash, SolidColor{1.0f, 0.95f, 0.7f});
    em.registry().emplace<Dead>(flash, Dead{0.05f});
}

// Fire a ranged weapon: spawn projectile(s) with optional spread.
static void fireRangedWeapon(EntityManager& em, entt::entity entity, const Weapon& weapon,
                             const Transform& transform, float facingX, float facingY, float damage,
                             bool leftHand)
{
    static constexpr float DEG_TO_RAD = 3.14159265f / 180.0f;
    static constexpr float MUZZLE_OFFSET = 20.0f;

    for (int i = 0; i < weapon.projectile_count; ++i)
    {
        float dx = facingX;
        float dy = facingY;

        if (weapon.spread > 0.0f)
        {
            const float halfSpread = weapon.spread * 0.5f * DEG_TO_RAD;
            std::uniform_real_distribution<float> dist(-halfSpread, halfSpread);
            const float angle = dist(combatRng());
            const float cosA = std::cos(angle);
            const float sinA = std::sin(angle);
            const float nx = dx * cosA - dy * sinA;
            const float ny = dx * sinA + dy * cosA;
            dx = nx;
            dy = ny;
        }

        const float ox = transform.x + dx * MUZZLE_OFFSET;
        const float oy = transform.y + dy * MUZZLE_OFFSET;
        spawnProjectile(em, entity, ox, oy, dx, dy, weapon, damage, leftHand);
    }

    // Muzzle flash at the fire point (one per volley, not per projectile).
    const float fx = transform.x + facingX * MUZZLE_OFFSET;
    const float fy = transform.y + facingY * MUZZLE_OFFSET;
    spawnMuzzleFlash(em, fx, fy);
}

// Set the animation speed multiplier so the attack anim fits within the lock duration.
static void syncAttackAnimSpeed(entt::registry& reg, entt::entity entity, const Weapon& weapon,
                                float lockDuration)
{
    if (!reg.all_of<AnimRowConfig, FacingDirection>(entity))
        return;

    int frameCount = 0;
    float frameDur = 0.0f;

    const auto* rowIdx = reg.ctx().find<AnimRowIndex>();
    if (!weapon.attack_anim.empty() && weapon.attack_anim != "slash" && rowIdx != nullptr)
    {
        const auto it = rowIdx->rows.find(weapon.attack_anim);
        if (it != rowIdx->rows.end())
        {
            frameCount = it->second.frames;
            frameDur = it->second.duration;
        }
    }

    if (frameCount <= 0)
    {
        const auto& rowCfg = reg.get<AnimRowConfig>(entity);
        const auto& atkRow = rowCfg.rows[static_cast<int>(AnimState::Attack)];
        frameCount = atkRow.frames;
        frameDur = atkRow.duration;
    }

    if (!weapon.shoot_frames.empty())
        frameCount = static_cast<int>(weapon.shoot_frames.size());

    const float nativeLen = static_cast<float>(frameCount) * frameDur;
    if (nativeLen > 0.0f && lockDuration > 0.0f)
        reg.get<FacingDirection>(entity).attack_anim_speed = lockDuration / nativeLen;
}

static void playAttackSound(const Weapon& weapon, const SoundConfig& snd)
{
    if (weapon.fire_sound.empty())
        return;
    const auto& fireSnd = snd.get(weapon.fire_sound);
    std::string clipPath;
    if (!fireSnd.variations.empty())
    {
        auto dist = std::uniform_int_distribution<size_t>(0, fireSnd.variations.size() - 1);
        clipPath = fireSnd.variations[dist(combatRng())];
    }
    else
    {
        clipPath = fireSnd.path;
    }
    if (!clipPath.empty())
        AudioSystem::playSfx(clipPath, fireSnd.volume);
}

// NOLINTNEXTLINE(readability-function-cognitive-complexity,readability-function-size)
void CombatSystem::update(EntityManager& em, double dt)
{
    ZoneScopedN("CombatSystem");
    const float fdt = static_cast<float>(dt);
    const FormulaConfig& f = em.registry().ctx().get<FormulaConfig>();
    const SoundConfig& snd = em.registry().ctx().get<SoundConfig>();

    // --- 1. Destroy hitboxes spawned last frame ----------------------------
    // Check for player-owned misses before destroying.
    // Projectile entities also have Hitbox but are managed by ProjectileSystem.
    std::vector<entt::entity> toDestroy;
    for (auto entity : em.registry().view<Hitbox>())
    {
        if (em.registry().all_of<Projectile>(entity))
            continue;
        const auto& hb = em.registry().get<Hitbox>(entity);
        if (!hb.hit_something && hb.owner != entt::null &&
            em.registry().all_of<PlayerActions>(hb.owner))
        {
            const auto& atk = snd.get("player_attack");
            AudioSystem::playSfx(atk.path, atk.volume);
        }
        toDestroy.push_back(entity);
    }
    for (auto e : toDestroy)
        em.destroy(e);

    // --- 2. Tick all combat timers -----------------------------------------

    // Weapon cooldowns (both hands).
    for (auto [entity, weapon] : em.registry().view<Weapon>().each())
    {
        weapon.swing_cooldown_remaining = std::max(0.0f, weapon.swing_cooldown_remaining - fdt);
        weapon.skill_cooldown_remaining = std::max(0.0f, weapon.skill_cooldown_remaining - fdt);
    }
    for (auto [entity, weapon] : em.registry().view<LeftWeapon>().each())
    {
        weapon.swing_cooldown_remaining = std::max(0.0f, weapon.swing_cooldown_remaining - fdt);
        weapon.skill_cooldown_remaining = std::max(0.0f, weapon.skill_cooldown_remaining - fdt);
    }

    // Reload timers (ranged weapons with magazines).
    for (auto [entity, rs, weapon] : em.registry().view<RangedState, Weapon>().each())
    {
        if (!rs.reloading)
            continue;
        rs.reload_timer -= fdt;
        if (rs.reload_timer > 0.0f)
            continue;

        rs.reloading = false;
        rs.reload_timer = 0.0f;

        // Fill magazine from inventory ammo pool.
        auto* inv = em.registry().try_get<Inventory>(entity);
        const bool godMode = em.registry().ctx().get<DebugFlags>().god_mode;
        if (inv != nullptr && !weapon.ammo_type.empty())
        {
            const int reserve = InventoryOps::countItem(*inv, weapon.ammo_type);
            const int needed = rs.magazine_size - rs.ammo_in_magazine;
            const int fill = std::min(needed, reserve);
            if (fill > 0)
            {
                if (!godMode)
                {
                    auto* equip = em.registry().try_get<Equipment>(entity);
                    if (equip != nullptr)
                        InventoryOps::consumeItems(*inv, *equip, weapon.ammo_type, fill);
                }
                rs.ammo_in_magazine += fill;
            }
        }
        else
        {
            rs.ammo_in_magazine = rs.magazine_size;
        }
    }

    // Stamina recovery: after recovery_delay with no deduction, regen at recovery_rate/s.
    // Sprint lockout clears once stamina is back to full (Elden Ring style).
    for (auto [entity, sta] : em.registry().view<Stamina>().each())
    {
        if (sta.recovery_timer > 0.0f)
        {
            sta.recovery_timer -= fdt;
        }
        else if (sta.current < sta.max_stamina)
        {
            sta.current = std::min(sta.max_stamina, sta.current + f.stamina.recovery_rate * fdt);
        }
        if (sta.sprint_locked && sta.current >= sta.max_stamina)
            sta.sprint_locked = false;
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
                poise.current = 0.0f;
                poise.decay_timer = 0.0f;
            }
        }
    }

    // Dodge cooldown (stored on PlayerActions component — player only).
    for (auto [entity, actions] : em.registry().view<PlayerActions>().each())
        actions.dodge_cooldown_remaining = std::max(0.0f, actions.dodge_cooldown_remaining - fdt);

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

    // RiposteWindow — remove when expired.
    {
        std::vector<entt::entity> expired;
        for (auto [entity, rw] : em.registry().view<RiposteWindow>().each())
        {
            rw.remaining = std::max(0.0f, rw.remaining - fdt);
            if (rw.remaining <= 0.0f)
                expired.push_back(entity);
        }
        for (auto e : expired)
            em.registry().remove<RiposteWindow>(e);
    }

    // CriticalAttacking — remove when expired.
    {
        std::vector<entt::entity> expired;
        for (auto [entity, ca] : em.registry().view<CriticalAttacking>().each())
        {
            ca.remaining = std::max(0.0f, ca.remaining - fdt);
            if (ca.remaining <= 0.0f)
                expired.push_back(entity);
        }
        for (auto e : expired)
            em.registry().remove<CriticalAttacking>(e);
    }

    // CriticalTarget — remove when expired.
    {
        std::vector<entt::entity> expired;
        for (auto [entity, ct] : em.registry().view<CriticalTarget>().each())
        {
            ct.remaining = std::max(0.0f, ct.remaining - fdt);
            if (ct.remaining <= 0.0f)
                expired.push_back(entity);
        }
        for (auto e : expired)
            em.registry().remove<CriticalTarget>(e);
    }

    // --- 3. Auto-attack mode toggle (P key, edge-detect on PlayerActions) ----------
    for (auto [entity, actions, autoMode] :
         em.registry().view<PlayerActions, AutoAttackMode>().each())
    {
        if (actions.auto_toggle_just_pressed)
        {
            autoMode.enabled = !autoMode.enabled;
        }
    }

    // --- 4. Parry window — opened by block_just_pressed + Shield -------------
    for (auto [entity, actions] : em.registry().view<PlayerActions>().each())
    {
        if (actions.block_just_pressed && em.registry().all_of<Shield>(entity) &&
            !em.registry().all_of<Parrying>(entity))
        {
            em.registry().emplace<Parrying>(entity, Parrying{f.combat.parry_window});
        }
    }

    // --- 5. Player attack logic -------------------------------------------
    // Shared hitbox spawner lambda — used for normal attack and skill.
    auto spawnHitbox =
        [&](entt::entity owner, float ox, float oy, float size, float damage, bool leftHand = false)
    {
        const auto hitboxEnt = em.create();
        em.registry().emplace<Transform>(hitboxEnt, Transform{ox, oy, 0.0f, 1.0f});
        em.registry().emplace<Collider>(hitboxEnt, Collider{size, size, false});
        em.registry().emplace<Hitbox>(hitboxEnt, Hitbox{damage, owner, false, leftHand});
    };

    // Per-hand attack helper — contains the full ranged + melee fire paths,
    // cooldown set, attack lock, animation speed calc, stamina deduction, sound.
    // Called once for right hand (Weapon) and once for left hand (LeftWeapon).
    auto attemptAttack = [&](entt::entity entity, Weapon& weapon, bool attackPressed,
                             const Transform& transform, FacingDirection& facing, bool isLeftHand)
    {
        const bool isAttackLocked = em.registry().all_of<AttackLocked>(entity);
        const bool isStaggered = em.registry().all_of<Staggered>(entity);
        const bool isCritLocked = em.registry().all_of<CriticalAttacking>(entity) ||
                                  em.registry().all_of<CriticalTarget>(entity);
        const float swingCost =
            weapon.stamina_cost >= 0.0f
                ? weapon.stamina_cost
                : f.stamina.base_swing_cost + weapon.weight * f.stamina.swing_effort;
        const bool hasSta = em.registry().all_of<Stamina>(entity);
        const float staCurrent = hasSta ? em.registry().get<Stamina>(entity).current : 999.0f;

        bool fireAttack = false;
        if (!isLeftHand && em.registry().all_of<AutoAttackMode>(entity))
        {
            const auto& autoMode = em.registry().get<AutoAttackMode>(entity);
            if (autoMode.enabled)
                fireAttack = (weapon.swing_cooldown_remaining <= 0.0f && !isAttackLocked &&
                              !isStaggered && !isCritLocked && staCurrent >= swingCost);
        }
        if (!fireAttack && attackPressed)
            fireAttack = (weapon.swing_cooldown_remaining <= 0.0f && !isAttackLocked &&
                          !isStaggered && !isCritLocked && staCurrent >= swingCost);

        if (!fireAttack && attackPressed && weapon.swing_cooldown_remaining <= 0.0f &&
            !isAttackLocked && !isStaggered && staCurrent < swingCost)
        {
            {
                const auto& hb = snd.get("low_stamina_heartbeat");
                AudioSystem::playSfx(hb.path, hb.volume);
            }
            if (em.registry().all_of<Velocity>(entity))
            {
                auto& vel = em.registry().get<Velocity>(entity);
                vel.dx -= facing.aim_dx * 40.0f;
                vel.dy -= facing.aim_dy * 40.0f;
            }
        }

        if (!fireAttack)
            return;

        float facingX = facing.aim_dx;
        float facingY = facing.aim_dy;

        if (!isLeftHand && em.registry().all_of<AutoAttackMode>(entity) &&
            em.registry().get<AutoAttackMode>(entity).enabled)
        {
            float bestDist = std::numeric_limits<float>::max();
            for (auto [eEnemy, ai, tEnemy] : em.registry().view<AIController, Transform>().each())
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
            facing.aim_dx = (facingX != 0.0f || facingY != 0.0f) ? facingX : facing.aim_dx;
            facing.aim_dy = (facingX != 0.0f || facingY != 0.0f) ? facingY : facing.aim_dy;
        }

        if (weapon.ranged)
        {
            auto* rs = em.registry().try_get<RangedState>(entity);
            auto* inv = em.registry().try_get<Inventory>(entity);
            const int reserve = (inv != nullptr && !weapon.ammo_type.empty())
                                    ? InventoryOps::countItem(*inv, weapon.ammo_type)
                                    : 999;

            const bool magazineEmpty =
                rs != nullptr && rs->magazine_size > 0 && rs->ammo_in_magazine <= 0;
            const bool bowEmpty = rs != nullptr && rs->magazine_size == 0 && reserve <= 0;
            const bool canFire = !(rs != nullptr && rs->reloading) && !magazineEmpty && !bowEmpty;

            if (magazineEmpty && reserve > 0)
            {
                rs->reloading = true;
                rs->reload_timer = rs->reload_time;
                const auto& rl = snd.get("reload");
                AudioSystem::playSfx(rl.path, rl.volume);
            }

            if (canFire)
            {
                const float dmg = em.registry().all_of<Stats>(entity)
                                      ? computeDamage(weapon, em.registry().get<Stats>(entity), f)
                                      : weapon.base_damage;
                fireRangedWeapon(em, entity, weapon, transform, facingX, facingY, dmg, isLeftHand);

                const bool godAmmo = em.registry().ctx().get<DebugFlags>().god_mode;
                if (!godAmmo)
                {
                    if (rs != nullptr && rs->magazine_size > 0)
                        rs->ammo_in_magazine--;
                    else if (inv != nullptr && !weapon.ammo_type.empty())
                    {
                        auto* equip = em.registry().try_get<Equipment>(entity);
                        if (equip != nullptr)
                            InventoryOps::consumeItems(*inv, *equip, weapon.ammo_type, 1);
                    }
                }

                const float cooldown =
                    weapon.fire_rate > 0.0f
                        ? (1.0f / weapon.fire_rate)
                        : (em.registry().all_of<Stats>(entity)
                               ? computeSwingCooldown(weapon, em.registry().get<Stats>(entity), f)
                               : f.swing.base_swing_time + weapon.weight * f.swing.weight_scale);
                weapon.swing_cooldown_remaining = cooldown;

                em.registry().emplace_or_replace<AttackLocked>(entity,
                                                               AttackLocked{cooldown, isLeftHand});
                syncAttackAnimSpeed(em.registry(), entity, weapon, cooldown);

                if (hasSta)
                    deductStamina(em.registry(), entity, swingCost, f);

                playAttackSound(weapon, snd);
                TracyMessageL("PlayerAttack");
            }
        }
        else
        {
            const float reach = f.combat.normal_reach;
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
                    spawnHitbox(entity, hx, hy, f.combat.normal_hitbox_size, dmg, isLeftHand);
                }
                else
                {
                    spawnHitbox(entity, hx, hy, f.combat.normal_hitbox_size, weapon.base_damage,
                                isLeftHand);
                }
            }

            const float cooldown =
                em.registry().all_of<Stats>(entity)
                    ? computeSwingCooldown(weapon, em.registry().get<Stats>(entity), f)
                    : f.swing.base_swing_time + weapon.weight * f.swing.weight_scale;
            weapon.swing_cooldown_remaining = cooldown;

            const float lockDuration = cooldown * f.combat.attack_lock_fraction;
            em.registry().emplace_or_replace<AttackLocked>(entity,
                                                           AttackLocked{lockDuration, isLeftHand});
            em.registry().emplace_or_replace<AttackFeedback>(entity, AttackFeedback{0.5f});
            syncAttackAnimSpeed(em.registry(), entity, weapon, lockDuration);

            if (hasSta)
                deductStamina(em.registry(), entity, swingCost, f);

            TracyMessageL("PlayerAttack");
        }
    };

    for (auto [entity, actions, weapon, transform, facing] :
         em.registry().view<PlayerActions, Weapon, Transform, FacingDirection>().each())
    {
        // Right hand attack (E / RMB) — not affected by LMB consumed.
        attemptAttack(entity, weapon, actions.right_attack, transform, facing, false);

        // Left hand attack (Q / LMB) — suppress when LMB consumed by UI.
        if (auto* lw = em.registry().try_get<LeftWeapon>(entity))
        {
            const bool leftPressed = !em.lmb_consumed && actions.left_attack;
            attemptAttack(entity, *lw, leftPressed, transform, facing, true);
        }

        const bool isAttackLocked = em.registry().all_of<AttackLocked>(entity);
        const bool isStaggered = em.registry().all_of<Staggered>(entity);
        const bool isCritLocked = em.registry().all_of<CriticalAttacking>(entity) ||
                                  em.registry().all_of<CriticalTarget>(entity);
        const bool hasSta = em.registry().all_of<Stamina>(entity);
        const float staCurrent = hasSta ? em.registry().get<Stamina>(entity).current : 999.0f;

        // ---- Weapon skill (tries right hand, then left) ----------------------
        if (actions.skill && !isAttackLocked && !isStaggered && !isCritLocked)
        {
            auto* lw = em.registry().try_get<LeftWeapon>(entity);
            Weapon* skillWeapon = nullptr;
            if (weapon.skill_cooldown_remaining <= 0.0f)
                skillWeapon = &weapon;
            else if (lw != nullptr && lw->skill_cooldown_remaining <= 0.0f)
                skillWeapon = lw;

            if (skillWeapon != nullptr)
            {
                const float skillCost =
                    f.stamina.base_swing_cost + skillWeapon->weight * f.stamina.skill_effort;
                if (staCurrent >= skillCost)
                {
                    const float skillReach = f.combat.skill_reach;
                    const float hx = transform.x + facing.aim_dx * skillReach;
                    const float hy = transform.y + facing.aim_dy * skillReach;
                    float dmg = skillWeapon->base_damage * f.combat.skill_damage_mult;
                    if (em.registry().all_of<Stats>(entity))
                        dmg = computeDamage(*skillWeapon, em.registry().get<Stats>(entity), f) *
                              f.combat.skill_damage_mult;

                    const bool skillLos =
                        !em.tile_map.valid() ||
                        em.tile_map.hasLineOfSight(transform.x, transform.y, hx, hy);
                    if (skillLos)
                        spawnHitbox(entity, hx, hy, f.combat.skill_hitbox_size, dmg);
                    skillWeapon->skill_cooldown_remaining = f.combat.skill_cooldown;
                    em.registry().emplace_or_replace<AttackLocked>(
                        entity, AttackLocked{f.combat.skill_lock_duration});

                    if (hasSta)
                        deductStamina(em.registry(), entity, skillCost, f);

                    TracyMessageL("PlayerSkill");
                    {
                        const auto& sk = snd.get("player_skill");
                        AudioSystem::playSfx(sk.path, sk.volume);
                    }
                }
            }
        }

        // ---- Dodge -------------------------------------------------------
        const float dodgeCost = f.stamina.base_swing_cost + weapon.weight * f.stamina.dodge_effort;
        const bool canDodge =
            (actions.dodge_cooldown_remaining <= 0.0f && !isStaggered && !isCritLocked &&
             !em.registry().all_of<Dodging>(entity) && staCurrent >= dodgeCost);
        if (actions.dodge && canDodge)
        {
            const float kEngagementRadius = f.combat_ai.engagement_radius;
            const float kEngagementRadiusSq = kEngagementRadius * kEngagementRadius;

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
            const auto* lockOn = em.registry().try_get<LockOnTarget>(entity);
            const bool hasLockOn = lockOn != nullptr && em.registry().valid(lockOn->target);

            if (hasLockOn)
            {
                const bool moving = actions.move_x != 0.0f || actions.move_y != 0.0f;
                if (moving)
                {
                    const float fwd_x = facing.aim_dx;
                    const float fwd_y = facing.aim_dy;
                    const float right_x = -fwd_y;
                    const float right_y = fwd_x;
                    dodgeX = fwd_x * actions.move_y + right_x * actions.move_x;
                    dodgeY = fwd_y * actions.move_y + right_y * actions.move_x;
                    const float len = std::sqrt(dodgeX * dodgeX + dodgeY * dodgeY);
                    if (len > 0.001f)
                    {
                        dodgeX /= len;
                        dodgeY /= len;
                    }
                }
                else
                {
                    dodgeX = -facing.aim_dx;
                    dodgeY = -facing.aim_dy;
                }
            }
            else if (nearEnemy)
            {
                dodgeX = -facing.aim_dx;
                dodgeY = -facing.aim_dy;
            }
            else
            {
                const bool moving = actions.move_x != 0.0f || actions.move_y != 0.0f;
                dodgeX = moving ? actions.move_x : facing.aim_dx;
                dodgeY = moving ? actions.move_y : facing.aim_dy;
            }

            if (em.registry().all_of<Velocity>(entity))
            {
                auto& vel = em.registry().get<Velocity>(entity);
                vel.dx = dodgeX * f.combat.dodge_speed;
                vel.dy = dodgeY * f.combat.dodge_speed;
            }

            TracyMessageL("PlayerDodge");
            {
                const auto& dg = snd.get("player_dodge");
                AudioSystem::playSfx(dg.path, dg.volume);
            }
            em.registry().emplace<Dodging>(entity, Dodging{f.dodge.duration});
            actions.dodge_cooldown_remaining = f.dodge.cooldown;

            if (hasSta)
                deductStamina(em.registry(), entity, dodgeCost, f);
        }

        if (actions.dodge && !canDodge && actions.dodge_cooldown_remaining <= 0.0f &&
            !isStaggered && !em.registry().all_of<Dodging>(entity) && staCurrent < dodgeCost)
        {
            {
                const auto& hb = snd.get("low_stamina_heartbeat");
                AudioSystem::playSfx(hb.path, hb.volume);
            }
            if (em.registry().all_of<Velocity>(entity))
            {
                auto& vel = em.registry().get<Velocity>(entity);
                vel.dx -= facing.aim_dx * 40.0f;
                vel.dy -= facing.aim_dy * 40.0f;
            }
        }

        // ---- Manual reload (R key) — tries right hand, then left --------------
        if (actions.reload && em.registry().all_of<RangedState>(entity))
        {
            const auto* lw = em.registry().try_get<LeftWeapon>(entity);
            const Weapon* reloadWeapon = nullptr;
            if (weapon.ranged)
                reloadWeapon = &weapon;
            else if (lw != nullptr && lw->ranged)
                reloadWeapon = lw;

            if (reloadWeapon != nullptr)
            {
                auto& rs = em.registry().get<RangedState>(entity);
                const auto* inv = em.registry().try_get<Inventory>(entity);
                const int reserve = (inv != nullptr && !reloadWeapon->ammo_type.empty())
                                        ? InventoryOps::countItem(*inv, reloadWeapon->ammo_type)
                                        : 999;
                const bool godMode = em.registry().ctx().get<DebugFlags>().god_mode;
                if (!rs.reloading && rs.magazine_size > 0 &&
                    (godMode || (rs.ammo_in_magazine < rs.magazine_size && reserve > 0)))
                {
                    rs.reloading = true;
                    rs.reload_timer = rs.reload_time;
                    {
                        const auto& rl = snd.get("reload");
                        AudioSystem::playSfx(rl.path, rl.volume);
                    }
                }
            }
        }

        // ---- Two-handed toggle (Left Alt) ------------------------------------
        // Cycles: all 1H → right 2H → left 2H → all 1H.
        // Only offers a state if that hand's weapon supports two-handed.
        if (actions.toggle_two_hand)
        {
            auto* lw = em.registry().try_get<LeftWeapon>(entity);
            const bool rightCan = weapon.two_handed;
            const bool leftCan = lw != nullptr && lw->two_handed;

            if (weapon.two_handed_active)
            {
                weapon.two_handed_active = false;
                if (leftCan && lw != nullptr)
                    lw->two_handed_active = true;
            }
            else if (lw != nullptr && lw->two_handed_active)
            {
                lw->two_handed_active = false;
            }
            else
            {
                if (rightCan)
                    weapon.two_handed_active = true;
                else if (leftCan && lw != nullptr)
                    lw->two_handed_active = true;
            }
        }
    }

    // --- 6. Enemy attacks — hitbox-based, range-gated by attack_radius ------
    // Enemies spawn a hitbox toward the player when in range, just like the
    // player does. Weapon reach = 24px offset from enemy center.
    // DamageSystem Path 1 (hitbox→health) handles the actual damage application.
    {
        entt::entity playerEnt = entt::null;
        Transform playerTransform{};
        for (auto e : em.registry().view<PlayerActions>())
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

                // Token gate: Attack-state enemies need a token to swing.
                if (ai.state == AIController::State::Attack)
                {
                    const auto& pool = em.registry().ctx().get<AttackTokenPool>();
                    bool hasToken = false;
                    for (auto h : pool.holders)
                    {
                        if (h == entity)
                        {
                            hasToken = true;
                            break;
                        }
                    }
                    if (!hasToken)
                        continue;
                }

                // Stamina gate — can't swing without enough stamina.
                const float swingCost =
                    f.stamina.base_swing_cost + weapon.weight * f.stamina.swing_effort;
                if (em.registry().all_of<Stamina>(entity) &&
                    em.registry().get<Stamina>(entity).current < swingCost)
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
                spawnHitbox(entity, transform.x + nx * f.combat_ai.enemy_reach,
                            transform.y + ny * f.combat_ai.enemy_reach, f.combat.normal_hitbox_size,
                            dmg);

                // Reset swing cooldown. AI attack_cooldown sets a minimum cadence.
                float cooldown =
                    em.registry().all_of<Stats>(entity)
                        ? computeSwingCooldown(weapon, em.registry().get<Stats>(entity), f)
                        : 1.0f;
                if (ai.attack_cooldown > 0.0f)
                    cooldown = std::max(cooldown, ai.attack_cooldown);
                weapon.swing_cooldown_remaining = cooldown;

                // Token holder keeps its token through swing cooldown so it stays
                // at the attack ring and swings again when ready. Token is only
                // freed on death or state change (manageAttackTokens cleanup).

                // Update facing direction toward player.
                if (em.registry().all_of<FacingDirection>(entity))
                {
                    auto& fd = em.registry().get<FacingDirection>(entity);
                    fd.dx = nx;
                    fd.dy = ny;
                    fd.aim_dx = nx;
                    fd.aim_dy = ny;
                }

                // Stamina cost — same formula as player swings.
                if (em.registry().all_of<Stamina>(entity))
                    deductStamina(em.registry(), entity, swingCost, f);

                // Yellow swing flash.
                em.registry().emplace_or_replace<AttackFeedback>(entity, AttackFeedback{0.5f});
            }
        }
    }
}
