#pragma once

#include "anim/SkeletonJointMap.h"
#include "combat/HurtboxDecl.h"
#include "gameplay/Faction.h"
#include "gameplay/Perception.h"

#include <nlohmann/json_fwd.hpp>

#include <filesystem>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

namespace selva::gameplay
{

// One thing an enemy can do — a clip to play, a range it's valid at,
// a cooldown, a weight for random selection. Loaded from JSON; never
// constructed by hand at runtime. Sprint 4's behavior tree's
// LeafPickAction filters the actor's actions[] by range_min..range_max
// + cooldown + min_awareness, then weighted-randoms within what
// remains, then fires the chosen action's clip via playOneShot.
//
// Schema is intentionally narrow for v1 — anything an action might
// also want (animation overrides, sound, damage scaling) can land on
// this struct as fields later without breaking the file format
// (nlohmann's WITH_DEFAULT serialization tolerates missing keys).
struct EnemyAction
{
    std::string id;   // unique key within archetype, e.g. "shade_swing"
    std::string clip; // ozz clip name to play
    float range_min = 0.0f;
    float range_max = 0.0f; // 0 = no upper limit
    float cooldown_seconds = 0.0f;
    float weight = 1.0f;
    int raw_damage = 0;
    int poise_damage = 0;
    float blend_in_seconds = 0.10f;
    float blend_out_seconds = 0.20f;
    bool freeze_last = false;
    Awareness min_awareness = Awareness::Combat;
    // Hitbox geometry — mirrors the player's WeaponAttack schema.
    // Empty hitbox_joint = action plays its clip but spawns no
    // hitbox (e.g. a roar, wind-up taunt). Common case: joint is
    // set, swing fires a hitbox; LeafPickAction calls
    // selva::combat::spawnAttackHitbox.
    std::string hitbox_joint;
    float hitbox_radius = 0.18f;
    float hitbox_tip_offset_z = 0.0f;

    // ----------------------------------------------------------------
    // Souls-style attack timing windows.
    // Every attack splits into windup -> active -> recovery on the
    // clip timeline:
    //   windup_seconds   : t in [0, windup_seconds) -- hitbox INACTIVE.
    //                      Player's read window: "she's about to bite,
    //                      dodge now." Longer = more telegraphed.
    //                      Souls UX: small enemies 150-250ms, bosses
    //                      400-800ms, big slow bosses 1s+.
    //   active_seconds   : t in [windup, windup+active) -- hitbox
    //                      ACTIVE, damage applies on overlap. Typically
    //                      80-200ms so a well-timed dodge passes
    //                      through. Default 0 = fall back to the legacy
    //                      lifetime_fraction calc (whole clip * 0.55).
    //   recovery         : (windup+active, clip_end] -- hitbox
    //                      INACTIVE, animator's follow-through. Player's
    //                      punish window. Length defines fight rhythm:
    //                      short = aggressive boss, long = trade.
    // The BT fires the one-shot immediately, then defers the hitbox
    // spawn by windup_seconds (LeafPickAction queues a PendingAttackSpawn
    // on the actor; tickPendingAttackSpawns spawns it when the wallclock
    // crosses fire_at_time). hitbox lifetime = active_seconds.
    // Defaults preserve existing behavior: windup=0 + active=0 falls
    // back to spawn-now + lifetime_fraction calc.
    float windup_seconds = 0.0f;
    float active_seconds = 0.0f;

    // Souls "commit + recover" model: how far into the one-shot the
    // actor regains control. tickEnemyLocomotion checks
    // isOneShotPastCancelFraction() and resumes the velocity ramp
    // past this fraction (default 1.0 = no early cancel, legacy
    // behavior: actor frozen for the full clip duration). Set to
    // (windup + active) / clip_duration for the cleanest feel --
    // hitbox dies on schedule, body finishes the bite-recovery
    // animation while gameplay-locomotion resumes underneath.
    float cancel_fraction = 1.0f;

