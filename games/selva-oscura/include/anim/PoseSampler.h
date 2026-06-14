#pragma once

#include <glm/glm.hpp>

#include <cstdio>
#include <functional>
#include <memory>
#include <vector>

namespace ozz::math
{
struct SoaTransform;
}

namespace selva::anim
{

struct AnimationClip;
struct Skeleton;
struct SkeletalMesh;

// PoseSampler — produces the GPU bone palette for an animated character,
// given a sequence of "play this clip" requests over time. Owns scratch
// buffers and per-clip ozz contexts so per-frame sampling is allocation-
// free in steady state.
//
// One PoseSampler per animated character. Internally maintains three
// tracks:
//   * locomotion-current — the looping clip the gameplay most-recently
//     requested (Idle/Walk/Run). Crossfades to previous on switch.
//   * locomotion-previous — the fading-out locomotion clip.
//   * one-shot — a non-looping clip (attack, dodge, hit react). When
//     active, dominates the output; locomotion keeps advancing in the
//     background so we can blend back to whatever the player is doing
//     when the one-shot ends.
//
// Lifecycle: construct via createPoseSampler(skeleton, mesh). Each frame
// call update(loco_clip, dt, blend_seconds); optionally call playOneShot()
// to kick off an attack. Read bone_palette from the renderer.
struct PoseSampler
{
    struct Impl; // ozz state lives here so the header doesn't pull ozz
    std::unique_ptr<Impl> impl;

    // The bone palette in **ozz joint order**, model-space. Updated by
    // update(); read by the renderer. Length = skeleton bone count.
    std::vector<glm::mat4> bone_palette;

    PoseSampler();
    PoseSampler(const PoseSampler&) = delete;
    PoseSampler& operator=(const PoseSampler&) = delete;
    PoseSampler(PoseSampler&&) noexcept;
    PoseSampler& operator=(PoseSampler&&) noexcept;
    ~PoseSampler();

    // Advance the sampler by `dt` seconds and update bone_palette.
    //   * `clip` is the desired locomotion clip this frame. If it differs
    //     from the currently-active locomotion clip, a cross-fade kicks
    //     off (previous→current over `blend_seconds`).
    //   * If a one-shot is active it dominates the output regardless of
    //     locomotion. Locomotion still advances so it's ready when the
    //     one-shot ends.
    //   * `blend_seconds` is the duration of any *new* locomotion blend
    //     that starts this frame. An in-progress blend keeps its
    //     original duration. Pass 0 to snap (no fade).
    //   * `loops` controls what happens when the locomotion clip's time
    //     hits its duration. true = loop back to t=0 (gait cycles,
    //     idles). false = clamp at end and signal completion via
    //     locomotionClipFinished(). Used for explicit transition clips
    //     (start_walking, run_to_stop) that play once and hand off to a
    //     destination state.
    // Returns false if the inputs are invalid (no skeleton, no clip).
    //
    // `clip_key` is the human-meaningful registry key (e.g.
    // "walking", "jogging") used only for diagnostic logging. Empty
    // string is allowed; the log will fall back to ozz Animation
    // name (which for Mixamo clips is always "mixamo.com").
    bool update(const AnimationClip& clip, float dt, float blend_seconds, bool loops = true,
                const char* clip_key = "");

    // True if the locomotion track's clip is non-looping AND has reached
    // its end. Used by the gameplay state machine to know when a
    // transition clip (e.g. start_walking) has finished and the next
    // state's loop clip should take over. Returns false for looping
    // clips (they never "finish").
    bool locomotionClipFinished() const;

    // Request an inertialization transition. Called BEFORE a clip
    // change (locomotion swap, one-shot fire, one-shot end). The
    // sampler computes a per-joint offset between a "source pose" and
    // the new clip's t=0 pose, then decays that offset to zero over
    // `duration_seconds`. Joints curve smoothly into the new motion
    // preserving visible continuity.
    //
    // Industry term: "Inertialization" or "Pose Match." This is the
    // technique combat-tier action games use to avoid skip-between-
    // poses on clip transitions.
    //
    // Without `source_pose`: source is the previous frame's rendered
    // pose. Right for locomotion transitions (idle ↔ walk ↔ run) where
    // both poses are part of an authored motion family — captures
    // velocity continuity.
    //
    // With `source_pose`: source is the supplied pose, treated as the
    // canonical baseline for the clip family of the new clip. Right
    // for attack one-shots where the destination clip was authored
    // from a specific baseline (e.g. combat-stance idle) — using the
    // baseline pose ensures inertialization bridges two animator-
    // related poses, not whatever the screen happened to show. Avoids
    // the "legs spaz" failure mode when capturing from peaceful idle
    // before a combat attack.
    //
    // Calling this multiple times in one frame is fine — each call
    // re-captures and restarts the decay. duration_seconds ~0.10-0.30
    // is the sweet spot.
    void requestInertialization(float duration_seconds);
    void requestInertializationFromPose(float duration_seconds,
                                        const std::vector<ozz::math::SoaTransform>& source_pose);

