#pragma once

#include <glm/vec3.hpp>
#include <nlohmann/json.hpp>

#include <string>

// ---------------------------------------------------------------------------
// Tunables — every numeric "feel" parameter for selva-oscura, in one place.
// Loaded from JSON at startup (config/tunables.json), edited at runtime via
// the in-game ImGui panel (F1), saved back to disk on demand. Plain struct
// of floats; one global instance shared across gameplay code and the
// procedural animation driver.
//
// Adding a tunable:
//   1. Add a field below with a default value.
//   2. Add the field name to the NLOHMANN_DEFINE_TYPE_INTRUSIVE_WITH_DEFAULT
//      macro at the bottom (this generates JSON load + save).
//   3. Add a slider entry in selva::tuning::renderImGui (in main.cpp).
// That's it — no constexpr, no rebuild-to-tune.
//
// Industry context: same pattern as Rockstar's Tunables, Source's cvars,
// Unreal's GameUserSettings. Plain struct + JSON + ImGui scales cleanly
// to ~50 fields; revisit (sub-structs, reflection macro, user-scope file)
// when we cross that threshold. See conversation notes 2026-05-05.
// ---------------------------------------------------------------------------

// glm::vec3 JSON I/O. Placed in the `glm` namespace so nlohmann's
// internal get<glm::vec3>() finds them via ADL. nlohmann doesn't
// ship a glm specialization out of the box; this is the standard
// recipe for binding glm types to nlohmann.
namespace glm
{
inline void to_json(nlohmann::json& j, const vec3& v)
{
    j = nlohmann::json::array({v.x, v.y, v.z});
}
inline void from_json(const nlohmann::json& j, vec3& v)
{
    if (j.is_array() && j.size() == 3)
    {
        v.x = j[0].get<float>();
        v.y = j[1].get<float>();
        v.z = j[2].get<float>();
    }
}
} // namespace glm

namespace selva::tuning
{

// Lighting sub-struct -- kept nested so the flat-field count in
// Tunables stays under the NLOHMANN macro's variadic cap. Holds every
// value the scene/sky/terrain/tree/skeletal shaders read from the
// atmosphere module. Live-mutable via the F1 panel's Lighting tab.
//
// All color values are LINEAR-SPACE. Authoring sRGB defaults +
// converting in source is a future helper; for now defaults are
// stored linear and the F1 panel surfaces them directly.
struct Lighting
{
    // Direction TO the sun (raw, gets normalized on read). +X right,
    // +Y up, +Z out-of-screen (so the wood's twilight beacon is -Z).
    glm::vec3 sun_dir = glm::vec3(0.0f, 0.42f, -0.91f);

    // HDR radiance fed into the Rayleigh+Mie scattering integral.
    // Big numbers (>>1) are expected: this is pre-tonemap radiance,
    // not display color. Reinhard saturates above ~3 so changes here
    // mostly affect twilight tints, not headline brightness.
    glm::vec3 sun_intensity = glm::vec3(11.0f, 9.5f, 7.0f);

    // Post-tonemap multiplier (acts BEFORE Reinhard saturation, so
    // this is the most effective overall-brightness knob). 1.0 =
    // pre-rewrite default; lower dims everything uniformly.
    float exposure = 0.7f;

    // Color of the directional sun term in the world's lighting
    // model. Multiplied with half-Lambert + shadow for the lit-side
    // contribution on terrain/tree/region/skeletal. Linear-space.
    glm::vec3 sun_tint = glm::vec3(0.55f, 0.40f, 0.25f);

