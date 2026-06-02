#include "gameplay/BossDispatcher.h"

#include "AppStateGlobal.h"
#include "WallClock.h"
#include "anim/ClipRegistry.h"
#include "anim/SkeletalAssets.h"
#include "audio/Audio.h"
#include "combat/CombatLog.h"
#include "gameplay/Actor.h"
#include "gameplay/Enemies.h"
#include "gameplay/EnemyArchetype.h"
#include "gameplay/Perception.h"
#include "world/JsonRegion.h"
#include "world/Region.h"

#include <cstdio>
#include <string>
#include <utility>

namespace selva::gameplay
{

namespace
{

// Split "verb:arg" on the first ':'. If no ':', verb = whole string,
// arg = "". Convention is documented in
// games/selva-oscura/docs/design/ideas/boss_backend.md section 3 +
// BossDispatcher.h.
std::pair<std::string, std::string> splitVerb(const std::string& payload)
{
    const auto pos = payload.find(':');
    if (pos == std::string::npos)
        return {payload, ""};
    return {payload.substr(0, pos), payload.substr(pos + 1)};
}

// Find an EnemySpawnDecl by id in the current region's enemy_spawns
// list. Returns nullptr if the current region is not a JsonRegion
// (no spawns concept) or if no decl matches.
const EnemySpawnDecl* findCurrentRegionSpawnDecl(const std::string& spawn_id)
{
    engine::world::Region* cur = engine::world::currentRegionPtr();
    if (cur == nullptr)
        return nullptr;
    auto* json_region = dynamic_cast<selva::world::JsonRegion*>(cur);
    if (json_region == nullptr)
        return nullptr;
    for (const auto& d : json_region->enemySpawnDecls())
    {
        if (d.id == spawn_id)
            return &d;
    }
    return nullptr;
}

// Find an already-spawned boss actor by its spawn_decl_id. Returns
// the actor pool index, or -1 if not found. Used by the engage verb.
int findActorIndexBySpawnDeclId(const std::string& spawn_decl_id)
{
    auto& pool = actors();
    for (std::size_t i = 0; i < pool.size(); ++i)
    {
        if (pool[i].spawn_decl_id == spawn_decl_id && pool[i].is_boss && !pool[i].is_dead)
            return static_cast<int>(i);
    }
    return -1;
}

// Verb: "spawn:<spawn_decl_id>". Pattern A trigger-spawned boss.
// Spawns the named decl now. If the decl was tagged with spawn_trigger_id
// it would have been skipped at boot; this is when it actually spawns.
void handleSpawnVerb(const std::string& spawn_id)
{
    const EnemySpawnDecl* decl = findCurrentRegionSpawnDecl(spawn_id);
    if (decl == nullptr)
    {
        selva::combat::combatLog("[boss-dispatch] spawn '{}' no matching enemy_spawns entry",
                                 spawn_id);
        return;
    }
    engine::world::Region* cur = engine::world::currentRegionPtr();
    const std::string region_id = (cur != nullptr) ? cur->regionId() : std::string{};
    spawnEnemyFromDecl(region_id, *decl);
    // After spawn, find the actor we just inserted and set it active
    // via the single setBossState funnel (writes mirrors + audio bed
    // + logs the transition).
    const int idx = findActorIndexBySpawnDeclId(spawn_id);
    if (idx >= 0)
    {
        Actor& a = actors()[idx];
        setBossState(a, BossState::Engaged);
        selva::combat::combatLog("[boss-dispatch] spawned + engaged '{}' (pool idx {})",
                                 spawn_id, idx);
    }
}

// Verb: "engage:<spawn_decl_id>". Pattern B already-there boss
// (e.g. Lupa sitting at the chapel). Finds the existing actor,
// clears its current_boss_state (combat AI begins ticking), sets
// GameState.active_boss_*. The engage_clip play-once is handled in
// the AI tick as a side-effect of detecting the state-clear edge.
void handleEngageVerb(const std::string& spawn_id)
{
    const int idx = findActorIndexBySpawnDeclId(spawn_id);
    if (idx < 0)
    {
        selva::combat::combatLog(
            "[boss-dispatch] engage '{}' no matching alive boss in pool", spawn_id);
        return;
    }
    Actor& a = actors()[idx];
    if (a.boss_state == BossState::Engaged)
    {
        // Already engaged. Idempotent -- trigger probably re-fired
        // due to player leaving and re-entering. No-op.
        return;
    }
    // Force-aggro: the engage trigger IS the engagement, so the BT's
    // Combat branch (IfAwarenessAtLeast Combat) must pass immediately.
    // Without this, perception starts at Unaware and Lupa idles until
    // her vision cone happens to catch the player. Mirrors the
    // applyEnemyHitReact rule "getting hit always engages."
    a.perception.awareness = Awareness::Combat;
    a.perception.last_seen_time = selva::wallClock();
    a.perception.last_known_player_pos = selva::gameplay::player().pos;
    a.perception.suspicious_sighting_count = 0;
    // SINGLE source of truth: setBossState handles current_boss_state
    // clear, active_boss_idx/id mirror writes, audio bed push, and
    // logs the [boss-state] transition.
    setBossState(a, BossState::Engaged);
    // Play the engage clip as a one-shot. After it ends, the
    // sampler returns to the default idle and tickEnemyDecision
    // begins firing combat actions.
    if (a.archetype != nullptr && !a.archetype->engage_clip.empty())
    {
        const auto& clips = selva::anim::clipsByKey(a.skeleton_id);
        const auto* clip = clips.get(a.archetype->engage_clip);
        if (clip != nullptr && clip->isLoaded())
        {
            std::fprintf(stderr,
                         "[boss-dispatch] engage skel='%s' clip='%s' "
                         "clip.trackCount=%d clip.duration=%.3f\n",
                         a.skeleton_id.c_str(), a.archetype->engage_clip.c_str(),
                         clip->trackCount(), clip->duration());
            std::fflush(stderr);
            selva::anim::PoseSampler::OneShotOptions opts;
            a.sampler.playOneShot(*clip, /*blend_in_seconds=*/0.15f,
                                  /*blend_out_seconds=*/0.20f,
                                  selva::anim::PoseSampler::BodyMask::Full,
                                  /*start_time_seconds=*/0.0f, /*playback_rate=*/1.0f, opts);
        }
        else
        {
            selva::combat::combatLog(
                "[boss-dispatch] engage_clip '{}' missing on skeleton '{}' (boss '{}')",
                a.archetype->engage_clip, a.skeleton_id, spawn_id);
        }
    }
    selva::combat::combatLog("[boss-dispatch] engaged '{}' (pool idx {}); engage_clip='{}'",
                             spawn_id, idx,
                             (a.archetype != nullptr) ? a.archetype->engage_clip : "");
}

} // namespace

void dispatchCustomTrigger(const engine::world::RegionTrigger& trigger)
{
    const auto [verb, arg] = splitVerb(trigger.action_payload);
    std::fprintf(stderr, "[boss-dispatch] trigger '%s' verb='%s' arg='%s'\n", trigger.id.c_str(),
                 verb.c_str(), arg.c_str());
    std::fflush(stderr);
    if (verb == "spawn")
        handleSpawnVerb(arg);
    else if (verb == "engage")
        handleEngageVerb(arg);
    else
        selva::combat::combatLog("[boss-dispatch] unknown verb '{}' (trigger '{}')", verb,
                                 trigger.id);
}

} // namespace selva::gameplay