    // Configure per-joint decay scaling. Each joint's decay window =
    // base + scale * |offset_radians|, clamped to max. Joints with
    // small pose offsets keep the snappy `base` window; joints with
    // large offsets get longer windows so their motion reads as
    // smooth rather than as a fast snap. Set once at startup; the
    // values are read at every capture.
    void setInertializationScaling(float base_seconds, float scale_seconds_per_radian,
                                   float max_seconds);

    // Snap the active locomotion track to clip-time `t_seconds`. Used
    // before firing an action whose blend-in source-pose works best
    // when the looping locomotion is at a known frame (e.g. combat-
    // idle at t=0 for the block raise handoff).
    void setLocomotionClipTime(float t_seconds);

    // Sample `clip` at clip-time `t` and write the local-space pose
    // into `out`. `out` is resized to num_soa_joints. Used by gameplay
    // to capture canonical baseline poses (e.g. combat-stance at t=0)
    // that get fed to requestInertializationFromPose. Doesn't affect
    // any live track state.
    bool sampleClipPose(const AnimationClip& clip, float t_seconds,
                        std::vector<ozz::math::SoaTransform>& out) const;

    // Which body region the one-shot drives.
    //   Full       — every joint follows the one-shot (dodges, hit reacts,
    //                death animations — the whole character commits).
    //   UpperBody  — only the spine/arms/head follow the one-shot; legs
    //                and hips keep playing locomotion. Convention for
    //                attacks: you can swing while walking or sprinting,
    //                and the legs never visually "snap" to a
    //                standing-attack rest pose because they aren't
    //                being driven by the attack at all. The lower-body
    //                keeps cycling through whatever locomotion clip
    //                update() is being fed.
    enum class BodyMask
    {
        Full,
        UpperBody,
    };