    // Hemispheric ambient terms. Sky-facing surfaces lerp toward
    // sky_ambient; ground-facing toward ground_ambient. Linear-space.
    glm::vec3 sky_ambient = glm::vec3(0.0272f, 0.0397f, 0.0637f);
    glm::vec3 ground_ambient = glm::vec3(0.0072f, 0.0049f, 0.0039f);
};

inline void to_json(nlohmann::json& j, const Lighting& l)
{
    j = nlohmann::json{
        {"sun_dir", l.sun_dir},         {"sun_intensity", l.sun_intensity},
        {"exposure", l.exposure},       {"sun_tint", l.sun_tint},
        {"sky_ambient", l.sky_ambient}, {"ground_ambient", l.ground_ambient},
    };
}
inline void from_json(const nlohmann::json& j, Lighting& l)
{
    // Per-field defaults from a fresh struct so missing keys fall
    // back to the in-source defaults (forward-compat with older
    // tunables.json files that predate this sub-struct).
    const Lighting d{};
    auto readVec3 = [&j](const char* key, const glm::vec3& fallback) -> glm::vec3
    {
        auto it = j.find(key);
        if (it == j.end())
            return fallback;
        return it->get<glm::vec3>();
    };
    l.sun_dir = readVec3("sun_dir", d.sun_dir);
    l.sun_intensity = readVec3("sun_intensity", d.sun_intensity);
    l.exposure = j.value("exposure", d.exposure);
    l.sun_tint = readVec3("sun_tint", d.sun_tint);
    l.sky_ambient = readVec3("sky_ambient", d.sky_ambient);
    l.ground_ambient = readVec3("ground_ambient", d.ground_ambient);
}

struct Tunables
{
    // ---- Debug ----
    // Global time-scale multiplier for the per-frame dt fed into
    // selvaPerFrame. 1.0 = normal speed. <1.0 = slow-mo (every
    // animation, lockout, timer, sampler advance scales down). >1.0 =
    // fast-forward. Lets split-second animation transitions unfold
    // over more wall-clock time so smoothness issues become visible.
    // Doesn't affect input edge timing or render rate; only the
    // simulation dt that drives time-advancing systems.
    float time_scale = 1.0f;

    // ---- Locomotion (velocity-driven) ----
    // Player yaw rotation rates (rad/s) toward move-intent direction.
    // The actual rate per frame is lerped between min and max by
    // |yaw_delta|/π so small course corrections stay smooth and
    // big reversals (W → S, pressing the opposite direction) snap
    // fast enough to feel responsive in combat.
    float turn_rate_min = 9.0f;  // small course corrections
    float turn_rate_max = 25.0f; // 180° reversal — snaps

    // Lock-on camera + body yaw smoothing rates. Hard-snapping yaw to
    // a MOVING target every frame produces visible jolt (target moves
    // -> camera+body jump -> next frame target moves -> jump again).
    // Souls/ER ease toward the target dir at a tunable rate so the
    // camera lags slightly behind a sprinting boss instead of teleporting.
    // Body rate slightly snappier than camera so the player always
    // faces the target while the camera reads-tracks behind. Set high
    // for snappy combat, lower for cinematic lag. Both in rad/s.
    float lockon_camera_yaw_rate = 12.0f;
    float lockon_body_yaw_rate = 16.0f;

    // Legacy SM debounce — UNUSED in the velocity-driven model
    // (velocity itself is naturally smoothed by accel/decel). Kept
    // for the JSON schema during the refactor; will be removed
    // once the SM is fully deleted.
    float wasd_debounce_seconds = 0.10f;

    // Global loco-track playback rate multiplier. Mirrors
    // attack_playback_rate on the one-shot side. Per-clip JSON
    // override available via LocomotionClipConfig::playback_rate
    // (default 0 = use this global).
    float loco_playback_rate = 1.0f;

    // Extra multiplier applied while armed + sprinting -- the armed
    // run clip is reused for the sprint tier and played faster
    // instead of authoring a separate sprint clip.
    float armed_sprint_run_multiplier = 1.4f;
    // Same pattern for the unarmed side: sprint reuses the jogging
    // clip (humanoid_unarmed.json's LocoSprint = "jogging") played
    // faster instead of a dedicated sprint clip.
    float unarmed_sprint_run_multiplier = 1.3f;

    // Target speed (m/s) for the LAlt-held Walk tier (DS3 PC convention).
    // Calibrated to match walking.ozz's authored hip travel so the
    // foot doesn't visibly skate.
    float walk_speed = 1.6f;

    // Target speed (m/s) for the default Jog tier (plain WASD).
    // Calibrated to jogging.ozz (renamed from running.ozz on
    // 2026-06-03 to free the "sprint" name for the new faster gait).
    float jog_speed = 4.5f;

    // Target speed (m/s) for the Space-held Sprint tier. Calibrated
    // to sprinting.ozz's authored hip travel (Faster Running.fbx).
    float sprint_speed = 7.5f;

