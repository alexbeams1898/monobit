#include "gameplay/Perception.h"

#include "Tunables.h"
#include "WallClock.h"
#include "combat/CombatLog.h"
#include "gameplay/Actor.h"

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

} // namespace

void tickPerception(Actor& actor, const Actor& player, float /*dt*/,
                    const selva::tuning::Tunables& tun)
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

    // Step 2: distance check (needed for Alerted -> Combat).
    const glm::vec3 to_player_xz(player.pos.x - actor.pos.x, 0.0f, player.pos.z - actor.pos.z);
    const float dist_to_player_sq =
        to_player_xz.x * to_player_xz.x + to_player_xz.z * to_player_xz.z;
    const float engage_sq = tun.ai_combat_engage_range_meters * tun.ai_combat_engage_range_meters;

    // Step 3: state transitions. Order matters: handle escalations
    // first (so a single tick can climb multiple levels — e.g. snap
    // into Combat from Unaware on a sighting in close range), then
    // decays.
    switch (p.awareness)
    {
    case Awareness::Unaware:
        if (saw_now)
        {
            setAwareness(p, Awareness::Suspicious, now);
            p.suspicious_sighting_count = 1;
        }
        break;

    case Awareness::Suspicious:
        if (saw_now)
        {
            ++p.suspicious_sighting_count;
            if (p.suspicious_sighting_count >= tun.ai_confirmed_sightings_to_alert)
                setAwareness(p, Awareness::Alerted, now);
        }
        else if ((now - p.last_seen_time) >= tun.ai_suspicion_decay_seconds ||
                 p.last_seen_time < 0.0f)
        {
            setAwareness(p, Awareness::Unaware, now);
        }
        break;

    case Awareness::Alerted:
        if (dist_to_player_sq <= engage_sq && saw_now)
        {
            setAwareness(p, Awareness::Combat, now);
        }
        else if ((now - p.last_seen_time) >= tun.ai_alerted_decay_seconds)
        {
            setAwareness(p, Awareness::Suspicious, now);
        }
        break;

    case Awareness::Combat:
        if ((now - p.last_seen_time) >= tun.ai_combat_disengage_seconds)
            setAwareness(p, Awareness::Alerted, now);
        break;
    }
}

} // namespace selva::gameplay
