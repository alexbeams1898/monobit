#include "systems/DamageSystem.h"

#include "ecs/Components.h"
#include "ecs/GameComponents.h"
#include "ecs/GameConfig.h"
#include "systems/AudioSystem.h"
#include "systems/CombatSystem.h" // computeDamage
#include "systems/WeaponXPSystem.h"

#include <cmath>
#include <iostream>
#include <random>
#include <tracy/Tracy.hpp>

static std::mt19937& damageRng()
{
    static std::mt19937 gen{std::random_device{}()};
    return gen;
}

// ---------------------------------------------------------------------------
// Internal helpers
// ---------------------------------------------------------------------------

namespace
{

// Resolve which entity is the hitbox and which is the target in a collision pair.
// Returns {hitbox, target} or {null, null} if the pair is not a hitbox-vs-health collision.
struct HitPair
{
    entt::entity hitbox = entt::null;
    entt::entity target = entt::null;
};

HitPair resolveHitPair(entt::registry& reg, entt::entity a, entt::entity b)
{
    if (reg.all_of<Hitbox>(a) && reg.all_of<Health>(b))
        return {a, b};
    if (reg.all_of<Hitbox>(b) && reg.all_of<Health>(a))
        return {b, a};
    return {};
}

// Grant weapon XP trickle to the player on a successful hit.
// Gathers enemy stats/health/weapon/loot to compute enemy power, then grants scaled XP.
void grantHitXP(EntityManager& em, entt::entity target)
{
    auto& reg = em.registry();
    const FormulaConfig& fc = reg.ctx().get<FormulaConfig>();

    int enemyHp = 0;
    float enemyDmg = 0.0f;
    int enemyStats = 0;
    int enemyLevel = 1;

    if (reg.all_of<Health>(target))
        enemyHp = reg.get<Health>(target).max;
    if (reg.all_of<Weapon>(target))
        enemyDmg = reg.get<Weapon>(target).base_damage;
    if (reg.all_of<Stats>(target))
    {
        const auto& s = reg.get<Stats>(target);
        enemyStats = s.str + s.dex + s.end + s.lck;
    }
    if (reg.all_of<Loot>(target))
        enemyLevel = reg.get<Loot>(target).level;

    const float power =
        WeaponXPSystem::computeEnemyPower(enemyLevel, enemyHp, enemyDmg, enemyStats, fc);
    WeaponXPSystem::grantXP(em, power, fc.weapon_xp.hit_multiplier);
}

} // anonymous namespace

// Compute the stat-requirement penalty factor: exp(-deficit * penaltyRate).
// Returns a value in (0, 1]; 1.0 = no penalty.
static float computePenalty(const Weapon& w, const Stats& s, const FormulaConfig& f)
{
    const int strDeficit = std::max(0, w.str_requirement - s.str);
    const int dexDeficit = std::max(0, w.dex_requirement - s.dex);
    return std::exp(-static_cast<float>(strDeficit) * f.stat_requirement.penalty_rate) *
           std::exp(-static_cast<float>(dexDeficit) * f.stat_requirement.penalty_rate);
}

// Compute DEF percentage (0–cap) from Body material + stats + level.
// Returns a value in [0, cap]; applies as `finalDmg = max(1, raw * (1 - DEF/100))`.
static float computeDef(const Stats& s, int baseDef, int level, const FormulaConfig& f)
{
    const float def =
        std::floor(static_cast<float>(baseDef) + static_cast<float>(s.str) * f.defense.str_scale +
                   static_cast<float>(s.end) * f.defense.end_scale +
                   static_cast<float>(level) * f.defense.level_scale);
    return std::min(def, f.defense.cap);
}