    // Acceleration toward target speed (m/s²). Higher = snappier
    // response to WASD press. Too high reads as "teleport into
    // motion"; too low feels sluggish.
    float locomotion_accel = 18.0f;

    // Deceleration toward zero when WASD released (m/s²). Separate
    // from accel because the feel is different — Souls-style
    // games often have faster decel than accel for responsive
    // stops. Too high: instant snap to idle (the "shoots back to
    // idle" complaint); too low: slidey overshoot.
    float locomotion_decel = 14.0f;

    // Clip-tier speed thresholds. velocity_magnitude:
    //   <= idle_to_walk_speed   → idle
    //   <= walk_to_jog_speed    → walking
    //   <= jog_to_sprint_speed  → jogging
    //   >  jog_to_sprint_speed  → sprinting
    // Bands keep transitions hysteresis-free at the input edges since
    // the picker reads TARGET speed (where intent is heading), not
    // current velocity.
    float idle_to_walk_speed = 0.30f;
    float walk_to_jog_speed = 3.0f;
    float jog_to_sprint_speed = 5.5f;

    // ---- Mouse-look ----
    float mouse_sensitivity = 0.0025f; // radians per pixel
    float pitch_min = -1.45f;          // ~-83 deg
    float pitch_max = 1.45f;           // ~+83 deg

    // ---- Camera follow ----
    // Ideal arm length. The pull-in pipeline shrinks this dynamically
    // when geometry blocks the ray, so this is the OUTER bound the
    // camera tries to maintain when nothing's in the way.
    float follow_distance = 6.0f;
    float follow_height = 2.5f;
    float fov_degrees = 60.0f;
    // Camera pull-in: raycast from the lookAt anchor toward the
    // ideal camera position; if a wall or tree blocks the view, the
    // camera slides forward to the hit point minus this margin so it
    // sits just clear of the surface. ~0.3m keeps the lens off the
    // wall texture without revealing the player's back.
    float camera_pull_in_margin = 0.3f;
    // Sphere-cast radius around the ray. Treats the camera as a small
    // volume rather than a point so a wall edge near the camera
    // produces tighter pull-in instead of the camera wrapping its
    // view frustum around the edge. ~0.4m approximately matches the
    // camera's near-plane width at 60deg FOV.
    float camera_pull_in_radius = 0.4f;
    // Minimum allowed camera-to-player distance. Below this the near
    // plane reaches into the player mesh and renders the inside of
    // the chest geometry (black screen / skin-color flat fill). Must
    // be >= sphere_radius + half-near-plane-extent + body-mesh
    // radius to keep the lens outside the player's torso.
    float camera_pull_in_min_separation = 0.5f;
    // Smoothing time-constant for the actual follow distance toward
    // the raycast-derived target. Short (~80ms) so wall transitions
    // glide instead of snapping when the player edges past a corner.
    float camera_pull_in_tau = 0.08f;

    // ---- Animation transitions ----
    // Cross-fade duration (seconds) between two clips when the active
    // selection changes (Idle ↔ Walk ↔ Run). 0 disables the fade and
    // snaps instantly. ~0.15-0.25s reads as "smooth" without making
    // the transition feel laggy.
    float anim_blend_seconds = 0.20f;
    // How long to remain in CombatReady stance after the last combat
    // input. The "stays braced for a moment" feel — long
    // enough that a quick re-engage doesn't snap back to peaceful;
    // short enough that walking away clearly drops the stance.
    float combat_idle_grace_seconds = 2.0f;
    // Delay (wall-clock seconds) between a Peaceful → Block press
    // (RMB) and the block one-shot actually firing. During this
    // window the locomotion track crossfades from standard_idle to
    // the combat-stance idle clip, so the player visibly settles
    // into a guard pose BEFORE the shield raise. Sized to roughly
    // match the cross-fade duration into the combat-stance loop.
    // 0 disables the beat (instant-fire).
    //
    // Attacks fire instantly regardless of this delay; the per-joint
    // inertialization decay handles the standard_idle → combat-idle
    // handoff for attacks. Only blocks honor this delay because their
    // hold-RMB semantics demand the establishing beat — you wouldn't
    // want RMB to read as "I tapped block while still standing
    // relaxed."
    float combat_entry_delay_seconds = 0.18f;