    // Hard movement lock for the full clip duration. When true,
    // velocity is zeroed for as long as the one-shot is active --
    // cancel_fraction is ignored for locomotion purposes (it still
    // gates other things like chain-input). Use for heavy / committed
    // swings where the body planting is part of the read (wolf bite:
    // the head lunges visually but the body MUST NOT slide forward
    // toward the player during recovery). Default true matches the
    // safer Souls feel; set false for light/jab attacks where the
    // actor should track the target through recovery.
    bool locks_movement = true;
};

// One enemy archetype = a list of actions + perception overrides.
// Loaded from config/enemies/<id>.json. Actor.archetype points at
// one of these (or nullptr for the test-dummy fallback).
//
// Perception fields are std::optional — present in JSON means
// "override the global tunable for this archetype." Absent means
// "use the global value." That lets a fast scout enemy override
// vision_range while a slow shade inherits the default.
struct EnemyArchetype
{
    std::string id;
    std::vector<EnemyAction> actions;
    std::optional<float> vision_fov_degrees;
    std::optional<float> vision_range_meters;

    // Faction the actor spawns with. Default Hostile preserves legacy
    // behavior (every existing shade / wolf archetype spawns Hostile
    // without authoring the field). NPCs override to Allied (the Guide,
    // companions) or Neutral (friendly-but-passive NPCs who can be
    // aggro'd by the player into Hostile). Read at spawn time by
    // spawnEnemyFromDecl to seed actor.faction.
    Faction faction = Faction::Hostile;

    // Cosmological form. Default DamnedSoul preserves legacy shade
    // behavior (existing shade JSON omits the field; loads as
    // damned-soul which is canonically correct for Hell-resident
    // sinners). Wolf archetype overrides to Animal. Guide overrides
    // to UnjudgedSoul. HellMachinery + Divine reserved for future
    // keeper / Beatrice ships. Read at spawn time to seed actor.form
    // AND to apply per-form stat-spread defaults BEFORE per-archetype
    // overrides (max_hp_override, etc.) layer on top.
    Form form = Form::DamnedSoul;

    // True for dialogue-capable actors. Gates the interaction prompt
    // (press-E-to-talk) + dialogue-tree lookup in npcs/<id>.json.
    // Independent of faction -- an NPC can be Allied (Guide), Neutral
    // (passive merchant), or Hostile (post-betrayal, the Roundtable
    // ghost who turned). Default false so existing enemy archetypes
    // (shade, wolf) don't accidentally read as NPCs.
    bool is_npc = false;
    // Which behavior tree drives this archetype's decisions. Tree
    // construction is in code (see BehaviorTree.cpp's tree-builder
    // registry); JSON just names which one to bind. Defaults to
    // "humanoid_basic" — covers every humanoid in the bestiary
    // until a tree-specific behavior demands its own builder.
    std::string tree_id = "humanoid_basic";
    // Skeleton key (matches SkeletalAssets registry: "player" for
    // humanoids reusing the X_Bot rig; "wolf" / etc. for distinct
    // skeletons). Default "player" preserves today's behavior --
    // every existing humanoid shade reuses the player rig.
    std::string skeleton_id = "player";
    // Per-archetype clip names. Empty = humanoid default (the X_Bot
    // mixamo names). Wolf overrides every entry. Read EXCLUSIVELY via
    // lookupArchetypeClip in Enemies.cpp so the per-skeleton registry
    // is always honored -- the wolf's sampler must never receive a
    // player clip (skel.num_joints != anim.num_tracks -> ozz garbage
    // -> IsNormalizedEst assert).
    std::string idle_clip;            // empty -> "standard_idle"
    std::string combat_idle_clip;     // empty -> "unarmed_combat_idle"
    std::string walk_clip;            // empty -> "walking"
    std::string walk_back_clip;       // empty -> "walking_backward"
    std::string strafe_left_clip;     // empty -> "strafe_walking_left"
    std::string strafe_right_clip;    // empty -> "strafe_walking_right"
    std::string death_clip;           // empty -> "death"
    std::string knockdown_clip;       // empty -> "stunned"
    std::string flinch_front_clip;    // empty -> "flinch_front"
    std::string flinch_back_clip;     // empty -> "flinch_back"
    std::string flinch_left_clip;     // empty -> "flinch_left"
    std::string flinch_right_clip;    // empty -> "flinch_right"
    std::string hit_react_medium_clip; // empty -> "hit_react_medium"
    std::string hit_react_heavy_clip;  // empty -> "hit_react_heavy"
    std::string run_clip;              // empty -> "running" (humanoid sprint clip)