    // Trigger a non-looping one-shot clip (attack, dodge, hit react).
    // While the one-shot is dominant, it overrides locomotion — when it
    // ends, the sampler blends back to whatever locomotion clip update()
    // is being passed at that moment.
    //   * `blend_in_seconds`  — fade in from current pose to one-shot.
    //   * `blend_out_seconds` — fade out from one-shot back to locomotion
    //     (started automatically when the one-shot's time hits clip
    //     duration minus blend_out_seconds).
    //   * `mask` — which body region the one-shot drives. Default Full
    //     for backwards compatibility; pass UpperBody for attacks so
    //     locomotion keeps animating the legs.
    //   * `start_time_seconds` — clip-time to begin playback at (0 =
    //     clip's t=0). Skips the clip's windup phase. Useful when
    //     transitioning into the one-shot from an already-moving pose
    //     (e.g. running into a roll): the first ~0.10s of a roll clip
    //     is the character squatting from a stationary stance; we skip
    //     past it so the blend lands directly in the tucked-and-rolling
    //     pose and the run→roll transition reads as one continuous
    //     forward fall. Clamped to [0, clip.duration()).
    //   * `playback_rate` — multiplier applied to dt while the one-shot
    //     advances. 1.0 = real-time, >1 = faster (snappier
    //     rolls), <1 = slower. Affects only the one-shot track, not
    //     locomotion. The clip's effective duration becomes
    //     duration() / playback_rate. Clamped to [0.1, 10.0].
    // Calling again while a one-shot is already playing cancels the
    // current one and starts the new one with a fresh blend-in. The
    // one-shot does NOT loop; it ends when its clip duration elapses.
    // Optional trailing parameters for playOneShot. Default-
    // constructed values match the historical "no special handling"
    // behavior; profile-driven callers (TransitionProfile) populate
    // explicit values.
    struct OneShotOptions
    {
        // Hold the clip's last frame instead of auto-fading. Used
        // for held actions like the unarmed block.
        bool freeze_last = false;
        // When freeze_last AND freeze_at_seconds > 0, clamp the
        // clip's time at this value instead of duration. Pose
        // freezes at a chosen mid-clip frame (the block-peak) rather
        // than wherever the clip happens to end. Ignored otherwise.
        float freeze_at_seconds = 0.0f;
        // Registry key for diagnostic logs (e.g. "jab"). Empty
        // string = falls back to ozz Animation::name() which is
        // always "mixamo.com" for Mixamo clips.
        const char* clip_key = "";
        // Suppress SM-driven loco clip swaps during this one-shot's
        // Hold + BlendOut phases. True for dodges/blocks (no
        // intended stance change). False for attacks (the attack
        // triggers CombatReady; combat-idle should be live by
        // BlendOut). Only honored when mask == Full.
        bool freeze_loco_during_one_shot = false;
        // Fraction of clip duration past which gameplay considers the
        // one-shot "past commitment" — movement unlocks, the next
        // one-shot can chain in via one_shot_previous crossfade. 1.0
        // = full-duration lock (default, no early cancel). 0.65 =
        // unlock at 65% of the clip's wallclock duration. Used by
        // dodges (chain rolls), jumps (chain jumps), and any future
        // commit-then-recover one-shot. Combat attacks use their own
        // per-clip wallclock cancel window (rhythm timing), not this.
        float cancel_fraction = 1.0f;
        // Cap the effective clip duration at this clip-local time.
        // Distinct from freeze_at_seconds: freeze HOLDS the pose at a
        // frame; end_seconds advances to that frame then blends out
        // into locomotion. Negative = uncapped.
        float end_seconds = -1.0f;
        // Authoritative per-fire override of the one-shot's TRAVELING
        // vs IN_PLACE classification. The auto-classifier uses a 0.5m
        // hip-path threshold which is a coarse heuristic -- some bite
        // clips have an authored "step into the bite" that crosses the
        // threshold but the intended gameplay is visual-only (no world
        // translation). Conversely a leap/pounce clip authored without
        // hip travel needs forced TRAVELING.
        //   0 = auto (legacy threshold-based behavior)
        //   1 = force IN_PLACE (no extraction, clip is visual only)
        //   2 = force TRAVELING (extract + translate world)
        int hip_translation_mode = 0;
    };

    void playOneShot(const AnimationClip& clip, float blend_in_seconds, float blend_out_seconds,
                     BodyMask mask, float start_time_seconds, float playback_rate,
                     const OneShotOptions& options);

    // Request the active one-shot to start blending out NOW. Used to
    // release a held (freeze_last) one-shot like the unarmed block.
    void releaseOneShot();

    // Wipe all runtime state to inactive: loco tracks, one-shot,
    // blend weights, inertialization, hip-delta history. Preserves
    // bindings (skeleton, joint map, IK probe). Use on character
    // switch / fresh session boundaries so the next update() produces
    // a clean pose unaffected by the prior character's last frame.
    void hardReset();

    // True while a one-shot is the dominant clip (>50% weight). Combat
    // gameplay reads this to gate movement, queue follow-ups, etc.
    bool isOneShotActive() const;

    // True when the active one-shot's elapsed time is past its
    // OneShotOptions::cancel_fraction × duration. Gameplay reads this
    // to unlock movement / fire buffered next-actions before the
    // one-shot's full BlendOut completes — the remaining tail still
    // plays visually but the player is no longer committed. Returns
    // false when no one-shot is active or cancel_fraction is 1.0
    // (default: no early cancel).
    bool isOneShotPastCancelFraction() const;

    // Read the MODEL-space translation of joint `i` (post-LocalToModelJob,
    // pre-actor-transform). Returns zero if `i` is out of range or the
    // sampler hasn't been initialized. Used by the animation-debug CSV
    // exporter and the foot-plant detector's clip-side scratch reads.
    glm::vec3 jointWorldPos(int i) const;
    int jointCount() const;
    const char* jointName(int i) const;

    // Read the WORLD-space translation of joint `i` (model-space joint
    // transformed by the actor placement supplied via setActorPlacement).
    // Convention:
    //   world.xz = actor.pos.xz + R(actor_yaw) * model.xz
    //   world.y  = actor.pos.y + model.y
    // Returns zero on invalid index or uninitialized sampler. This is
    // the accessor the foot-plant detector reads each frame.
    glm::vec3 jointWorldPosWithActor(int i) const;