    // ---- Attack combos ----
    // After a chain attack lands, players have this much wall-clock
    // time to press attack again (within the active clip's cancel
    // window) before the chain resets to step 0. Sized so a slow
    // press still extends the chain, but walking-away-then-attacking
    // clearly restarts at slash 1.
    float combo_reset_grace_seconds = 0.5f;
    // If the player presses attack BEFORE the current swing's cancel
    // window opens, that press is held in a buffer and auto-fires when
    // the window opens. Buffered presses expire after this many
    // wall-clock seconds — long enough that mashing players don't get
    // inputs eaten, short enough that a stale press doesn't trigger
    // an attack the player no longer wants.
    float combo_input_buffer_seconds = 0.20f;
    // Cross-fade duration between chain steps. The first attack of a
    // chain uses 0.10s (snappy first-strike); subsequent steps use
    // this longer value so the body has time to morph from one pose
    // to the next instead of visibly snapping. 0.20-0.25s is a
    // reasonable starting range; iteration may move it.
    float combo_chain_blend_seconds = 0.22f;
    // Blend-in for the FIRST attack out of combat-idle. Larger = more
    // forgiving when idle pose is far from the action's t=0.
    float first_strike_blend_seconds = 0.10f;
    // Inertialization scaling fields removed — no game-side profile
    // enrolls inertialization. The engine retains the capability
    // (PoseSampler::setInertializationScaling) for future opt-in
    // callers; this game does not configure it.
    // Playback-rate multiplier applied to attack one-shots. >1 plays
    // faster, scaling clip duration by 1/rate. Sword-and-shield attack
    // clips were authored at a deliberate combat pace; the multiplier
    // tunes them for a snappier feel. Beyond ~1.6 the windup
    // disappears and reads as a teleport-swing.
    float attack_playback_rate = 1.3f;
    // Threshold (fraction of peak hand-velocity) at which the auto-
    // detected cancel window opens. Applied at clip-load time when
    // resolving each attack's cancel-open seconds.
    //   * 0.10 = full settle: hand has returned to stance and stopped
    //     moving. Pure pose-bridge inertialization with no
    //     velocity continuity.
    //   * 0.50 = mid-follow-through: hand is past contact and still
    //     decelerating but not at rest yet. The next swing's blend
    //     starts from a still-moving pose, producing visible
    //     velocity continuity and a less "stop-and-restart" feel.
    //   * 0.80 = near peak: cancel almost on contact. Very snappy,
    //     visibly cuts the swing short.
    // 0.50 is the practical sweet spot for clips that weren't authored
    // with bookend matching: cancel mid-follow-through (hand past
    // contact, still in motion) so the chain-link splice enters the
    // next swing with continuous velocity rather than restarting from
    // rest. 0.10 = full settle, slower rhythm. Above ~0.70 cancels
    // too close to contact and the swings visibly cut short.
    float cancel_open_velocity_fraction = 0.50f;
    // Threshold for a "perfect" chain press. The accuracy score is
    // 1.0 at dead-center of the rhythm window and 0.0 at the edges.
    // A press at >= this threshold is considered perfect. 0.85 = a
    // press within 7.5% of either edge of the window from center
    // (i.e., the middle 70% of the window). Tighten toward 1.0 for
    // a stricter "bullseye" feel; loosen toward 0.5 to make perfect
    // routine.
    float perfect_accuracy_threshold = 0.85f;

    // ---- Dodge ----
    // Playback-rate multiplier applied to the dodge/roll one-shot
    // clip. >1 = snappier, lighter roll feel; 1.0 = real-time.
    // The clip's authored hip motion drives world translation (root
    // motion); rate scales how fast the clip plays AND therefore how
    // quickly the character covers the clip's authored distance.
    float roll_playback_rate = 1.3f;
    float backstep_playback_rate = 1.3f;
    // How long Space can be held before it commits to "sprint" instead
    // of "dodge" (seconds). Tap shorter than this → dodge fires on
    // release. Hold longer than this → sprint state engages.
    float dodge_tap_window = 0.20f;
    // Max angular rate (rad/s) the player can re-aim their roll
    // mid-dodge by turning the camera. We re-derive direction from
    // the current camera-relative WASD intent and steer toward it
    // with this clamp. ~5 rad/s ≈ ~285°/s, which lets a 0.6s roll
    // bend ~170° at most — sharp enough to feel responsive, capped
    // enough that you can't pirouette out of a swing.
    float dodge_steer_rate = 5.0f;
    // Cancel fractions are now declared per-profile in TransitionProfile
    // (profiles::dodge(), profiles::jump(), etc.) so every commit-then-
    // recover one-shot uses the same mechanism (see PoseSampler::
    // isOneShotPastCancelFraction). dodge_cancel_fraction and
    // dodge_attack_cancel_fraction were both 0.65 and have been folded
    // into profiles::dodge().cancel_fraction.