    // Per-archetype chase speed when awareness >= Combat. <= 0 falls
    // back to tun.walk_speed (humanoid shades stay at walking pace).
    // Wolves gallop -- the locomotion IS the aggression -- so wolf.json
    // sets this to ~6 m/s and overrides run_clip to "gallop". The gait
    // picker switches to the Run family when current speed crosses
    // the run threshold below.
    float chase_speed = 0.0f;

    // If true, LeafCircleTarget returns Failure immediately for this
    // archetype -- the BT skips the duel-circle branch entirely and
    // falls through to LeafMoveToTarget (charge-in). Set on quadrupeds
    // and other "no-dance" bosses whose locomotion model is straight-
    // line pursuit. Humanoid shades leave this false (default) to keep
    // the Souls-style mirror-strafe behavior.
    bool disable_circle_strafe = false;

    // Per-archetype HP / poise overrides. <= 0 = fall back to the
    // formula-computed default (Body + Stats). Use for boss-class
    // archetypes whose pool sizes are tuned for encounter feel (Lupa:
    // legend-tier HP, broken poise -- "tired legend" framing). Future
    // bosses can promote to full Stats overrides when the variety of
    // dimensions outgrows two integers.
    int max_hp_override = 0;
    float max_poise_override = 0.0f;

    // Scripted-death timer (seconds). When > 0, the boss dies at
    // (engage_wallclock + scripted_death_seconds) regardless of damage
    // dealt, and player HP is floored at 1 against this boss's hits
    // while it is Engaged or Dying. <= 0 disables both behaviors.
    float scripted_death_seconds = 0.0f;

    // HP drain over the scripted-death window. While Engaged, HP is
    // continuously reduced toward (max_hp * scripted_death_drain_to_fraction)
    // on a curve of exponent scripted_death_drain_exponent (1 = linear,
    // >1 = accelerating near the end). 0.0 fraction = no drain. The
    // drain never reaches 0 HP unless fraction is 0, so the death-
    // trigger remains the timer; player damage can still finish the
    // boss early by driving HP to 0 directly.
    float scripted_death_drain_to_fraction = 0.0f;
    float scripted_death_drain_exponent = 1.0f;

    // Pain clip played once when the scripted-death timer expires,
    // before the actor dies. Reads on the actor's skeleton clip
    // registry. Empty = skip the pain stage and go straight to
    // Felled. The clip's duration drives the Dying-state hold time.
    std::string scripted_death_pain_clip;
    // Per-archetype hurtbox layout. Empty -> the archetype loads
    // the player's hurtboxes (every humanoid shade today). Non-empty
    // overrides (e.g. wolf authors its own 4-or-5 capsules).
    std::vector<selva::combat::HurtboxDecl> hurtbox_decls;

    // Per-archetype lockon points. Empty -> fall back to the
    // skeleton's default_lockon_points (single "chest" for most rigs).
    // Authored for boss-style enemies with multiple targetable parts
    // (Lupa: head/torso/hindleg_*). Player cycles between them with
    // the lockon-cycle input. is_default=true on one entry sets the
    // initial point on acquire; if none flag-default, the first entry
    // wins.
    std::vector<selva::anim::LockOnPointDecl> lockon_points;

    // ----------------------------------------------------------------
    // Boss fields (per boss_backend.md). All default to false / empty
    // so existing non-boss archetypes (limbo_shade) carry no boss
    // semantics. When is_boss is true, the spawn lifecycle changes
    // (see boss_backend.md sections 1-12 + Pattern A/B).
    // ----------------------------------------------------------------
    bool is_boss = false;