    // Full world-space transform of joint `i` (rotation + translation),
    // with the actor placement applied. Used by FPV camera-roll math:
    // during dodge/roll one-shots the camera takes the head bone's
    // orientation from the clip so the eyes tumble with the body.
    // Returns identity on invalid index or uninitialized sampler.
    glm::mat4 jointWorldMatrixWithActor(int i) const;

    // Per-frame hip translation delta the clip authored — i.e., how far
    // the clip wanted to move the character on this frame, in model
    // (pre-yaw) space. Always XZ only; Y is zero.
    //
    // The PoseSampler internally freezes the hip's XZ back to the rest
    // pose so the visual bones stay anchored to the model origin. This
    // accessor returns the motion that was *consumed* by that freeze —
    // gameplay code reads it and applies it (rotated by player yaw) to
    // its world-space position, giving the character the authored
    // travel without the bones drifting from the model anchor.
    //
    // Discontinuities (clip change, one-shot fire, time wrap) emit a
    // zero delta to avoid spurious pose-snap motion.
    glm::vec3 consumedHipDelta() const;

    // Result of scanning a clip's hip XZ trajectory.
    //   * path_length: clip-local meters the hip traveled during the
    //     "active motion" window (hop / push / lunge phase only —
    //     post-landing settle drift is excluded so gameplay's script
    //     push doesn't amplify a tiny animator cleanup into a
    //     world-space slide).
    //   * motion_end_time: clip time at which the hip velocity first
    //     drops below `quiet_velocity_fraction` × peak velocity. Use
    //     this as the cutoff for any gameplay-driven world translation.
    struct ClipHipScan
    {
        float path_length = 0.0f;
        float motion_end_time = 0.0f;
    };

    // Scan `clip`'s hip XZ trajectory at fixed-rate samples and return
    // the total clip-local path length the hip traversed up to the
    // motion-end cutoff (see ClipHipScan). Used by gameplay code that
    // wants to scale a script-driven push so the per-frame world
    // translation tracks the clip's authored hip motion exactly — push
    // is zero where the clip is stationary, peaks where the clip's hip
    // is moving fastest, and integrates to the configured total distance.
    //
    // `sample_hz` is the sampling rate (60 Hz is typical, matches our
    // animation tick). `quiet_velocity_fraction` is the fraction of the
    // clip's peak hip velocity below which we declare the foot planted
    // (0.10 = 10%). Returns zeros if the clip isn't loaded or the
    // skeleton has no hip.
    ClipHipScan clipHipPathLength(const AnimationClip& clip, float sample_hz = 60.0f,
                                  float quiet_velocity_fraction = 0.10f) const;

    // Scan `clip` at fixed-rate samples and return, for each joint in
    // `joint_indices`, the clip-local time at which that joint's
    // 3D-position velocity first drops below `quiet_velocity_fraction`
    // of its peak (after the peak has occurred). The returned value is
    // the MAX across all joints — i.e. the clip-local time after which
    // EVERY watched joint has settled.
    //
    // Used by combat to derive the cancel-window open time for an
    // attack: the swing visibly settles back to combat-stance when the
    // hand(s) and any other watched joints stop moving. Pressing for
    // the next chain entry at that instant produces a seamless
    // transition (no stall, no cut-off recovery).
    //
    // Differs from clipHipPathLength in two ways:
    //   1. Operates on 3D position (XYZ), not XZ — hand swings move
    //      vertically too, so XZ-only would miss most of the motion.
    //   2. Returns a single time rather than an integrated path
    //      length — combat doesn't care about distance, only "when
    //      did the swing stop."
    //
    // Returns clip duration if the joint list is empty or the clip is
    // unloaded (no settle detected → assume cancel opens at end).
    float clipJointMotionEnd(const AnimationClip& clip, const std::vector<int>& joint_indices,
                             float sample_hz = 60.0f, float quiet_velocity_fraction = 0.10f) const;

    // Scan `clip` and return the clip-local time at which the watched
    // joints' motion FIRST becomes significant — i.e. the start of the
    // swing's contact phase. Returns the MIN across joints (earliest
    // any of them starts moving fast). Used to compute a per-attack
    // "chain-link start offset": when firing this attack mid-combo
    // (rather than from peaceful idle), skip past the windup so the
    // swing lands directly in its active phase, matching the previous
    // swing's recovery position. This cuts the visible cut-off-then-
    // restart artifact on chain transitions.
    //
    // Threshold semantics mirror clipJointMotionEnd: per-joint peak
    // step-velocity is found, the joint is "moving" when its velocity
    // first crosses `start_velocity_fraction` of peak.
    //
    // Returns 0 if the joint list is empty or the clip is unloaded
    // (no skip → play from t=0).
    float clipJointMotionStart(const AnimationClip& clip, const std::vector<int>& joint_indices,
                               float sample_hz = 60.0f,
                               float start_velocity_fraction = 0.30f) const;