    // Wall-clock seconds past cancel_window_close at which walking
    // becomes responsive again post-attack. Anchoring the lockout to
    // cancel_window_close (instead of the full clip-end + grace)
    // lets the player resume walking while the swing's recovery tail
    // animates underneath via the one-shot blend-out. The clip's
    // visible motion finishes naturally; the player isn't locked
    // through it. ~0.10s is a small breathing room past the rhythm
    // window so chain advances aren't fighting walking input.
    float attack_lockout_extension_seconds = 0.10f;

    // ---- Sprint-finisher (running attack) ----

    // ---- Actor formulas (player + enemies, see gameplay/Actor.h) ----
    // Per-stat scaling for derived pools is owned by
    // engine::ecs::FormulaConfig (loaded by selva::formulas::current()
    // from config/balance/formulas.json) -- NOT here. Tunables holds
    // camera/animation/combat-feel knobs only.

    // Floor on damage after defense reduction. Even heavily-armored
    // targets take this much per hit so combat never stalls on
    // unbreakable defense numbers. 1 is a soulslike default.
    float damage_floor = 1.0f;

    // ---- Enemy combat feel (gameplay/Enemies.cpp) ----
    // Damage thresholds for hit reaction tiers. Hits below
    // medium play upper-body flinches; >= medium play
    // hit_react_medium; >= heavy play hit_react_heavy. Will become
    // per-attack-declared (and poise-driven) later.
    float hit_react_medium_threshold = 25.0f;
    float hit_react_heavy_threshold = 45.0f;

    // Minimum wallclock seconds between consecutive hit-react fires
    // on the same enemy. Without this, fast multi-hits re-trigger
    // the one-shot from t=0 every frame and the enemy looks frozen.
    float hit_react_cooldown_seconds = 0.30f;

    // How long an enemy stays in the knockdown freeze pose before
    // recovering in place. The sampler blends back to combat idle
    // via the one-shot's blend_out window; no get-up clip plays.
    float enemy_recovery_after_knockdown_seconds = 2.5f;

    // Enemy corpse fade-out timing. Death triggers, then `hold` seconds
    // of full alpha (read-the-kill beat), then linear fade to alpha=0
    // over `duration` seconds, then the draw is skipped. Cosmologically
    // the body dissolves back into Hell's substance per
    // [[project_soul_larvae_cosmology]] dissolution doctrine.
    float enemy_death_fade_hold_seconds = 0.3f;
    float enemy_death_fade_duration_seconds = 0.7f;

    // ---- AI perception (Sprint 1) ----
    // Forward-facing vision cone. FOV is the full angular spread (so
    // 90 degrees = 45 degrees off each side of forward). Range is the
    // max distance at which a target inside the cone is "seen."
    float ai_vision_fov_degrees = 90.0f;
    float ai_vision_range_meters = 12.0f;

    // How long the actor stays in Suspicious before decaying back to
    // Unaware if no further sightings happen.
    float ai_suspicion_decay_seconds = 1.5f;

    // How many vision-confirmed sightings inside the suspicion window
    // are required to escalate Suspicious -> Alerted. 1 = snap-aggro;
    // 3+ = the "double-take" Souls feel.
    int ai_confirmed_sightings_to_alert = 2;

    // How long Alerted decays back to Suspicious if no further contact.
    float ai_alerted_decay_seconds = 4.0f;

    // Distance at which an Alerted actor commits to Combat.
    float ai_combat_engage_range_meters = 5.0f;