// Apply incoming damage to a target entity, respecting Dodging i-frames,
// Shield blocking/parry, DEF, and stat-requirement penalty.
// Returns true if damage was applied (false = blocked / i-frames / parried).
// NOLINTNEXTLINE(readability-function-cognitive-complexity)
static bool applyDamage(EntityManager& em, entt::entity target, float rawDamage,
                        entt::entity attacker)
{
    auto& reg = em.registry();
    const FormulaConfig& f = reg.ctx().get<FormulaConfig>();
    const SoundConfig& snd = reg.ctx().get<SoundConfig>();

    // I-frames: ignore if target is currently dodging.
    if (reg.all_of<Dodging>(target))
        return false;

    // Staggered attacker can't hit (e.g. parried last swing).
    if (attacker != entt::null && reg.all_of<Staggered>(attacker))
        return false;

    // Shield block / parry (with frontal arc check).
    if (reg.all_of<Shield>(target))
    {
        auto& shield = reg.get<Shield>(target);

        // Frontal arc: attacks from behind (>90 degrees from facing) bypass the shield.
        bool fromFront = true;
        if (shield.blocking && attacker != entt::null && reg.all_of<Transform>(attacker) &&
            reg.all_of<Transform, FacingDirection>(target))
        {
            const auto& tgt = reg.get<Transform>(target);
            const auto& atk = reg.get<Transform>(attacker);
            const auto& face = reg.get<FacingDirection>(target);
            const float toAtkX = atk.x - tgt.x;
            const float toAtkY = atk.y - tgt.y;
            const float len = std::sqrt(toAtkX * toAtkX + toAtkY * toAtkY);
            if (len > 0.0f)
            {
                const float dot = (toAtkX / len) * face.dx + (toAtkY / len) * face.dy;
                fromFront = (dot > 0.0f); // dot <= 0 = behind the defender
            }
        }

        if (shield.blocking && shield.guard_health > 0.0f && fromFront)
        {
            // Parry window: negate damage and stagger the attacker.
            if (reg.all_of<Parrying>(target))
            {
                if (attacker != entt::null)
                {
                    reg.emplace_or_replace<Staggered>(attacker, Staggered{0.5f});
                    AudioSystem::playSfx(snd.parry.path, snd.parry.volume);
                    std::cout << "[DamageSystem] Parry! Attacker staggered.\n";
                }
                return false; // damage fully negated
            }

            // Normal block: reduce guard health.
            shield.guard_health -= rawDamage;
            if (shield.guard_health <= 0.0f)
            {
                shield.guard_health = 0.0f;
                reg.emplace_or_replace<Staggered>(target, Staggered{1.0f});
                std::cout << "[DamageSystem] Guard broken!\n";
            }
            return false; // damage absorbed by shield
        }
    }

    // Stat-requirement penalty on the attacker's weapon.
    float penalty = 1.0f;
    if (attacker != entt::null && reg.all_of<Weapon, Stats>(attacker))
    {
        penalty = computePenalty(reg.get<Weapon>(attacker), reg.get<Stats>(attacker), f);
    }
    rawDamage *= penalty;

    // Flat armor DR (applied before percentage-based DEF).
    if (reg.all_of<ArmorStats>(target))
    {
        rawDamage = std::max(0.0f, rawDamage - reg.get<ArmorStats>(target).total_defense);
    }

    // DEF reduction on the target.
    if (reg.all_of<Health, Stats>(target))
    {
        int level = 1;
        if (reg.all_of<Experience>(target))
            level = reg.get<Experience>(target).level;

        const auto& stats = reg.get<Stats>(target);
        const int baseDef = reg.all_of<Body>(target) ? reg.get<Body>(target).base_defense : 0;
        const float def = computeDef(stats, baseDef, level, f);
        rawDamage = std::max(1.0f, rawDamage * (1.0f - def / 100.0f));
    }

    const int dmg = static_cast<int>(rawDamage);
    if (!reg.all_of<Health>(target))
        return false;

    auto& health = reg.get<Health>(target);
    health.current = std::max(0, health.current - dmg);

    // Trigger red damage flash on the target.
    reg.emplace_or_replace<DamageFeedback>(target, DamageFeedback{0.2f});

    if (!snd.hit_paths.empty())
    {
        auto dist = std::uniform_int_distribution<size_t>(0, snd.hit_paths.size() - 1);
        AudioSystem::playSfx(snd.hit_paths[dist(damageRng())], snd.hit.volume);
    }
    else
    {
        AudioSystem::playSfx(snd.hit.path, snd.hit.volume);
    }

    // Per-entity hit sound layered on top of the global hit SFX.
    if (reg.all_of<HitSound>(target))
    {
        const auto& hs = reg.get<HitSound>(target);
        std::uniform_real_distribution<float> pitchDist(hs.min_pitch, hs.max_pitch);
        AudioSystem::playSfx(hs.path, hs.volume, pitchDist(damageRng()));
    }
    TracyMessageL("EntityDamaged");
    const std::string name = reg.all_of<Tag>(target) ? reg.get<Tag>(target).name : "entity";
    std::cout << "[DamageSystem] " << name << " took " << dmg << " damage (" << health.current
              << "/" << health.max << " hp)\n";

    if (health.current <= 0 && !reg.all_of<Dead>(target))
    {
        // Set death timer from animation duration so death anim plays out.
        float deathTimer = 0.0f;
        if (reg.all_of<Animation>(target))
        {
            const auto& anim = reg.get<Animation>(target);
            const auto& deathState = anim.states[static_cast<int>(AnimState::Death)];
            deathTimer = static_cast<float>(deathState.frames) * deathState.duration;
        }
        reg.emplace<Dead>(target, Dead{deathTimer});
        AudioSystem::playSfx(snd.death.path, snd.death.volume);
        TracyMessageL("EntityDied");
        std::cout << "[DamageSystem] " << name << " died.\n";
    }

    // Poise damage — accumulate per hit; stagger when threshold is breached.
    // Entities without a Poise component are skipped (e.g. walls, pickups).
    if (reg.all_of<Poise>(target))
    {
        auto& poise = reg.get<Poise>(target);
        poise.decay_timer = 0.0f; // reset decay window on every hit

        // Poise damage scales from attacker weapon weight.
        float poiseDmg = 0.3f; // bare-fist baseline (no weapon component at all)
        if (attacker != entt::null && reg.all_of<Weapon>(attacker))
            poiseDmg = reg.get<Weapon>(attacker).weight * f.poise.weight_scale;

        const std::string targetName =
            reg.all_of<Tag>(target) ? reg.get<Tag>(target).name : "entity";

        if (poise.max <= 0.0f)
        {
            // Zero poise (no armor): any hit staggers.
            if (!reg.all_of<Staggered>(target))
            {
                reg.emplace<Staggered>(target, Staggered{f.poise.stagger_duration});
                std::cout << "[Poise] " << targetName << ": staggered (no poise)\n";
            }
        }
        else
        {
            poise.current += poiseDmg;
            std::cout << "[Poise] " << targetName << ": " << poise.current << "/" << poise.max
                      << " (++" << poiseDmg << ")\n";
            if (poise.current >= poise.max)
            {
                poise.current = 0.0f;
                reg.emplace_or_replace<Staggered>(target, Staggered{f.poise.stagger_duration});
                std::cout << "[Poise] " << targetName << ": staggered (poise broken)\n";
            }
        }
    }

    return true;
}

