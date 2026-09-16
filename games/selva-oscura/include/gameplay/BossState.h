#pragma once

#include <cstdint>

namespace selva::gameplay
{

struct Actor;

// Lifecycle of a boss-class actor. SINGLE source of truth for what
// stage the boss is in -- HUD, AI, audio bed, and save-persistence all
// read from this enum instead of inferring from a tangle of separate
// flags (active_boss_idx + current_boss_state + is_dead + felled_bosses).
//
// Transitions are funneled through setBossState() so logs + mirror
// writes happen in exactly one place. The legacy fields
// (Actor.current_boss_state, GameState.active_boss_idx/id) are KEPT as
// mirrors during the migration; setBossState updates them so existing
// callsites that read those fields keep working without per-call
// changes. Future cleanup migrates each callsite to read boss_state
// directly and drops the mirrors.
//
// Non-boss actors carry the default Dormant value and never transition;
// nothing reads boss_state for them.
enum class BossState : std::uint8_t
{
    Dormant = 0,    // pre-engage (Pattern B idle / Pattern A pre-spawn)
    Engaged = 1,    // active fight; HUD visible, audio bed pushed
    Disengaged = 2, // alive but player out of leash; HUD hidden
    Dying = 3,      // scripted death in progress; AI off, pain clip playing
    Felled = 4,     // HP <= 0; profile-write happens, transient
};

const char* bossStateName(BossState s);

// THE setter. Writes actor.boss_state, mirrors to legacy fields, logs
// the transition. Callers must funnel ALL state changes through this
// function -- no direct writes to boss_state or the legacy fields.
// No-op if next == current.
void setBossState(Actor& actor, BossState next);

// Force every Engaged boss in the actor pool to Disengaged. Use on
// "leaving the fight" boundaries: quit-to-main-menu, save-and-quit,
// any UI transition out of Playing, the window-close path. Without
// this, an actor lingers in Engaged across the boundary -- audio bed
// stays pushed, save persists active_boss_idx, the next reload shows
// a phantom HP bar / Felled flash because the HUD sees the carried-
// over Engaged actor before re-engage happens fresh.
//
// Dormant + Disengaged + Felled actors are untouched (already not
// "engaged" in the fight-active sense).
//
// The symmetric rule: if you ENTER the fight, you must EXIT it through
// the same pipeline.
void tearDownActiveBosses();

} // namespace selva::gameplay
