#include "gameplay/Footsteps.h"

#include "WallClock.h"
#include "audio/Audio.h"
#include "debug/Flags.h"
#include "gameplay/Actor.h"
#include "log/Log.h"
#include "physics/PhysicsWorld.h"
#include "world/Collision.h"
#include "world/Terrain.h"

#include <glm/vec2.hpp>

#include <cstdio>
#include <cstdlib>

#include <fmt/core.h>

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
// Maximum model-space foot Y at which a zero-crossing counts as a
// plant. The humanoid's foot bone rests ~0.085m above hip-relative ground
// across walk/jog/sprint authored clips, with ~1cm of micro-variation
// per plant. Any vy zero-crossing significantly above that height is
// a mid-trajectory local minimum, not a floor-touching plant.
//
// The new sprinting.ozz clip (Faster Running.fbx) has TWO local
// minima per cycle on the right foot: a stride-mid dip at ~0.155m
// (knee-tuck inflection) and the real floor-touch at ~0.087m. The
// detector's original "any vy zero-crossing = plant" logic fired on
// the 0.155m dip, then cooldown-suppressed the real 0.087m plant
// ~67ms later -- producing the audible 400ms/233ms limp cadence on
// sprint (real same-foot cadence 633ms, but alternating-foot fires
// asymmetric). 0.12m sits cleanly between the real plants (<0.094m
// observed) and the stride-dip (>0.150m observed); also rejects the
// mid-swing apex at 0.72m. This is the structural definition of
// "plant" -- foot near floor at moment of zero-vy -- not per-clip
// tuning.
constexpr float kMaxPlantFootY = 0.12f; // meters; above = stride-mid dip
// Velocity at which audio reaches full volume. Plants above this
// play at max gain; plants below scale linearly. Tuned so
// steady-state walking sits ~0.7 of full gain (audible but not
// shouting) and running sits at full.
constexpr float kFullVolumeDescent = 2.5f; // m/s; saturation point
// Per-foot debounce. Covers the secondary descent wobble that happens
// ~0.22-0.25s after the primary plant in some gait clips. Headroom
// under steady-state same-foot cadences: walk ~0.83s, jog ~0.67s,
// sprint ~1.27s (sprint is slower same-foot because the flight phase
// extends the cycle). Mid-air swing-arc inflections are filtered by
// kMaxPlantFootY upstream, not by this cooldown.
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

// Footstep "log" is really a CSV data file (trajectory rows +
// [event] markers), NOT a routed engine::log::Channel — the channel
// prefix "[name:LV]" would corrupt the CSV. We compose each row with
// fmt::format then fwrite raw, which type-checks the format string
// at compile time and keeps va_list out of the codebase.
FILE* sLog = nullptr;
bool sLogOpenAttempted = false;

FILE* footstepLog()
{
    if (!selva::debug::flags().footstep_log)
        return nullptr;
    if (sLog != nullptr)
        return sLog;
    if (sLogOpenAttempted)
        return nullptr;
    sLogOpenAttempted = true;
    sLog = std::fopen("footstep-debug.log", "w");
    if (sLog != nullptr)
    {
        const std::string header = fmt::format(
            "# footstep detector trace (v4 - model-space foot Y)\n"
            "# thresholds: min_descent={:.3f}m/s full_vol_descent={:.3f}m/s "
            "refire={:.3f}s min_gain={:.3f}\n"
            "# trajectory rows: t,foot,foot_y_model,vy,peak_descent_vy,since_last_fire\n"
            "# event rows: [event] ...\n",
            kMinPlantDescent, kFullVolumeDescent, kRefireCooldown, kMinGain);
        std::fwrite(header.data(), 1, header.size(), sLog);
        std::fflush(sLog);
    }
    return sLog;
}

template <typename... Args> void footstepLogf(fmt::format_string<Args...> fmt, Args&&... args)
{
    FILE* f = footstepLog();
    if (f == nullptr)
        return;
    const std::string line = fmt::format(fmt, std::forward<Args>(args)...);
    std::fwrite(line.data(), 1, line.size(), f);
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
    footstepLogf("[event] joint resolve name={} idx={}\n", joint_name, fc.joint_idx);
}

// Downward raycast just above the foot; reports tag + body so callers
// can pick the right footstep bank.
struct FootSurface
{
    engine::physics::SurfaceTag tag = engine::physics::SurfaceTag::Unknown;
    engine::physics::BodyHandle body = engine::physics::kInvalidBody;
};
FootSurface raycastFootSurface(const glm::vec3& foot_world)
{
    FootSurface s;
    const engine::physics::RayHit hit =
        engine::physics::raycast(foot_world + glm::vec3(0.0f, 0.10f, 0.0f), // start just above foot
                                 glm::vec3(0.0f, -1.0f, 0.0f), 1.0f);
    if (hit.hit)
    {
        s.tag = hit.tag;
        s.body = hit.body;
    }
    return s;
}