// ---------------------------------------------------------------------------

void DamageSystem::update(EntityManager& em)
{
    ZoneScopedN("DamageSystem");

    auto& waveState = em.registry().ctx().get<WaveState>();
    if (waveState.phase == WaveState::Phase::GameOver)
        return;

    auto& reg = em.registry();

    // Propagate PlayerActions.block_held → Shield.blocking for all shielded entities.
    for (auto [entity, actions, shield] : reg.view<PlayerActions, Shield>().each())
        shield.blocking = actions.block_held;

    // --- Path 1: Hitbox → Health entity -----------------------------------
    for (const auto& ev : em.collision_events)
    {
        const auto [hitboxEnt, targetEnt] = resolveHitPair(reg, ev.a, ev.b);
        if (hitboxEnt == entt::null)
            continue;

        auto& hb = reg.get<Hitbox>(hitboxEnt);

        // Don't damage the owner of the hitbox.
        if (targetEnt == hb.owner)
            continue;

        // Skip dead targets (already killed this frame).
        if (reg.all_of<Dead>(targetEnt))
            continue;

        if (applyDamage(em, targetEnt, hb.damage, hb.owner))
        {
            hb.hit_something = true;

            // Grant weapon XP trickle on hit (player-only).
            if (hb.owner != entt::null && reg.all_of<PlayerActions>(hb.owner))
                grantHitXP(em, targetEnt);
        }
    }
    // Path 2 (enemy direct overlap → player) removed. Enemies now spawn hitboxes
    // via CombatSystem section 6, which flows through Path 1 above.
}