    // Souls-style leash: once an actor enters Combat awareness, they
    // stay in Combat as long as the player is within this distance.
    // Combat is retained REGARDLESS of vision — the enemy "knows" the
    // player is engaged with them and tracks position continuously.
    // Decays back to Alerted only when the player exceeds this range.
    // This is what makes a knocked-down enemy stay engaged after
    // recovery: their cone may not see the player (e.g. player ran
    // behind them), but the enemy still knows the player exists and
    // walks toward them. Vision drives initial engagement (Unaware →
    // Suspicious → Alerted); leash drives retention. Generous default
    // matches Souls convention — the player has to genuinely flee
    // to lose aggro.
    float ai_combat_leash_range_meters = 25.0f;

    // How long Combat decays back to Alerted if the player remains
    // outside leash range. Acts as hysteresis on the leash boundary
    // — without this, oscillating in and out of leash range would
    // flicker the awareness state. Kept short so disengage feels
    // responsive once the player has actually escaped.
    float ai_combat_disengage_seconds = 1.0f;

    // ---- AI decision-tick scheduler (Sprint 2) ----
    // Baseline rate at which an actor's decision-making code (behavior
    // tree, action selection) re-evaluates. Perception still runs at
    // full render rate; only the *decision* layer is throttled. Souls
    // convention: ~10Hz baseline.
    float ai_decision_tick_hz = 10.0f;

    // Multiplier applied to ai_decision_tick_hz when the actor's
    // awareness is Combat. Alerted enemies "think faster" than
    // dormant ones. 1.0 = no change; 1.5 = combat at 15Hz when
    // baseline is 10Hz.
    float ai_decision_tick_combat_hz_multiplier = 1.5f;

    // ---- AI locomotion (Sprint 4a) ----
    // Rate at which an AI actor rotates toward its turn_intent_yaw.
    // Souls convention: enemies turn faster than they move, so they
    // can re-orient before walking into a new direction. Per-
    // archetype overrides land in Sprint 4b.
    float ai_turn_rate_radians_per_sec = 6.0f;

    // Action-fire freshness gate: how recently must the actor have
    // SEEN the player to fire an attack (vs walk to investigate).
    // Souls rule: don't punch into empty air when you've lost
    // sight of the target. If perception's last_seen_time is older
    // than this window, LeafPickAction returns Failure and the
    // Selector falls through to LeafMoveToTarget — the actor walks
    // toward last_known_player_pos, rotates as it walks, and its
    // vision cone sweeps until it re-acquires. Surfaces most
    // visibly after knockdown recovery: the actor stood face-down
    // during the fall (cone in the dirt), woke up with stale
    // last_known_player_pos, and would otherwise swing in the
    // wrong direction.
    float ai_action_freshness_seconds = 0.5f;

    // ---- Poise / knockdown ----
    // Stat-scaling (poise_per_end, poise_per_str) and decay-window live
    // in engine::ecs::FormulaConfig (config/balance/formulas.json).

    // Knockdown chain clip trimming. Both knockdown and getting_up
    // clips often have authored windup or trailing idle that we
    // want to skip. The phase plays from `*_start_seconds` to
    // `*_end_seconds` (or clip duration, whichever is shorter).
    // Phase advances when wallclock elapsed reaches
    // (end_seconds - start_seconds). Set start > 0 to skip windup,
    // set end < clip_duration to cut trailing idle.
    float knockdown_clip_start_seconds = 0.0f;
    float knockdown_clip_end_seconds = 999.0f;
    float getting_up_clip_start_seconds = 0.0f;
    float getting_up_clip_end_seconds = 999.0f;

    // ---- Per-clip audio event time (running jump) ----
    // Time (seconds into the flying_knee_punch_combo clip) at which the
    // left-arm whoosh should fire. Tunable so iteration is "tweak
    // value, hit F1 save, listen" without code change. The fire
    // happens on the first per-frame tick where the one-shot's clip
    // time crosses this threshold. Set to a negative value to
    // disable.
    float flying_knee_whoosh_time_seconds = 0.95f;

    // ---- First-person camera offsets ----
    // Lift from the `mixamorig:Head` joint (sits at head-base /
    // neck-top) up to the player's eye line. Applied along the head's
    // local up axis so the offset rotates correctly during rolls.
    // Tune live to match the visible character's eyes.
    float fpv_eye_up_offset = 0.15f;
    // Forward push along the camera-fwd axis so the camera origin
    // sits just outside the skull (avoids near-plane clipping into
    // the back of the head when the player looks straight up).
    float fpv_eye_fwd_offset = 0.12f;