// Footstep bank for the raycast result. Terrain bodies carry their
// region name as debug_name; the region declares its footstep_sound_id.
// Returns nullptr for surfaces without an authored bank (intentional —
// every walkable surface should own its own bank; nullptr is a bug
// flag, not a "default sound" fallback).
const char* pickFootstepSfx(const FootSurface& s)
{
    switch (s.tag)
    {
    case engine::physics::SurfaceTag::Architecture:
        return "footstep_concrete";
    case engine::physics::SurfaceTag::Terrain:
    {
        if (s.body == engine::physics::kInvalidBody)
            return nullptr;
        const char* region_name = engine::physics::bodyDebugName(s.body);
        if (region_name == nullptr || *region_name == '\0')
            return nullptr;
        const auto* region = selva::world::terrainRegionAtName(region_name);
        return region ? region->footstep_sound_id.c_str() : nullptr;
    }
    case engine::physics::SurfaceTag::Foliage:
    case engine::physics::SurfaceTag::Unknown:
    case engine::physics::SurfaceTag::Actor:
    default:
        return nullptr;
    }
}

// Per-plant context — bundled so firePlantSfx + handlePlantEvent stay
// under the param-count threshold and read naturally at the call site.
struct PlantContext
{
    glm::vec3 foot_world;
    FootSurface surface;
    float gain;
    float foot_y;
    float since_fire;
    float now;
    bool is_left;
};

void firePlantSfx(Actor::FootContact& fc, Actor& actor, const PlantContext& ctx,
                  const char* sfx_name)
{
    if (sfx_name == nullptr)
    {
        footstepLogf("[event] surface foot={} foot_xz=({:.3f},{:.3f}) surface_tag={} "
                     "body_name='{}' — NO SFX (unauthored surface)\n",
                     footLabel(ctx.is_left), ctx.foot_world.x, ctx.foot_world.z,
                     static_cast<int>(ctx.surface.tag),
                     ctx.surface.body != engine::physics::kInvalidBody
                         ? engine::physics::bodyDebugName(ctx.surface.body)
                         : "(no hit)");
        fc.last_fire_time = ctx.now;
        actor.last_footstep_fire_time = ctx.now;
        return;
    }
    footstepLogf("[event] surface foot={} foot_xz=({:.3f},{:.3f}) body_xz=({:.3f},{:.3f}) "
                 "surface_tag={} sfx={}\n",
                 footLabel(ctx.is_left), ctx.foot_world.x, ctx.foot_world.z, actor.pos.x,
                 actor.pos.z, static_cast<int>(ctx.surface.tag), sfx_name);
    selva::audio::playSfxScaled(sfx_name, ctx.gain);
    fc.last_fire_time = ctx.now;
    actor.last_footstep_fire_time = ctx.now;
    footstepLogf("[event] FIRE foot={} gain={:.3f} peak_descent={:.3f} foot_y={:.4f} "
                 "since_last={:.3f}s sfx={}\n",
                 footLabel(ctx.is_left), ctx.gain, fc.peak_descent_vy, ctx.foot_y, ctx.since_fire,
                 sfx_name);
}

// Handle a foot zero-crossing (transition from descent to ascent =
// foot plant). Picks surface + bank, plays SFX if gain + cooldown
// allow, otherwise logs a suppression reason. Resets peak_descent.
void handlePlantEvent(Actor::FootContact& fc, Actor& actor, float foot_y, float now,
                      float since_fire, bool can_fire, bool is_left)
{
    const float gain = plantGain(fc.peak_descent_vy);
    const bool foot_near_ground = (foot_y <= kMaxPlantFootY);
    if (gain > 0.0f && can_fire && foot_near_ground)
    {
        PlantContext ctx;
        ctx.foot_world = actor.sampler.jointWorldPosWithActor(fc.joint_idx);
        ctx.surface = raycastFootSurface(ctx.foot_world);
        ctx.gain = gain;
        ctx.foot_y = foot_y;
        ctx.since_fire = since_fire;
        ctx.now = now;
        ctx.is_left = is_left;
        firePlantSfx(fc, actor, ctx, pickFootstepSfx(ctx.surface));
    }
    else
    {
        const char* reason = !foot_near_ground ? "mid_swing_inflection"
                             : !can_fire       ? "cooldown"
                                               : "below_min_descent";
        footstepLogf("[event] SUPPRESS foot={} peak_descent={:.3f} foot_y={:.4f} can_fire={} "
                     "since_last={:.3f}s gain={:.3f} reason={}\n",
                     footLabel(is_left), fc.peak_descent_vy, foot_y, can_fire ? 1 : 0, since_fire,
                     gain, reason);
    }
    fc.peak_descent_vy = 0.0f;
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

    footstepLogf("{:.4f},{},{:.4f},{:.3f},{:.3f},{:.3f}\n", now, footLabel(is_left), foot_y, vy,
                 fc.peak_descent_vy, since_fire);

    const bool zero_crossing = (fc.prev_vy < 0.0f) && (vy >= 0.0f);
    if (zero_crossing)
        handlePlantEvent(fc, actor, foot_y, now, since_fire, can_fire, is_left);

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
