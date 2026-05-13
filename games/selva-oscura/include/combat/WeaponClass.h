#pragma once

#include <glm/glm.hpp>
#include <nlohmann/json.hpp>

#include <string>
#include <unordered_map>
#include <vector>

// ---------------------------------------------------------------------------
// WeaponClass — animation + attach data shared across every weapon of a kind.
// Every "longsword" (iron, steel, fancy, cursed) uses the same swing clips
// and attaches to the same hand bone with the same offsets, so all of that
// lives once on the class. Per-weapon data (stats, mesh skin) lives on the
// individual Weapon (see Weapon.h).
//
// Schema split rationale: animations and gameplay numbers have different
// lifecycles. A designer who tunes a weapon's damage shouldn't have to
// re-author the same clip names twenty times across twenty sword variants.
// And a moveset author who retunes the swing shouldn't risk touching
// damage. Two schemas, two owners.
//
// One-handed and two-handed grips are *separate animsets on the same class*
// because the actual motions differ in identity (different attack count,
// different timing, sometimes different combo trees). This is a feature,
// not duplication — it's the lever that lets a katana have unique
// two-handed iaijutsu, a curved sword keep stagger only in one hand, etc.
// ---------------------------------------------------------------------------

namespace selva::combat
{

// One attack within a chain. Matches a clip name in
// assets/characters/<character>/<clip>.ozz.
//
// Cancel-window model (what gates chain advance):
//   * The cancel window OPENS at clip-local time = cancel_open_seconds
//     and never closes within the clip — once open, every press
//     advances the chain until the chain ends or chain_reset_at
//     elapses.
//   * `cancel_open_seconds`: clip-local seconds. If > 0, used as-is
//     (manual override for art-broken clips). If < 0 (default), the
//     cancel-open time is auto-detected at clip-load by scanning the
//     clip's `motion_joints` and finding the first instant after which
//     every watched joint's velocity has dropped below the quiet-
//     threshold. That instant is when the swing has visibly settled
//     back to combat-stance — pressing for the next chain entry then
//     produces a seamless transition (no stall, no cut-off).
//   * `motion_joints`: which joint(s) to watch when auto-detecting.
//     Empty = use the grip-default set (right hand for one-handed,
//     both hands for two-handed) — see resolveAttackMotionEndTime in
//     main.cpp. List specific joints (Mixamo names) to override.
//
// `recovery_seconds` is unrelated to the cancel window — it gates how
// long after the swing starts the chain auto-resets to step 0.
struct WeaponAttack
{
    std::string clip;
    float recovery_seconds = 0.4f;
    // Negative sentinel = auto-detect via clip scan (default).
    float cancel_open_seconds = -1.0f;
    std::vector<std::string> motion_joints;
    // Which input button the player must press to land this step.
    // "" / "any" = either button. "LMB" = left mouse only. "RMB" = right
    // mouse only. Pressing the wrong button is a chain miss.
    std::string expected_button;

    // Joint the hitbox is parented to while this attack swings. Mixamo
    // bone name (e.g. "mixamorig:LeftHand"). Empty = fall back to the
    // weapon's grip bone_right (the standard weapon hand). Set per-clip
    // when the animation drives a non-default limb — e.g. unarmed jab
    // animates the LEFT hand even though the dispatch hand is Right;
    // a kick animates RightFoot; etc.
    std::string hitbox_joint;

    // Hitbox shape. The capsule spans `hitbox_joint` (p0) to a point
    // offset along the joint's forward axis by hitbox_tip_offset_z
    // (p1). For a fist: tip_offset_z = 0 (sphere at the hand). For a
    // sword: tip_offset_z = ~0.8 (blade extends forward from the
    // hand). hitbox_radius is the capsule radius — small for blades
    // (~0.06), wider for fists (~0.18), wider still for heavy
    // bludgeons. 0 = fall back to a sensible default at the spawn
    // site.
    float hitbox_radius = 0.0f;
    float hitbox_tip_offset_z = 0.0f;

    // Poise damage dealt per hit. Drains the target's Poise pool;
    // when poise hits 0 the hit triggers a knockdown chain instead
    // of a normal hit-react. 0 = fall back to attacker's body
    // unarmed_poise_damage at the spawn site. Heavy / committed
    // attacks should set this explicitly (e.g. heavy_punch ~30,
    // jab/hook ~8, kicks higher). Souls-style poise-break model.
    float poise_damage = 0.0f;
    // Override: if >= 0, force resolved_chain_link_start_seconds to
    // this value instead of motion_start - 0.05. Set to 0.0 for clips
    // that have a Blender-authored bookend in their first ~5-10
    // frames — we want the splice to enter at frame 0 so the bookend
    // pose is what the player sees, not skipped past as windup.
    float chain_link_start_seconds = -1.0f;

    // Override: blend duration when entering THIS clip as a chain
    // link. >= 0 = use this; < 0 = fall back to the global
    // combo_chain_blend_seconds tunable. Per-attack so a transition
    // with a large pose mismatch (e.g., slash → slash_3 where the
    // hip swings reverse direction) can have a longer decay window
    // for slow-moving joints (legs, hips), without dragging out
    // transitions whose poses already match (e.g., slash_3 → slash_4).
    // Mirrors chain_link_start_seconds — different transitions need
    // different timings; one global value can't serve both.
    float chain_link_blend_seconds = -1.0f;