    // Clip-local time at which the watched joints' velocity is at
    // its peak — i.e. the moment of contact / strike apex. Used to
    // clamp pose-match searches to [motion_start, peak] so the
    // splice can never land in post-contact follow-through (where
    // pose can be coincidentally close to pre-contact pose, since
    // a swing arc passes through the same general region twice —
    // once on the way up, once on the way down).
    //
    // Returns the MAX across watched joints (latest joint to
    // peak); the splice window must be valid for every watched
    // joint so we use the latest peak as the cut-off.
    //
    // Returns 0.5 * clip_duration if joints invalid (rough fallback).
    float clipJointMotionPeak(const AnimationClip& clip, const std::vector<int>& joint_indices,
                              float sample_hz = 60.0f) const;

    // Sample `clip` at clip-time `t_seconds` and return joint
    // `joint_idx`'s world-space (post-LocalToModel) position. Single
    // joint, single time — diagnostic-only. Used to log splice
    // distance between the on-screen pose and the next clip's
    // start-pose. Returns (0,0,0) on invalid inputs.
    glm::vec3 sampleJointWorldPos(const AnimationClip& clip, float t_seconds, int joint_idx) const;

    // Look up a joint by Mixamo bone name. Returns -1 if not found.
    // Used by combat config resolution to translate JSON joint-name
    // overrides (e.g. "mixamorig:RightHand") to skeleton indices for
    // clipJointMotionEnd.
    int findJoint(const char* name) const;

    // Scan `next_clip` at fixed-rate samples and find the time at
    // which the watched joints' positions are closest (in summed
    // Euclidean distance) to those same joints' positions in
    // `prev_clip` at clip time `prev_t_seconds`. Optional
    // `search_window_start/end` clamp the search to a clip-local
    // range — useful for restricting "valid splice points" to e.g.
    // [motion_start, motion_end] so the result is always inside the
    // contact arc, not in pre-windup or post-recovery (where pose
    // matches might be artificially close at neutral stance).
    //
    // Used by combat to pre-compute chain-link splice points: the
    // optimal `start_seconds` to feed playOneShot for swing N+1
    // when swing N has just hit its cancel window. Pose-matching
    // produces a smaller residual offset than velocity-thresholding
    // alone, so the inertialization decay has nearly zero pose gap
    // to bridge.
    //
    // Returns 0 if either clip is unloaded or joints invalid.
    float clipPoseMatchTime(const AnimationClip& prev_clip, float prev_t_seconds,
                            const AnimationClip& next_clip, const std::vector<int>& joint_indices,
                            float search_window_start = 0.0f, float search_window_end = -1.0f,
                            float sample_hz = 60.0f) const;

    // Overload taking the reference joint world positions directly,
    // ordered to match `joint_indices`. Use when the source pose is
    // the live skinned pose (mid-crossfade, post-one-shot, etc.) —
    // the live pose isn't a single clip's pose so the (prev_clip,
    // prev_t) overload would sample the wrong reference.
    float clipPoseMatchTime(const std::vector<glm::vec3>& ref_world_pos,
                            const AnimationClip& next_clip, const std::vector<int>& joint_indices,
                            float search_window_start = 0.0f, float search_window_end = -1.0f,
                            float sample_hz = 60.0f) const;

    // Sample `clip` and return the watched joints' summed forward
    // velocity vector at clip-time `t_seconds`. Computed as the
    // position delta over a one-frame interval (1/60s) starting at
    // t. Used by the chain splice resolver to determine which way
    // the hand is moving at cancel-time of clip N — the next clip's
    // splice point is then chosen so its forward velocity best
    // aligns with this vector, hiding the inertialization smear in
    // the perceived motion arc.
    //
    // Returns (0,0,0) on invalid inputs. The summed vector is the
    // sum across all watched joints (suitable for "overall arm
    // motion" — when joints work together their vectors stack).
    glm::vec3 clipJointVelocityAt(const AnimationClip& clip, float t_seconds,
                                  const std::vector<int>& joint_indices) const;