    // Display name shown by the boss-GUI on encounter. Italian for
    // legends per [[selva-epistemic-doctrine-2026-05-31]] (e.g.
    // "LUPA"). Empty for non-bosses.
    std::string boss_name;

    // Audio bed key to swap to on encounter start (push). On death
    // (post-felled-overlay), bed pops back to previous. Empty = no
    // swap; encounter uses ambient. Key must exist in audio.json's
    // music section.
    std::string encounter_audio_bed;

    // Text shown briefly in the boss-felled overlay on death.
    // Empty = generic fallback ("<boss_name> FELLED"). Per-boss
    // override lets each boss have a tonally-appropriate line
    // (tragic register for Lupa, etc.).
    std::string felled_message;

    // PlayerProfile flag name to set when this actor dies via
    // fireEnemyDeath. Read by ScriptedEvents.cpp to gate world-state
    // changes. Empty = no flag set on death.
    std::string felled_flag;

    // Whether to show the "X FELLED" overlay when this boss dies.
    // True by default; set false for actors whose death should not
    // surface that framing (e.g. animals -- the soul-fate language
    // doesn't apply).
    bool show_felled_overlay = true;

    // Pattern B (already-there boss): spawn-time state for bosses
    // that are visible before combat begins (sitting, sleeping,
    // waiting). Empty = combat-ready immediately on spawn (Pattern A
    // — most bosses). Lupa: "sitting".
    //
    // Currently a string label; the spawn lifecycle interprets
    // "sitting" as "use the archetype's idle_clip as a held idle
    // and skip the combat AI tick." Future states (sleeping, etc.)
    // wire similarly.
    std::string initial_state;

    // Pattern B: clip to play ONCE when the engage-trigger fires,
    // transitioning the actor from initial_state to combat-ready.
    // After this clip ends, normal combat AI begins. Lupa:
    // "jump_to_idle" (the rise-to-attention animation).
    std::string engage_clip;

    // Pattern B held-pose: clamp-time within idle_clip to freeze on at
    // spawn (sad-look pose for Lupa lands at ~1.69s of
    // idle_2_head_low). Played as a held one-shot (freeze_last +
    // freeze_at_seconds) on top of the idle_clip loco track. 0 =
    // no freeze, idle_clip just loops normally (legacy behavior).
    // Released automatically when the engage trigger fires --
    // engage_clip is a fresh one-shot that crossfades over the held
    // pose, so the 1.69 -> end portion of idle_clip never plays.
    float initial_freeze_at_seconds = 0.0f;
};

// nlohmann JSON I/O for these structs. Defined in EnemyArchetype.cpp
// to keep this header from pulling the full json.hpp.
void to_json(nlohmann::json& j, const EnemyAction& a);
void from_json(const nlohmann::json& j, EnemyAction& a);
void to_json(nlohmann::json& j, const EnemyArchetype& a);
void from_json(const nlohmann::json& j, EnemyArchetype& a);

// Process-wide archetype registry. Loaded once at startup; read by
// gameplay code via archetypes(). Sprint 3 ships with one entry —
// "limbo_shade" — but the registry scales to as many archetypes as
// the bestiary has files for.
class EnemyArchetypeRegistry
{
  public:
    // Load every *.json in `dir` as an archetype. Files whose JSON
    // parse fails are logged and skipped — never throws.
    void loadDirectory(const std::filesystem::path& dir);

    // nullptr if no archetype with that id was loaded.
    const EnemyArchetype* get(const std::string& id) const;

    // Read-only view of the registry. Used by F1 diagnostic panel.
    const std::unordered_map<std::string, EnemyArchetype>& all() const
    {
        return by_id;
    }

  private:
    std::unordered_map<std::string, EnemyArchetype> by_id;
};

// Single global registry. Pattern matches selva::anim::clips().
EnemyArchetypeRegistry& archetypes();

} // namespace selva::gameplay
