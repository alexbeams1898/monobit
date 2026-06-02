#include "gameplay/BossState.h"

#include "AppStateGlobal.h"
#include "WallClock.h"
#include "anim/AnimationClip.h"
#include "anim/ClipRegistry.h"
#include "anim/SkeletalAssets.h"
#include "audio/Audio.h"
#include "combat/CombatLog.h"
#include "gameplay/Actor.h"
#include "gameplay/EnemyArchetype.h"

#include <cstdio>

namespace selva::gameplay
{

const char* bossStateName(BossState s)
{
    switch (s)
    {
    case BossState::Dormant: return "Dormant";
    case BossState::Engaged: return "Engaged";
    case BossState::Disengaged: return "Disengaged";
    case BossState::Dying: return "Dying";
    case BossState::Felled: return "Felled";
    }
    return "?";
}

namespace
{

// Walk the actor pool to find `actor`'s full-pool index. Returns -1 if
// not found (shouldn't happen for live actors). active_boss_idx
// expects full-pool index (not the enemy-filtered index from
// enemyIndex()).
int poolIndexOfActor(const Actor& actor)
{
    const auto& pool = actors();
    for (std::size_t i = 0; i < pool.size(); ++i)
    {
        if (&pool[i] == &actor)
            return static_cast<int>(i);
    }
    return -1;
}

// Mirror writes that follow each transition. Centralized here so the
// mirror logic is one block, not duplicated per transition. The legacy
// fields (Actor.current_boss_state string, GameState.active_boss_idx /
// id) are KEPT during migration so callsites reading them keep working.
//
//   Dormant    -> current_boss_state = archetype.initial_state (so the
//                  AI-tick boss-skip gate fires); active_boss_*
//                  cleared.
//   Engaged    -> current_boss_state empty (AI runs); active_boss_*
//                  set to this actor; audio bed pushed.
//   Disengaged -> current_boss_state empty; active_boss_* cleared;
//                  audio bed popped if pushed.
//   Felled     -> current_boss_state empty; active_boss_* cleared;
//                  audio bed popped.
void writeMirrors(Actor& actor, BossState prev, BossState next)
{
    auto& gs = selva::gameState();
    const std::string id_for_log =
        actor.spawn_decl_id.empty() ? actor.spawn_id : actor.spawn_decl_id;

    switch (next)
    {
    case BossState::Dormant:
        actor.current_boss_state =
            (actor.archetype != nullptr) ? actor.archetype->initial_state : std::string{};
        if (gs.active_boss_id == actor.spawn_decl_id)
        {
            gs.active_boss_idx = -1;
            gs.active_boss_id.clear();
        }
        break;
    case BossState::Engaged:
    {
        actor.current_boss_state.clear();
        const int idx = poolIndexOfActor(actor);
        gs.active_boss_idx = idx;
        gs.active_boss_id = actor.spawn_decl_id;
        // Push encounter bed on the entering-Engaged edge only (so we
        // don't double-push on Disengaged -> Engaged re-aggros... wait,
        // we DO want to re-push on re-engage; the bed was popped on
        // disengage. Safe to push: pushMusicBed is the stack op).
        if (prev != BossState::Engaged && actor.archetype != nullptr &&
            !actor.archetype->encounter_audio_bed.empty())
        {
            selva::audio::pushMusicBed(actor.archetype->encounter_audio_bed);
        }
        // Scripted-death timers. Set FIRST time this boss enters
        // Engaged (Dormant -> Engaged) and preserved across Disengaged
        // re-engage. resetCycleEnemies clears them.
        if (prev == BossState::Dormant && actor.archetype != nullptr &&
            actor.archetype->scripted_death_seconds > 0.0f &&
            actor.scripted_death_at_wallclock < 0.0f)
        {
            const float now = selva::wallClock();
            actor.scripted_death_at_wallclock = now + actor.archetype->scripted_death_seconds;
            float pain_duration = 0.0f;
            if (!actor.archetype->scripted_death_pain_clip.empty())
            {
                const auto& reg = selva::anim::clipsByKey(actor.skeleton_id);
                const auto* clip = reg.get(actor.archetype->scripted_death_pain_clip);
                if (clip != nullptr && clip->isLoaded())
                    pain_duration = clip->duration();
            }
            actor.scripted_death_drain_end_wallclock =
                actor.scripted_death_at_wallclock + pain_duration;
            std::fprintf(stderr,
                         "[scripted-death] '%s' armed: dying at %.2f, drain ends at %.2f\n",
                         actor.spawn_decl_id.c_str(), actor.scripted_death_at_wallclock,
                         actor.scripted_death_drain_end_wallclock);
            std::fflush(stderr);
        }
        break;
    }
    case BossState::Disengaged:
        actor.current_boss_state.clear();
        if (gs.active_boss_id == actor.spawn_decl_id)
        {
            gs.active_boss_idx = -1;
            gs.active_boss_id.clear();
        }
        if (prev == BossState::Engaged && actor.archetype != nullptr &&
            !actor.archetype->encounter_audio_bed.empty())
        {
            selva::audio::popMusicBed();
        }
        break;
    case BossState::Dying:
        actor.current_boss_state.clear();
        if (gs.active_boss_id == actor.spawn_decl_id)
        {
            gs.active_boss_idx = -1;
            gs.active_boss_id.clear();
        }
        if (prev == BossState::Engaged && actor.archetype != nullptr &&
            !actor.archetype->encounter_audio_bed.empty())
        {
            selva::audio::popMusicBed();
        }
        break;
    case BossState::Felled:
        actor.current_boss_state.clear();
        if (gs.active_boss_id == actor.spawn_decl_id)
        {
            gs.active_boss_idx = -1;
            gs.active_boss_id.clear();
        }
        if ((prev == BossState::Engaged) && actor.archetype != nullptr &&
            !actor.archetype->encounter_audio_bed.empty())
        {
            selva::audio::popMusicBed();
        }
        break;
    }

    std::fprintf(stderr, "[boss-state] '%s': %s -> %s\n", id_for_log.c_str(),
                 bossStateName(prev), bossStateName(next));
    std::fflush(stderr);
}

} // namespace

void setBossState(Actor& actor, BossState next)
{
    const BossState prev = actor.boss_state;
    if (prev == next)
        return;
    actor.boss_state = next;
    writeMirrors(actor, prev, next);
}

void tearDownActiveBosses()
{
    for (auto& a : actors())
    {
        if (a.boss_state == BossState::Engaged)
            setBossState(a, BossState::Disengaged);
    }
}

} // namespace selva::gameplay
