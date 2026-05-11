#include "gameplay/Enemies.h"

#include "Tunables.h"
#include "WallClock.h"
#include "anim/AnimationClip.h"
#include "anim/ClipRegistry.h"
#include "anim/SkeletalAssets.h"
#include "combat/CombatLog.h"
#include "world/Collision.h"

#include <algorithm>
#include <cmath>

namespace selva::gameplay
{

namespace
{

// Severity tier + cooldown + respawn knobs live in Tunables so
// they're hot-reloadable from the F1 panel and serialize with the
// rest of the game's feel parameters. Read fresh each call.

// Generic hit-react clip set, baked from
// games/selva-oscura/assets/characters/x_bot/source/combat/unarmed/.
// All authored on the shared X_Bot rig; the figura umana rule means
// every enemy in the bestiary uses these same clips.
//
// Enemies idle in combat stance — flinch / hit-react clips were
// authored against this pose. Using standard_idle produces visual
// seams at the hip when blending. Hostile enemies are always in
// combat stance.
constexpr const char* kEnemyIdleClipName = "unarmed_combat_idle";
constexpr const char* kFlinchFrontClipName = "flinch_front";
constexpr const char* kFlinchBackClipName = "flinch_back";
constexpr const char* kFlinchLeftClipName = "flinch_left";
constexpr const char* kFlinchRightClipName = "flinch_right";
constexpr const char* kHitReactMediumClipName = "hit_react_medium";
constexpr const char* kHitReactHeavyClipName = "hit_react_heavy";
constexpr const char* kDeathClipName = "death";

// Spawn an enemy actor into the shared pool. Caller must have
// already initialized the pool (player at index 0).
void spawnEnemyActor(float x, float z, float yaw)
{
    Actor e;
    e.controller = Controller::AI_Stationary;
    e.faction = Faction::Hostile;
    e.pos = glm::vec3(x, 0.0f, z);
    e.yaw = yaw;
    e.spawn_pos = e.pos;
    e.spawn_yaw = yaw;
    e.sampler = selva::anim::createPoseSampler(selva::anim::skeleton(), selva::anim::playerMesh());
    initActorPools(e.hp, e.stamina, e.body, e.stats);
    if (const auto* idle = selva::anim::clips().get(kEnemyIdleClipName);
        idle != nullptr && idle->isLoaded())
        e.sampler.update(*idle, 0.0f, 0.0f);
    actors().push_back(std::move(e));
}

// Pick the directional flinch clip from the hit normal rotated into
// the target's local frame. Convention: target yaw=0 faces -Z.
// Local forward = -Z; local right = +X. world_normal points
// attacker->target, so its projection onto the target's local axes
// tells us which side of the body was struck.
const char* pickDirectionalFlinchClip(float target_yaw, const glm::vec3& world_normal)
{
    const float cy = std::cos(-target_yaw);
    const float sy = std::sin(-target_yaw);
    const float local_x = cy * world_normal.x + sy * world_normal.z;
    const float local_z = -sy * world_normal.x + cy * world_normal.z;
    if (std::abs(local_x) > std::abs(local_z))
        return (local_x > 0.0f) ? kFlinchRightClipName : kFlinchLeftClipName;
    return (local_z < 0.0f) ? kFlinchFrontClipName : kFlinchBackClipName;
}

// Per-actor death + respawn handling. Returns true if the actor
// should be skipped in this frame's idle update (dead and holding
// the death pose). Called once per AI actor per tick.
bool tickDeathLifecycle(Actor& a, float dt, const selva::anim::AnimationClip* death_clip)
{
    if (!a.is_dead)
        return false;
    const float respawn_delay = selva::tuning::current().enemy_respawn_after_death_seconds;
    if (a.death_time > 0.0f && (selva::wallClock() - a.death_time) >= respawn_delay)
    {
        selva::combat::combatLog("[enemy-respawn] respawning actor at t=%.3f\n",
                                 selva::wallClock());
        a.is_dead = false;
        a.death_time = -1.0f;
        a.last_damage_time = -1.0f;
        a.last_hit_react_time = -1.0f;
        a.pos = a.spawn_pos;
        a.yaw = a.spawn_yaw;
        initActorPools(a.hp, a.stamina, a.body, a.stats);
        a.sampler.releaseOneShot();
        return false;
    }
    if (death_clip != nullptr && death_clip->isLoaded())
        a.sampler.update(*death_clip, dt, 0.0f, /*loops=*/false);
    return true;
}

} // namespace

void initHubEnemies()
{
    // Pool must already have the player at index 0; we append.
    // First enemy: stationary humanoid 6m north of the clearing.
    spawnEnemyActor(0.0f, -6.0f, 0.0f);
}

void shutdownHubEnemies()
{
    // Remove every AI actor from the pool; leave the player intact.
    auto& pool = actors();
    pool.erase(std::remove_if(pool.begin() + (pool.empty() ? 0 : 1), pool.end(),
                              [](const Actor& a) { return a.controller != Controller::Input; }),
               pool.end());
}

void tickEnemies(float dt)
{
    const auto* idle = selva::anim::clips().get(kEnemyIdleClipName);
    const auto* death = selva::anim::clips().get(kDeathClipName);
    for (auto& a : actors())
    {
        if (a.controller == Controller::Input)
            continue;
        if (tickDeathLifecycle(a, dt, death))
        {
            applyActorClipHipDelta(a);
            continue;
        }
        if (idle != nullptr && idle->isLoaded())
            a.sampler.update(*idle, dt, 0.0f);
        applyActorClipHipDelta(a);
    }
}

std::vector<Actor*> enemies()
{
    std::vector<Actor*> out;
    for (auto& a : actors())
    {
        if (a.controller != Controller::Input)
            out.push_back(&a);
    }
    return out;
}

void playEnemyHitReact(int index, int damage, const glm::vec3& world_normal)
{
    auto& pool = actors();
    // `index` is the legacy "enemy index" — i.e. position within the
    // filtered enemy view. Translate to pool index by counting.
    int seen = 0;
    Actor* target = nullptr;
    for (auto& a : pool)
    {
        if (a.controller == Controller::Input)
            continue;
        if (seen == index)
        {
            target = &a;
            break;
        }
        ++seen;
    }
    if (target == nullptr)
        return;
    Actor& e = *target;
    if (e.is_dead)
        return;

    // Death takes priority over any other reaction.
    if (e.hp.current <= 0)
    {
        const auto* death_clip = selva::anim::clips().get(kDeathClipName);
        if (death_clip != nullptr && death_clip->isLoaded())
        {
            selva::anim::PoseSampler::OneShotOptions opts;
            opts.clip_key = kDeathClipName;
            opts.freeze_last = true;
            e.sampler.playOneShot(*death_clip, /*blend_in=*/0.10f, /*blend_out=*/0.20f,
                                  selva::anim::PoseSampler::BodyMask::Full,
                                  /*start_time=*/0.0f, /*playback_rate=*/1.0f, opts);
        }
        e.is_dead = true;
        e.death_time = selva::wallClock();
        selva::combat::combatLog("[enemy-death] enemy[%d] died\n", index);
        return;
    }

    const auto& tun = selva::tuning::current();
    const float now = selva::wallClock();
    if (e.last_hit_react_time > 0.0f &&
        (now - e.last_hit_react_time) < tun.hit_react_cooldown_seconds)
        return;

    const char* clip_name = nullptr;
    float blend_in = 0.04f;
    float blend_out = 0.15f;
    if (static_cast<float>(damage) >= tun.hit_react_heavy_threshold)
    {
        clip_name = kHitReactHeavyClipName;
        blend_in = 0.08f;
        blend_out = 0.20f;
    }
    else if (static_cast<float>(damage) >= tun.hit_react_medium_threshold)
    {
        clip_name = kHitReactMediumClipName;
        blend_in = 0.06f;
        blend_out = 0.15f;
    }
    else
    {
        clip_name = pickDirectionalFlinchClip(e.yaw, world_normal);
    }

    const auto* clip = selva::anim::clips().get(clip_name);
    if (clip == nullptr || !clip->isLoaded())
        return;
    selva::anim::PoseSampler::OneShotOptions opts;
    opts.clip_key = clip_name;
    e.sampler.playOneShot(*clip, blend_in, blend_out,
                          selva::anim::PoseSampler::BodyMask::Full,
                          /*start_time=*/0.0f, /*playback_rate=*/1.0f, opts);
    e.last_hit_react_time = now;
    e.last_damage_time = now;
    selva::combat::combatLog("[hit-react] enemy[%d] dmg=%d -> clip=%s\n", index, damage, clip_name);
}

} // namespace selva::gameplay