    // ---- Lighting ----
    // Atmosphere / scene-lighting state. See the Lighting struct
    // above for per-field doctrine. Lifted from constexpr in
    // src/render/Atmosphere.cpp on 2026-06-30 so the F1 panel can
    // drive it live instead of edit-rebuild-relaunch per tweak.
    Lighting lighting{};

    // ---- Debug ----
    // Session-only diagnostic toggles live in selva::debug::Flags
    // (include/debug/Flags.h). They are deliberately NOT here:
    // Tunables ships and serializes; Flags don't.
};

// JSON serialization — generates to_json / from_json for nlohmann::json
// at namespace scope (NON_INTRUSIVE keeps the struct itself macro-free).
// "_WITH_DEFAULT" means missing keys in the input fall back to the struct's
// default-initialized value, so older tunables.json files don't break when
// new fields are added.
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(
    Tunables, time_scale, turn_rate_min, turn_rate_max, lockon_camera_yaw_rate,
    lockon_body_yaw_rate, wasd_debounce_seconds, loco_playback_rate, armed_sprint_run_multiplier,
    unarmed_sprint_run_multiplier, walk_speed, jog_speed, sprint_speed, locomotion_accel,
    locomotion_decel, idle_to_walk_speed, walk_to_jog_speed, jog_to_sprint_speed, mouse_sensitivity,
    pitch_min, pitch_max, follow_distance, follow_height, fov_degrees, anim_blend_seconds,
    combat_idle_grace_seconds, combat_entry_delay_seconds, combo_reset_grace_seconds,
    combo_input_buffer_seconds, combo_chain_blend_seconds, first_strike_blend_seconds,
    attack_playback_rate, cancel_open_velocity_fraction, perfect_accuracy_threshold,
    roll_playback_rate, backstep_playback_rate, dodge_tap_window, dodge_steer_rate,
    attack_lockout_extension_seconds, damage_floor, hit_react_medium_threshold,
    hit_react_heavy_threshold, hit_react_cooldown_seconds, enemy_recovery_after_knockdown_seconds,
    enemy_death_fade_hold_seconds, enemy_death_fade_duration_seconds, knockdown_clip_start_seconds,
    knockdown_clip_end_seconds, getting_up_clip_start_seconds, getting_up_clip_end_seconds,
    ai_vision_fov_degrees, ai_vision_range_meters, ai_suspicion_decay_seconds,
    ai_confirmed_sightings_to_alert, ai_alerted_decay_seconds, ai_combat_engage_range_meters,
    ai_combat_leash_range_meters, ai_combat_disengage_seconds, ai_decision_tick_hz,
    ai_decision_tick_combat_hz_multiplier, ai_turn_rate_radians_per_sec,
    ai_action_freshness_seconds, flying_knee_whoosh_time_seconds, lighting);
// NOTE: fpv_eye_up_offset, fpv_eye_fwd_offset are NOT serialized -
// they're live-tuning fields. The NLOHMANN macro has a ~64-field
// variadic limit; the count is fine now (debug_* moved out to
// selva::debug::Flags) so any new tunable can be added freely. If
// the count creeps back toward 60 reconsider; until then it's room.
// NOTE: camera_pull_in_margin and camera_pull_in_tau still NOT
// serialized for legacy reasons; live-tuned via F1 panel and the
// dialed-in values bake into the struct defaults.

// Single global instance. Both gameplay code and the procedural driver
// read from this; the ImGui panel edits it in place. Keep it global rather
// than threading it through every function — there's exactly one game.
Tunables& current();

// JSON I/O. Path is relative to the working directory (typically
// build/bin/, which has its own copy synced from the source tree). Loads
// silently fall back to defaults if the file is missing or malformed —
// the game must remain runnable on a fresh checkout.
bool loadFromFile(const std::string& path);

// Saves to a relative path. Returns true on success. Used by the "Save"
// button in the ImGui panel; not called automatically.
bool saveToFile(const std::string& path);

} // namespace selva::tuning