    // Override: how long the one-shot's BlendOut window is. Larger =
    // more of the clip's tail is hidden under the loco-track reveal,
    // effectively trimming the visible recovery (useful for clips
    // with long authored waddles like the unarmed combo finisher).
    // >= 0 = use this; < 0 = fall back to the playOneShot default
    // (0.20s). Clamped to clip duration internally.
    float blend_out_seconds = -1.0f;

    // Override: clip-time at which a FIRST-STRIKE fire of this
    // attack begins (i.e. when no other one-shot is active).
    // Default behavior: pose-match scans the first 0.30s of the
    // clip and picks the best splice time. Some attacks (e.g. the
    // running flying-knee) have a t=0 windup pose that's
    // geometrically far from the live gait pose; the 0.30s scan
    // window can't find a close match, so the splice snaps. Set
    // this to skip past the windup into a frame closer to the
    // gait pose. Negative = use the auto pose-match.
    float first_strike_start_seconds = -1.0f;

    // Populated at load-time by resolveCancelOpenTime(). Holds the
    // effective clip-local cancel-open seconds (either the override
    // or the result of the joint-velocity scan). Not serialized to
    // JSON — purely a runtime cache that lives next to the loaded
    // class so chain code can read it without re-scanning.
    float resolved_cancel_open_seconds = -1.0f;
    // Clip-local seconds at which to begin playback when this attack
    // fires AS A CHAIN LINK (not first-strike). Auto-detected from
    // the watched joints' motion-start: the first frame any watched
    // joint's velocity crosses the start threshold. Skipping past
    // the windup avoids the "previous swing visibly cut off → next
    // swing slowly winds up from neutral" artifact — instead the
    // next clip plays directly from contact onward, where the
    // previous swing's recovery left off. First-strike still uses
    // 0 (full windup) for the visible "settle into stance, then
    // swing" beat.
    float resolved_chain_link_start_seconds = 0.0f;
};
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(WeaponAttack, clip, recovery_seconds,
                                                cancel_open_seconds, motion_joints,
                                                chain_link_start_seconds, chain_link_blend_seconds,
                                                blend_out_seconds, first_strike_start_seconds,
                                                expected_button, hitbox_joint, hitbox_radius,
                                                hitbox_tip_offset_z, poise_damage);

// All attack clips available within one grip mode. Slots:
//   light   — standing or walking primary attack chain
//   heavy   — Shift-modified primary attack chain; bigger swings, longer recovery
//   running — sprint-cancel attack; lunges forward,
//             distinct from light/heavy. Optional; if empty, gameplay
//             falls back to light when sprinting + attacking.
//
// A named technique = an ordered chain of attacks. Player presses
// follow `attacks[i].expected_button` per step; chain breaks on
// mismatch.
struct WeaponTechnique
{
    std::string id;
    std::vector<WeaponAttack> attacks;
};
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(WeaponTechnique, id, attacks);

// All techniques available within one grip mode. The runtime picks
// which technique to follow based on the second press's button (the
// first press is always slot 0 of every technique). When techniques
// share slot-0 button + slot-1 button, the first-listed wins.
struct WeaponGripAnimSet
{
    std::vector<WeaponTechnique> light;
    std::vector<WeaponTechnique> heavy;
    std::vector<WeaponTechnique> running;
};
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(WeaponGripAnimSet, light, heavy, running);

// Where the weapon mesh attaches when held in either hand. Same offsets
// for both — different weapons sit differently in the hand, but a given
// weapon's grip pose doesn't change between hands. Bone names follow
// Mixamo convention (mixamorig:RightHand / mixamorig:LeftHand).
struct WeaponAttach
{
    std::string bone_right = "mixamorig:RightHand";
    std::string bone_left = "mixamorig:LeftHand";
    glm::vec3 offset_translation = glm::vec3(0.0f);
    glm::vec3 offset_rotation_euler_deg = glm::vec3(0.0f);
};
// glm::vec3 is not directly serializable by nlohmann's macros, so the
// to_json/from_json for WeaponAttach are hand-written below.
void to_json(nlohmann::json& j, const WeaponAttach& a);
void from_json(const nlohmann::json& j, WeaponAttach& a);

// The full class record. Loaded once per class file at startup.
struct WeaponClass
{
    std::string id;
    WeaponGripAnimSet one_handed;
    WeaponGripAnimSet two_handed;
    WeaponAttach attach;
    // Trim leading idle frames off the block clip. >= 0 = use as-is;
    // < 0 (default) = auto-detect via hand-velocity scan, same as
    // chain-link motion-start.
    float block_clip_start_seconds = -1.0f;
    // Per-class attack playback rate. <= 0 = fall back to the global
    // tunables.attack_playback_rate. Lets unarmed punches (snappier)
    // and sword swings (heavier) keep their authored pace independently.
    float attack_playback_rate = 0.0f;
};
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(WeaponClass, id, one_handed, two_handed, attach,
                                                block_clip_start_seconds, attack_playback_rate);

// Registry of all WeaponClasses, keyed by `id`. Populated at startup by
// scanning config/weapon_classes/*.json. Pointers handed out from get()
// remain stable for the lifetime of the registry.
struct WeaponClassRegistry
{
    std::unordered_map<std::string, WeaponClass> by_id;

    // Load every *.json in `dir` (non-recursive) into the map. Returns
    // the number of classes loaded; logs and skips files that fail to
    // parse. dir is interpreted relative to the current working dir
    // (typically build/bin/, where assets and config are synced).
    int loadDirectory(const std::string& dir);

    // Returns nullptr if the id wasn't loaded.
    const WeaponClass* get(const std::string& id) const;
};

} // namespace selva::combat
