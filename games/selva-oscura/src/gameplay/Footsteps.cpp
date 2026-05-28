#include "gameplay/Footsteps.h"

#include "Tunables.h"
#include "WallClock.h"
#include "audio/Audio.h"
#include "gameplay/Actor.h"
#include "physics/PhysicsWorld.h"
#include "world/Collision.h"
#include "world/Terrain.h"

#include <glm/vec2.hpp>

#include <cstdarg>
#include <cstdio>
#include <cstdlib>

namespace selva::gameplay
{

namespace
{

constexpr const char* kFootLeftJointName = "mixamorig:LeftFoot";
constexpr const char* kFootRightJointName = "mixamorig:RightFoot";

// Foot-plant detection v2 — velocity-based.
//
// We fire on the moment the foot's vertical velocity transitions from
// descending to ascending (vy crosses zero going positive) — that's
// the visible foot-plant. Two gates filter it:
//
//   * peak_descent: the maximum |descent velocity| accumulated during
//     the descent leading into this zero-crossing must exceed
//     kMinPlantDescent. Filters idle micro-sway (tiny ~0.4 m/s
//     wobbles) without filtering walking (~1.0+ m/s descents).
//   * cooldown: per-foot refire window prevents jitter at the trough
//     producing double-fires.
//
// Bank pick uses peak_descent: hard plants (running, jumps) >
// Plant gain ramps with descent velocity. SFX name is picked per-fire
// by surface (grass outside, hallway inside the crypt) — see playSfx
// call below for the swap. Future: per-zone surface authoring (e.g.
// concrete on stone paths).
//
// This approach is rig-offset-agnostic (we don't compare to ground Y)
// and clip-agnostic (any animation with a foot-plant motion fires
// the SFX, including attack windups and get-ups).
// Foot-plant detection thresholds, tuned from real-walk traces:
//   * Steady-state walk plants:  peak descent ~1.0-1.3 m/s
//   * Soft entry plants (first step from idle): ~0.4-0.6 m/s
//   * Wobble plants (foot settling): ~0.1-0.5 m/s
//   * Running plants: ~2.5-3.0 m/s
//
// We fire on EVERY zero-crossing above kMinPlantDescent, but scale
// the audio volume by peak_descent. Soft plants come out quiet
// (audible but small); hard plants come out loud; wobbles get
// filtered by the threshold. This is how real footsteps work -
// volume tracks impact energy.
//
// kMinPlantDescent is set just above wobble floor so wobbles don't
// produce barely-audible ghost fires. The first step of a fresh
// stride crosses this fine.
constexpr float kMinPlantDescent = 0.35f; // m/s; below = noise floor
// Velocity at which audio reaches full volume. Plants above this
// play at max gain; plants below scale linearly. Tuned so
// steady-state walking sits ~0.7 of full gain (audible but not
// shouting) and running sits at full.
constexpr float kFullVolumeDescent = 2.5f; // m/s; saturation point
// Per-foot debounce. Has to cover the secondary descent wobble that
// happens ~0.22-0.25s after the primary plant in the X_Bot walk
// clip's R-foot (and occasionally L). 0.30s catches the wobbles
// while still leaving headroom under the steady-state same-foot
// cadence of ~0.83s walk / ~0.67s run.
constexpr float kRefireCooldown = 0.30f;

// Velocity-to-gain mapping. Linear ramp from kMinPlantDescent (gain
// floor) to kFullVolumeDescent (gain 1.0). Below min returns 0; at
// or above full returns 1.
//
// The gain floor at min descent isn't zero - a plant barely above
// the wobble threshold should still be audible (just very quiet).
// 0.12 keeps soft plants from being inaudible while still reading
// as "softer" relative to a full plant.
constexpr float kMinGain = 0.12f;
float plantGain(float peak_descent)
{
    if (peak_descent <= kMinPlantDescent)
        return 0.0f;
    if (peak_descent >= kFullVolumeDescent)
        return 1.0f;
    const float t = (peak_descent - kMinPlantDescent) / (kFullVolumeDescent - kMinPlantDescent);
    return kMinGain + (1.0f - kMinGain) * t;
}

FILE* sLog = nullptr;
bool sLogOpenAttempted = false;

FILE* footstepLog()
{
    if (!selva::tuning::current().debug_footstep_log)
        return nullptr;
    if (sLog != nullptr)
        return sLog;
    if (sLogOpenAttempted)
        return nullptr;
    sLogOpenAttempted = true;
    sLog = std::fopen("footstep-debug.log", "w");
    if (sLog != nullptr)
    {
        std::fprintf(sLog,
                     "# footstep detector trace (v4 - model-space foot Y)\n"
                     "# thresholds: min_descent=%.3fm/s full_vol_descent=%.3fm/s "
                     "refire=%.3fs min_gain=%.3f\n"
                     "# trajectory rows: t,foot,foot_y_model,vy,peak_descent_vy,since_last_fire\n"
                     "# event rows: [event] ...\n",
                     kMinPlantDescent, kFullVolumeDescent, kRefireCooldown, kMinGain);
        std::fflush(sLog);
    }
    return sLog;
}

void footstepLogf(const char* fmt, ...)
{
    FILE* f = footstepLog();
    if (f == nullptr)
        return;
    va_list args;
    va_start(args, fmt);
    std::vfprintf(f, fmt, args); // NOLINT(clang-analyzer-valist.Uninitialized)
    va_end(args);
    std::fflush(f);
}

const char* footLabel(bool is_left)
{
    return is_left ? "L" : "R";
}

void resolveJointIdx(Actor::FootContact& fc, const Actor& actor, const char* joint_name)
{
    if (fc.joint_idx != -2)
        return;
    fc.joint_idx = actor.sampler.findJoint(joint_name);
    footstepLogf("[event] joint resolve name=%s idx=%d\n", joint_name, fc.joint_idx);
}

void tickOneFoot(Actor::FootContact& fc, Actor& actor, const char* joint_name, float dt,
                 bool is_left)
{
    resolveJointIdx(fc, actor, joint_name);
    if (fc.joint_idx < 0)
        return;

    // Read MODEL-space foot Y (not world). On hills the actor's world
    // Y rises with terrain - if we read world-space foot Y, the
    // terrain-rise gets baked into our velocity calculation:
    // descending model-foot + ascending actor + terrain partially
    // cancel uphill (step suppressed), reinforce downhill (step fires
    // too hard). Model-space is what the CLIP authored - the foot's
    // motion relative to the hip - and is the correct signal for
    // foot-plant detection regardless of terrain.
    const glm::vec3 foot_model = actor.sampler.jointWorldPos(fc.joint_idx);
    const float foot_y = foot_model.y;

    if (!fc.initialized)
    {
        fc.prev_y = foot_y;
        fc.prev_vy = 0.0f;
        fc.peak_descent_vy = 0.0f;
        fc.initialized = true;
        return;
    }

    if (dt <= 0.0001f)
        return;

    const float vy = (foot_y - fc.prev_y) / dt;
    const float now = selva::wallClock();
    const float since_fire = now - fc.last_fire_time;
    const bool can_fire = since_fire >= kRefireCooldown;

    if (vy < 0.0f)
    {
        const float descent_speed = -vy;
        if (descent_speed > fc.peak_descent_vy)
            fc.peak_descent_vy = descent_speed;
    }

    footstepLogf("%.4f,%s,%.4f,%.3f,%.3f,%.3f\n", now, footLabel(is_left), foot_y, vy,
                 fc.peak_descent_vy, since_fire);

    const bool zero_crossing = (fc.prev_vy < 0.0f) && (vy >= 0.0f);

    if (zero_crossing)
    {
        const float gain = plantGain(fc.peak_descent_vy);
        if (gain > 0.0f && can_fire)
        {
            // Surface picker: per real-physics doctrine, ask physics
            // what the foot is standing on. Downward raycast from the
            // foot's world XYZ; the hit body's SurfaceTag drives the
            // bank selection. Terrain → grass; Architecture → concrete.
            //
            // No more XZ-rect "is the player inside the chapel?" check
            // — that broke as soon as architecture extended underground
            // (descent corridor) where the chapel-interior rect doesn't
            // reach. Reading the actual contacted body generalizes:
            // ANY future surface (sand, water, stone path, dungeon) just
            // adds a SurfaceTag value, no rect to author.
            const glm::vec3 foot_world = actor.sampler.jointWorldPosWithActor(fc.joint_idx);
            engine::physics::SurfaceTag surface = engine::physics::SurfaceTag::Unknown;
            engine::physics::BodyHandle hit_body = engine::physics::kInvalidBody;
            {
                const engine::physics::RayHit hit = engine::physics::raycast(
                    foot_world + glm::vec3(0.0f, 0.10f, 0.0f), // start just above foot
                    glm::vec3(0.0f, -1.0f, 0.0f), 1.0f);
                if (hit.hit)
                {
                    surface = hit.tag;
                    hit_body = hit.body;
                }
            }
            // Footstep bank selection. The foot raycast tells us EXACTLY
            // which body the foot hit; we look up that body's surface
            // tag and (for terrain) its region. No XZ-region lookup —
            // each surface knows what it is.
            //   - Terrain bodies are tagged with their region name as
            //     debug_name (see JsonScene.cpp); the region declares
            //     its footstep_sound_id in terrain config.json.
            //   - Architecture (chapel mesh, descent stairs) → concrete.
            //   - No hit / unknown body → no footstep, intentional. We
            //     never fall back to a "default" sound: every walkable
            //     surface is authored to own its own bank.
            const char* sfx_name = nullptr;
            switch (surface)
            {
            case engine::physics::SurfaceTag::Architecture:
                sfx_name = "footstep_concrete";
                break;
            case engine::physics::SurfaceTag::Terrain:
                if (hit_body != engine::physics::kInvalidBody)
                {
                    const char* region_name = engine::physics::bodyDebugName(hit_body);
                    if (region_name != nullptr && *region_name != '\0')
                    {
                        if (const auto* region = selva::world::terrainRegionAtName(region_name))
                            sfx_name = region->footstep_sound_id.c_str();
                    }
                }
                break;
            case engine::physics::SurfaceTag::Foliage:
            case engine::physics::SurfaceTag::Unknown:
            case engine::physics::SurfaceTag::Actor:
            default:
                break;
            }
            if (sfx_name == nullptr)
            {
                // No surface known. Don't play anything — every
                // walkable surface should declare its bank. If this
                // fires, it's an unauthored surface (bug), not a case
                // where we fall back to a default sound.
                footstepLogf("[event] surface foot=%s foot_xz=(%.3f,%.3f) surface_tag=%d "
                             "body_name='%s' — NO SFX (unauthored surface)\n",
                             footLabel(is_left), foot_world.x, foot_world.z,
                             static_cast<int>(surface),
                             hit_body != engine::physics::kInvalidBody
                                 ? engine::physics::bodyDebugName(hit_body)
                                 : "(no hit)");
                fc.last_fire_time = now;
                actor.last_footstep_fire_time = now;
            }
            else
            {
                footstepLogf("[event] surface foot=%s foot_xz=(%.3f,%.3f) body_xz=(%.3f,%.3f) "
                             "surface_tag=%d sfx=%s\n",
                             footLabel(is_left), foot_world.x, foot_world.z, actor.pos.x,
                             actor.pos.z, static_cast<int>(surface), sfx_name);
                selva::audio::playSfxScaled(sfx_name, gain);
                fc.last_fire_time = now;
                actor.last_footstep_fire_time = now;
                footstepLogf("[event] FIRE foot=%s gain=%.3f peak_descent=%.3f foot_y=%.4f "
                             "since_last=%.3fs sfx=%s\n",
                             footLabel(is_left), gain, fc.peak_descent_vy, foot_y, since_fire,
                             sfx_name);
            }
        }
        else
        {
            footstepLogf("[event] SUPPRESS foot=%s peak_descent=%.3f foot_y=%.4f can_fire=%d "
                         "since_last=%.3fs gain=%.3f reason=%s\n",
                         footLabel(is_left), fc.peak_descent_vy, foot_y, can_fire ? 1 : 0,
                         since_fire, gain, !can_fire ? "cooldown" : "below_min_descent");
        }
        fc.peak_descent_vy = 0.0f;
    }

    fc.prev_y = foot_y;
    fc.prev_vy = vy;
}

} // namespace

void tickFootsteps(Actor& actor, float dt)
{
    if (actor.is_dead)
        return;
    tickOneFoot(actor.foot_left, actor, kFootLeftJointName, dt, /*is_left=*/true);
    tickOneFoot(actor.foot_right, actor, kFootRightJointName, dt, /*is_left=*/false);
}

} // namespace selva::gameplay