    // Scan `next_clip` and find the time where the watched joints'
    // forward-velocity vector is BEST ALIGNED (smallest angle) with
    // the supplied reference velocity vector. Search is restricted
    // to [window_start, window_end]. Used by the chain splice
    // resolver to pick a start_seconds for chain-link N+1 such that
    // when the new clip starts playing, the hand moves in the same
    // direction the live (decelerating) hand from clip N was already
    // moving — making the inertialization decay invisible because
    // its smear aligns with the perceived motion arc.
    //
    // Cost function is the angle between vectors (lower is better).
    // Magnitude of the velocity is NOT considered — direction only
    // — because we want the swing's *direction* to feel continuous,
    // not its speed (speed is whatever the clip authored, and is
    // usually correct on its own).
    //
    // Returns (window_start) if velocities are too small to compare
    // reliably, or if inputs are invalid.
    float clipVelocityMatchTime(const glm::vec3& reference_velocity, const AnimationClip& next_clip,
                                const std::vector<int>& joint_indices,
                                float search_window_start = 0.0f, float search_window_end = -1.0f,
                                float sample_hz = 60.0f) const;

    // Sample `clip` at clip-time `t_seconds` and return the hip joint's
    // XZ position in clip-local model space. Diagnostic-only — used to
    // dump a clip's hip trajectory for analysis (e.g. is the travel
    // front-loaded or evenly distributed?). Returns (0,0) if invalid.
    glm::vec2 sampleHipXZAt(const AnimationClip& clip, float t_seconds) const;

    // True when the active one-shot is full-mask AND in Hold. The
    // loco track is fully occluded in this state; gameplay code that
    // would mutate loco state (e.g. SM clip-pick) should defer until
    // this returns false. PoseSampler enforces the gate internally
    // for clip-change; this is exposed so PerFrameTick can suppress
    // upstream side-effects (sLastLocoClipName updates, residual
    // logging) that would otherwise desync against the suppressed
    // swap.
    bool isLocoFrozenByOneShot() const;

    // Ground-Y probe: world (x, z) → world ground Y. Used by foot IK
    // to find the surface under each foot. Caller injects this so the
    // sampler stays decoupled from any specific terrain implementation.
    using GroundProbeFn = std::function<float(float, float)>;

    // Configure foot IK. Two modes, independently togglable:
    //   * position: two-bone IK repositions the ankle vertically to
    //     match terrain offset between the foot and the root (slope
    //     correction). Off by default — needed only for stair-step /
    //     rocky terrain where per-foot Y placement matters.
    //   * orient:   foot joint is rotated so its sole matches the
    //     terrain slope normal under it. The visually important fix
    //     for smooth-heightmap terrain.
    // Probe is shared by both modes. Pass nullptr probe to disable IK.
    void setFootIK(GroundProbeFn probe, bool position_enabled, bool orient_enabled);

    // Per-frame actor placement so the IK can convert between world
    // and model space. Call before update() each frame when IK is on.
    void setActorPlacement(const glm::vec3& world_pos, float yaw_radians);

    // Diagnostic accessors: read the sampler's internal state so the
    // gameplay side can log it for debugging. These are NOT for
    // gameplay logic; they're explicit diagnostic reads.
    struct FrameDiagnostics
    {
        const char* loco_current_name; // ozz Animation::name(), nullptr if no clip
        const char* loco_previous_name;
        const char* one_shot_name;
        float loco_current_time; // clip-time of the active locomotion track
        float loco_blend_weight;
        float loco_blend_elapsed;
        float loco_blend_duration;
        float one_shot_weight;
        int one_shot_phase; // 0=Inactive, 1=BlendIn, 2=Hold, 3=BlendOut
        float one_shot_time;
        float one_shot_duration;
        bool last_hip_xz_valid;
        float last_hip_xz_x;
        float last_hip_xz_z;
        float last_hip_delta_x;
        float last_hip_delta_z;
    };
    FrameDiagnostics frameDiagnostics() const;
};

struct SkeletonJointMap; // anim/SkeletonJointMap.h

// Build a PoseSampler bound to the given skeleton + mesh + joint map.
// The skeleton, mesh, and joint map must outlive the returned sampler.
//
// Overload without joint map fetches the PLAYER's map by default --
// matches every existing call site (player + every humanoid shade).
// New non-humanoid actors (wolf, etc.) pass their own map.
PoseSampler createPoseSampler(const Skeleton& skeleton, const SkeletalMesh& mesh,
                              const SkeletonJointMap& joint_map);
PoseSampler createPoseSampler(const Skeleton& skeleton, const SkeletalMesh& mesh);

} // namespace selva::anim
