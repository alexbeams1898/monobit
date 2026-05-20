#include "gameplay/Perception.h"

#include "Tunables.h"
#include "WallClock.h"
#include "combat/CombatLog.h"
#include "gameplay/Actor.h"

#include <algorithm>
#include <cmath>

namespace selva::gameplay
{

namespace
{

const char* awarenessName(Awareness a)
{
    switch (a)
    {
    case Awareness::Unaware:
        return "Unaware";
    case Awareness::Suspicious:
        return "Suspicious";
    case Awareness::Alerted:
        return "Alerted";
    case Awareness::Combat:
        return "Combat";
    }
    return "?";
}

// Set awareness with side-effects: stamp the entry time, reset the
// sighting counter on Unaware<->Suspicious edges, and emit a log line
// so transitions are visible without instrumenting downstream code.
void setAwareness(PerceptionState& p, Awareness next, float now)
{
    if (p.awareness == next)
        return;
    const Awareness prev = p.awareness;
    p.awareness = next;
    p.awareness_entered_time = now;
    // Sighting counter only meaningful inside Suspicious. Reset on
    // any edge that leaves or enters Suspicious from another state.
    if (next == Awareness::Suspicious || prev == Awareness::Suspicious)
        p.suspicious_sighting_count = 0;
    // outside_leash_since only meaningful inside Combat. Reset on
    // any edge that leaves Combat so the next Combat entry starts
    // fresh (no stale timestamp from a prior engagement).
    if (prev == Awareness::Combat)
        p.outside_leash_since = -1.0f;
    selva::combat::combatLog("[perception] %s -> %s at t=%.3f\n", awarenessName(prev),
                             awarenessName(next), now);
}

// True if `target_pos` is inside `actor`'s forward-facing vision cone.
// Cone is centered on actor.yaw (convention: yaw=0 faces -Z), half-
// angle = fov_degrees / 2, range = range_meters. XZ only — pitch
// doesn't factor in for ground combat.
bool targetInVisionCone(const glm::vec3& actor_pos, float actor_yaw, const glm::vec3& target_pos,
                        float fov_degrees, float range_meters)
{
    const glm::vec3 to_target = target_pos - actor_pos;
    const float dx = to_target.x;
    const float dz = to_target.z;
    const float dist_sq = dx * dx + dz * dz;
    if (dist_sq > range_meters * range_meters)
        return false;
    if (dist_sq < 1e-6f)
        return true; // target is at our feet — always seen
    // Forward unit vector for yaw=0 is (0, 0, -1); rotate by yaw.
    const float fx = -std::sin(actor_yaw);
    const float fz = -std::cos(actor_yaw);
    const float dist = std::sqrt(dist_sq);
    const float dot = (dx * fx + dz * fz) / dist;
    const float half_cos = std::cos(0.5f * fov_degrees * 0.017453293f);
    return dot >= half_cos;
}

// Advance the awareness state machine. Inputs: current perception
// state, whether the player was seen this tick, squared distance to
// player. Extracted from tickPerception so the per-state switch
// fits within the lizard CCN budget. Pure state transitions — does
// not modify last_seen_time or last_known_player_pos.
// Unaware → Suspicious on first sighting.
static void advanceAwarenessUnaware(PerceptionState& p, bool saw_now, float now)
{
    if (saw_now)
    {
        setAwareness(p, Awareness::Suspicious, now);
        p.suspicious_sighting_count = 1;
    }
}

// Suspicious → Alerted on N confirmed sightings, → Unaware on decay.
static void advanceAwarenessSuspicious(PerceptionState& p, bool saw_now, float since_contact,
                                       float now, const selva::tuning::Tunables& tun)
{
    if (saw_now)
    {
        ++p.suspicious_sighting_count;
        if (p.suspicious_sighting_count >= tun.ai_confirmed_sightings_to_alert)
            setAwareness(p, Awareness::Alerted, now);
    }
    else if (since_contact >= tun.ai_suspicion_decay_seconds || p.last_seen_time < 0.0f)
    {
        setAwareness(p, Awareness::Unaware, now);
    }
}

// Alerted → Combat on in-range visible sighting, → Suspicious on decay.
static void advanceAwarenessAlerted(PerceptionState& p, bool saw_now, float dist_to_player_sq,
                                    float since_contact, float now,
                                    const selva::tuning::Tunables& tun)
{
    const float engage_sq = tun.ai_combat_engage_range_meters * tun.ai_combat_engage_range_meters;
    if (dist_to_player_sq <= engage_sq && saw_now)
        setAwareness(p, Awareness::Combat, now);
    else if (since_contact >= tun.ai_alerted_decay_seconds)
        setAwareness(p, Awareness::Suspicious, now);
}

// Combat retention: distance-based leash with continuous-outside
// hysteresis. outside_leash_since timestamps the first crossing;
// disengage timer counts continuous-outside duration only.
static void advanceAwarenessCombat(PerceptionState& p, float dist_to_player_sq, float now,
                                   const selva::tuning::Tunables& tun)
{
    const float leash_sq = tun.ai_combat_leash_range_meters * tun.ai_combat_leash_range_meters;
    const bool outside_leash = dist_to_player_sq > leash_sq;
    if (!outside_leash)
    {
        p.outside_leash_since = -1.0f;
        return;
    }
    if (p.outside_leash_since < 0.0f)
        p.outside_leash_since = now;
    if ((now - p.outside_leash_since) >= tun.ai_combat_disengage_seconds)
        setAwareness(p, Awareness::Alerted, now);
}

void advanceAwareness(PerceptionState& p, bool saw_now, float dist_to_player_sq, float now,
                      const selva::tuning::Tunables& tun)
{
    // since_contact uses max(last_seen, awareness_entered) so each
    // state's decay clock starts from when it was entered, not from
    // an ancient absolute timestamp. Prevents chain-collapse after
    // knockdown.
    const float since_contact = now - std::max(p.last_seen_time, p.awareness_entered_time);
    switch (p.awareness)
    {
    case Awareness::Unaware:
        advanceAwarenessUnaware(p, saw_now, now);
        break;
    case Awareness::Suspicious:
        advanceAwarenessSuspicious(p, saw_now, since_contact, now, tun);
        break;
    case Awareness::Alerted:
        advanceAwarenessAlerted(p, saw_now, dist_to_player_sq, since_contact, now, tun);
        break;
    case Awareness::Combat:
        advanceAwarenessCombat(p, dist_to_player_sq, now, tun);
        break;
    }
}

} // namespace

void tickPerception(Actor& actor, const Actor& player, float dt, const selva::tuning::Tunables& tun)
{
    PerceptionState& p = actor.perception;
    const float now = selva::wallClock();

    // Step 1: did we see the player this tick?
    const bool saw_now = targetInVisionCone(actor.pos, actor.yaw, player.pos,
                                            tun.ai_vision_fov_degrees, tun.ai_vision_range_meters);
    if (saw_now)
    {
        p.last_seen_time = now;
        p.last_known_player_pos = player.pos;
    }
    // Combat retention: once engaged, the actor "knows" where the
    // player is and tracks position continuously regardless of LOS.
    // Souls convention — getting around behind a Combat-aware enemy
    // doesn't make them forget you exist, it just means they can't
    // *swing* at you (the LeafPickAction freshness gate handles
    // that) until they turn around and re-see. Without this update,
    // last_known_player_pos stays stale and LeafMoveToTarget walks
    // to where the player WAS, not where they are.
    if (p.awareness == Awareness::Combat)
        p.last_known_player_pos = player.pos;

    // While knocked down or dead, freeze the DECAY CLOCK (awareness
    // state stays put) by advancing awareness_entered_time with dt.
    // Without this, an actor face-down in the dirt would have their
    // Combat decay timer expire mid-fall and chain-collapse the
    // awareness ladder during recovery.
    //
    // Deliberately DON'T shift last_seen_time — that's a fact about
    // perception, and vision genuinely failed during knockdown. The
    // freshness gate in LeafPickAction reads last_seen_time directly
    // and uses staleness to force "investigate" behavior after
    // recovery. Shifting both clocks together would conflate them
    // and reintroduce the "swing in pre-knockdown direction" bug.
    if (actor.is_knocked_down || actor.is_dead)
    {
        p.awareness_entered_time += dt;
        return;
    }

    const float dx = player.pos.x - actor.pos.x;
    const float dz = player.pos.z - actor.pos.z;
    const float dist_to_player_sq = dx * dx + dz * dz;
    advanceAwareness(p, saw_now, dist_to_player_sq, now, tun);
}

} // namespace selva::gameplay
