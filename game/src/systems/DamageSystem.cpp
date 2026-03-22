#include "systems/DamageSystem.h"

#include "ecs/Components.h"
#include "ecs/GameComponents.h"
#include "ecs/GameConfig.h"
#include "systems/AudioSystem.h"
#include "systems/CombatSystem.h" // computeDamage

#include <cmath>
#include <iostream>
#include <tracy/Tracy.hpp>

// ---------------------------------------------------------------------------
// Internal helpers
// ---------------------------------------------------------------------------

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

    // Shield block / parry.
    if (reg.all_of<Shield>(target))
    {
        auto& shield = reg.get<Shield>(target);
        if (shield.blocking && shield.guard_health > 0.0f)
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

    AudioSystem::playSfx(snd.hit.path, snd.hit.volume);
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

        // Poise damage scales from attacker weapon weight; bare-fist baseline = 1.
        float poiseDmg = 1.0f;
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
        // Identify which entity is the hitbox and which is the target.
        entt::entity hitboxEnt = entt::null;
        entt::entity targetEnt = entt::null;

        if (reg.all_of<Hitbox>(ev.a) && reg.all_of<Health>(ev.b))
        {
            hitboxEnt = ev.a;
            targetEnt = ev.b;
        }
        else if (reg.all_of<Hitbox>(ev.b) && reg.all_of<Health>(ev.a))
        {
            hitboxEnt = ev.b;
            targetEnt = ev.a;
        }

        if (hitboxEnt == entt::null)
            continue;

        const auto& hb = reg.get<Hitbox>(hitboxEnt);

        // Don't damage the owner of the hitbox.
        if (targetEnt == hb.owner)
            continue;

        // Skip dead targets (already killed this frame).
        if (reg.all_of<Dead>(targetEnt))
            continue;

        applyDamage(em, targetEnt, hb.damage, hb.owner);
    }
    // Path 2 (enemy direct overlap → player) removed. Enemies now spawn hitboxes
    // via CombatSystem section 6, which flows through Path 1 above.
}
