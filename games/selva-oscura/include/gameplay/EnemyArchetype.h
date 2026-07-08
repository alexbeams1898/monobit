#pragma once

#include "anim/SkeletonJointMap.h"
#include "combat/HurtboxDecl.h"
#include "ecs/Items.h"
#include "gameplay/Faction.h"
#include "gameplay/Perception.h"

#include <nlohmann/json_fwd.hpp>

#include <cstdint>
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
    // Optional designer override of the action's effective reach
    // (meters). 0 = no override; the runtime-resolved reach computed
    // from the clip + joint trajectory + hitbox geometry drives BT
    // gating. Non-zero = force this number even if the clip would
    // compute differently. Rarely needed; intended for big-boss
    // actions where the designer wants forced-fire-distance for
    // pacing reasons. See [[feedback_action_range_max_is_chase_stop_range]]
    // for why this is a single number (BT chase-stop AND fire-gate
    // resolve to the same value).
    float effective_reach_override = 0.0f;
    // Runtime-computed reach in meters. Populated by
    // resolveActionReach() at archetype-load time from the action's
    // clip + hitbox_joint trajectory during the active window +
    // hitbox_radius + hitbox_tip_offset_z. Zero means "not yet
    // resolved" -- consumers fall back to effective_reach_override,
    // then to a safe default. Not serialized to JSON (it's derived).
    float resolved_effective_reach = 0.0f;
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
    // Per-action clip playback rate. 1.0 = author's authored cadence
    // (default). 2.0 = clip plays at 2x speed (1s clip becomes 0.5s
    // wall-time). Use when the source clip was authored at a tempo
    // that doesn't fit the enemy's combat feel (e.g. stock zombie
    // clips are at a deliberately-slow shamble, but feral larvae need
    // a snappier swing).
    //
    // SEMANTIC NOTE: windup_seconds and active_seconds are in
    // CLIP-AUTHORED time, not wall-time. The runtime divides them by
    // playback_rate when scheduling fire_at_time + lifetime. This
    // keeps windup/active values stable when playback_rate is tuned
    // (a strike at clip-time 1.0s stays at windup=0.85 regardless of
    // playback_rate). Reach computation uses the same clip-time
    // window because reach is a property of the clip's authored
    // joint geometry, not wallclock.
    float playback_rate = 1.0f;

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
// NOLINTNEXTLINE(clang-analyzer-optin.performance.Padding)
// Field order matches the authoring JSON schema and the bestiary doc
// (lifecycle / combat / cosmology groupings). Reordering for tight
// packing would scatter related fields and break the doc-mirrored
// layout; the runtime cost (~40 bytes per archetype, of which we
// have <100) is negligible.
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
    // Human-readable label for the interaction prompt and dialog UI.
    // Distinct from boss_name (which is uppercase / dramatic for the
    // boss-HP overlay). NPCs without a boss role still need a name
    // for "Talk to {display_name}". Empty falls back to the spawn id.
    std::string display_name;
    // Language-map key for the player-facing NPC name. When set,
    // wins over display_name in the Talk-interactable prompt. Per
    // the insight system: the player may know the NPC by a different
    // word as their understanding grows. The Guide is just "Guide"
    // at every tier; future NPCs (the keepers) may reveal proper
    // names at tier 2.
    std::string display_name_key;
    // Non-empty = this archetype is examinable. The spawn-flow system
    // registers an Examine-kind interactable on spawn; pressing E
    // shows this text in an examine-dialog (Grimoire register: Hell's
    // third-person voice describing the being, not the being itself
    // speaking -- larvae and similar are mute). Empty (default) = not
    // examinable; spawn-flow skips interactable registration.
    //
    // Used for environmental flavor on non-dialog beings (fresh
    // larvae, future ambient observables). NPCs that have a real
    // dialog tree should use the dialog system instead (is_npc=true).
    std::string examine_text;
    // Language-map keys for the examinable's prompt label and prose
    // body. Tier-gated through the insight system (same pattern as
    // static-mesh examines in region.json). When set, win over the
    // literal examine_text field. Empty = use literal examine_text
    // as fallback.
    std::string examine_label_key;
    std::string examine_text_key;
    // Interaction radius (meters) for the Talk or Examine prompt this
    // archetype's spawn registers. Reaches both kinds because no
    // archetype today opts into both (NPC -> Talk, examinable mob ->
    // Examine). 0 = use default (2.5 for Talk, 2.0 for Examine -- the
    // numbers that shipped before this field landed). Designer-tunable
    // per archetype: bosses can broadcast prompts further; ambient
    // mobs with examine_text can pull in tighter.
    float interact_range_meters = 0.0f;
    // Story-gated talk availability. If non-empty, the NPC's Talk
    // prompt only appears when this flag is set on the active profile.
    // Used for NPCs whose interactability is driven by a scripted
    // sequence -- e.g. the Guide is silent inside the chapel until the
    // post-Lupa rescue scene completes (talk_requires_flag =
    // "signing_committed"). Default empty = always talkable (existing
    // behavior).
    std::string talk_requires_flag;
    // Yaw-acknowledgment range (meters). If non-zero, this actor
    // turns its yaw to face the player whenever the player is within
    // this XZ range AND the actor is in a passive state (not in
    // active combat, not on a scripted-walk leg). Souls-style "the
    // NPC notices you walking by" behavior. 0 = disabled (default).
    // Per-archetype because only named NPCs (Guide, future
    // merchants/companions) should do this; ambient mobs ignore the
    // player until they aggro.
    float face_player_range_meters = 0.0f;
    // Maximum angular deviation from spawn_yaw the acknowledgment
    // overlay will turn to. Caps the turn so the NPC reads as
    // glancing at the player rather than tracking like a camera --
    // a person can't physically turn past their own shoulders. Past
    // this angle the NPC simply holds at the cap (or returns to
    // spawn_yaw if the player moves out of the human-natural
    // viewing arc). Default 0.6 rad (~35deg) -- a head-turn, not a
    // body-turn. 0 = no cap (overlay tracks freely).
    float acknowledgment_max_angle_radians = 0.6f;
    // Turn-rate multiplier for the acknowledgment overlay. The base
    // turn rate is tun.ai_turn_rate_radians_per_sec (6.0 rad/s ~=
    // combat snap); multiplying by 0.15 yields ~0.9 rad/s ~= 50deg/s,
    // a slow attentive head-turn rather than a snap. Applied only
    // when the overlay drove turn_intent_yaw this frame; combat and
    // scripted-walk yaw updates use the full rate.
    float acknowledgment_turn_rate_scale = 0.15f;
    // Which behavior tree drives this archetype's decisions. Tree
    // construction is in code (see BehaviorTree.cpp's tree-builder
    // registry); JSON just names which one to bind. Defaults to
    // "humanoid_basic" — covers every humanoid in the bestiary
    // until a tree-specific behavior demands its own builder.
    std::string tree_id = "humanoid_basic";
    // Skeleton key (matches SkeletalAssets registry: "humanoid_male"
    // for the default humanoid Body Type 1; "humanoid_female" for
    // Body Type 2; "wolf" / etc. for distinct skeletons). Default is
    // the player skeleton key so any humanoid archetype that doesn't
    // override gets a working rig.
    std::string skeleton_id = "humanoid_male";

    // Optional per-archetype mesh override (FromSoft-pattern:
    // every humanoid enemy archetype has its own baked .glb with its
    // own skin diffuse -- leached-pale Foundling, sangue-darkened
    // Gorged Foundling, etc. All humanoid archetypes SHARE the skeleton_id
    // bundle's clip library for animation.) Empty => use the shared
    // bundle's default mesh. Load path is resolved via
    // selva::anim::meshByArchetypePath() at draw time.
    std::string mesh_path;

    // Optional multi-variant mesh pool (FromSoft-pattern: one
    // archetype conceptually maps to N pre-baked meshes -- e.g.
    // male + female larvae -- and each spawned actor picks one at
    // random). When non-empty, OVERRIDES mesh_path; the spawn code
    // rolls a uniform random index and assigns the picked path to
    // Actor::mesh_path. Two typical variants (male, female) but the
    // list is unbounded so future ethnicity/skin-tone variants slot
    // in without schema changes. Use the flat mesh_path field for
    // archetypes with a single canonical baked mesh.
    std::vector<std::string> mesh_path_variants;

    // Per-actor face-morph randomization flag. When true, spawn code
    // rolls random values from a hardcoded range table (see
    // selva::gameplay::rollRandomFaceMorphs) and writes them into
    // Actor::appearance.morph_weights so no two spawned actors of
    // this archetype look identical. Off by default -- most player-
    // creator characters + boss actors want their exact-configured
    // face. Enemy archetypes that spawn in crowds (Foundlings, future
    // trash zombies) opt in via "random_face_morphs": true in JSON.
    bool random_face_morphs = false;

    // AuthoredCharacter config path. Empty = default character
    // (default appearance + no identity overlay -- same as existing
    // behavior, so legacy archetypes with no character_path stay
    // identical). Set per-archetype to make every instance of this
    // enemy share a body shape -- e.g. a Foundling file with
    // body_scale=0.85 for a slightly smaller Foundling, or a keeper file
    // with 1.4 for a hulking keeper. Named characters (Guide,
    // Beatrice, bosses) additionally author identity keys
    // (display_name_key, player_class, stats, rh_item, lh_item) that
    // overlay onto the spawned Actor when the corresponding has_*
    // flag is set in the file. spawnEnemyFromDecl loads this once at
    // spawn; every Actor consumer (renderer, buildActorModelMatrix,
    // applyActorClipHipDelta) reads through the actor as it does for
    // the player.
    std::string character_path;

    // Per-archetype clip names. Empty = humanoid default (the standard
    // clip set on the legacy rig). Wolf overrides every entry. Read
    // EXCLUSIVELY via lookupArchetypeClip in Enemies.cpp so the per-
    // skeleton registry is always honored -- the wolf's sampler must
    // never receive a humanoid clip (skel.num_joints != anim.num_tracks
    // -> ozz garbage -> IsNormalizedEst assert).
    std::string idle_clip;             // empty -> "standard_idle"
    std::string combat_idle_clip;      // empty -> "unarmed_combat_idle"
    std::string walk_clip;             // empty -> "walking"
    std::string walk_back_clip;        // empty -> "walking_backward"
    std::string strafe_left_clip;      // empty -> "strafe_walking_left"
    std::string strafe_right_clip;     // empty -> "strafe_walking_right"
    std::string death_clip;            // empty -> "death"
    std::string knockdown_clip;        // empty -> "stunned"
    std::string flinch_front_clip;     // empty -> "flinch_front"
    std::string flinch_back_clip;      // empty -> "flinch_back"
    std::string flinch_left_clip;      // empty -> "flinch_left"
    std::string flinch_right_clip;     // empty -> "flinch_right"
    std::string hit_react_medium_clip; // empty -> "hit_react_medium"
    std::string hit_react_heavy_clip;  // empty -> "hit_react_heavy"
    std::string run_clip;              // empty -> "jogging" (humanoid fast-gait)

    // Aggro / wake-up clip. Fired once when this actor's perception
    // transitions Suspicious -> Alerted (the "confirmed sighting"
    // moment per Awareness comment in perception.h). Matches the
    // Souls/ER pattern: Hollows wake from slumped idle, knights raise
    // weapon, Foundlings scream as the imprint finds outlet. Movement is
    // locked for the clip's full duration (action_locks_movement set
    // alongside the playOneShot); the BT's chase + attack starts after
    // the clip finishes. Empty = no aggro clip; the actor goes
    // directly from Alerted into normal combat AI (existing behavior
    // for archetypes that haven't been authored an aggro animation).
    //
    // This is distinct from engage_clip (Pattern B boss state-machine
    // transition initial_state -> combat-ready). aggro_clip is
    // perception-driven and applies to any actor; engage_clip is
    // boss-trigger-driven and only fires on Pattern B bosses.
    std::string aggro_clip;

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

    // Sangue granted to the player's vessel + lifetime ledger when the
    // player kills an actor of this archetype. Default 0 (no grant).
    // Per [[project_imprint_handle_required_for_sangue]] trash kills
    // produce barely any collectible substance; keeper-fall events are
    // where playable amounts arrive. Tuned per archetype in JSON.
    std::uint32_t sangue_drop = 0u;

    // Loot drops rolled when the player kills an actor of this archetype.
    // Distinct from sangue: sangue is the Hell-substance landing in the
    // Vagrant's vessel; loot is physical material / consumables / rare
    // flavor items dropped into inventory. Per
    // [[project_items_loot_doctrine_locked]]: weapons NEVER drop from
    // enemies (the damned do not bear arms); enemy loot is materials +
    // rare flavor items only. Each entry rolls independently per kill:
    // effective_chance = min(1.0, base_chance * (1 + LCK * drop_scale / 100)).
    // Quantity is uniformly rolled in [min_qty, max_qty]. Resolved by
    // engine::ecs::DropEntry shape (config_path string keys the engine
    // ItemRegistry).
    std::vector<engine::ecs::DropEntry> loot_drops;

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
    // True -> this archetype has NO hurtboxes regardless of empty
    // hurtbox_decls. Spawn-side code skips both the explicit and
    // inherited hurtbox paths. Used for beings that are intentionally
    // not killable: Foundlings (substance too tightly arranged for
    // the Vagrant's second-death-grant per project_soul_larvae_cosmology),
    // future intact NPCs, decoration-tier entities.
    bool disable_hurtboxes = false;

    // Cosmological "insubstantial" flag. True -> spawned actors live
    // in the physics INCORPOREAL layer: they still stand on terrain
    // (gravity + ground snap intact) and hazards still catch them,
    // but the player and other actors walk through them and they walk
    // through each other. Used for fresh larvae (soul-substance too
    // loose to displace flesh; the Vagrant passes through the pile
    // per project_soul_larvae_cosmology), future incorporeal NPCs.
    // Distinct from disable_hurtboxes: an actor can be intangible and
    // still take damage (a soul-form the player hits with a weapon),
    // and can be tangible while unkillable (a Guide NPC).
    bool intangible = false;

    // Hazard kinds this archetype's actors avoid. Tags match the
    // `kind` field of selva::hazard::HazardZone instances declared
    // in region JSON's `hazard_zones`. Actors will not chase a
    // target into a zone with a matching kind; their locomotion
    // velocity clamps at the boundary. Empty -> avoids no hazards
    // (default; the Vagrant + unjudged souls don't avoid anything;
    // damned souls of every circle should include "acheron" since
    // the river dissolves them per the substance law). See
    // [[project_soul_larvae_cosmology]] river-dissolves-on-contact
    // + selva/hazard/HazardZones.h.
    std::vector<std::string> avoids_hazards;

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
    // Language-map key for the boss HP-bar name. Tier-gated through
    // the insight system; tier-0 is "???" until the player gains the
    // insight that knows this boss (typically fires on death). Empty
    // = fall back to literal boss_name.
    std::string boss_name_key;

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
    // Language-map key for the felled overlay. Tier-gated. When the
    // player doesn't yet know the boss's name (insight unlock fires
    // on death of THIS boss; the felled overlay is shown for ~3s
    // after death, so the message you see depends on the same node
    // that gates the HP bar -- per the locked design "same node
    // gates both"). Empty = fall back to felled_message.
    std::string felled_message_key;

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

    // Optional clip to bind as the LOCO track at spawn, instead of
    // idle_clip. Use when the actor's spawn pose differs from its
    // standing idle -- e.g. foundling spawns prone in zombie_crawl,
    // not standing in zombie_idle. Empty (default) = bind idle_clip
    // at spawn (legacy behavior).
    //
    // Resolved via the skeleton's clip registry (clipsByKey(skeleton_id))
    // so authors give a clip-name string here.
    //
    // Bound as the LOCO track only; the per-frame gait picker
    // (pickEnemyLocomotionClip) takes over on the first tick after
    // spawn. If the picked clip differs (e.g. spawn_clip was
    // zombie_crawl but the actor stands still on its first tick and
    // the picker selects idle_clip), the sampler blends between them
    // via its standard transition. For Foundlings where spawn_clip and
    // walk_clip are the same (both zombie_crawl), the transition is
    // invisible.
    std::string spawn_clip;

    // Optional freeze-at-frame for spawn_clip, mirroring the
    // initial_freeze_at_seconds pattern but on the spawn-clip track.
    // 0 (default) = play spawn_clip normally (the actor's first
    // frame is the clip's first frame, then it advances).
    // > 0 = play as a freeze-held one-shot at this clip time, so the
    // actor holds a specific pose-frame at spawn. Used when the
    // spawn pose is a specific moment of the clip rather than its
    // first frame.
    float spawn_clip_freeze_at_seconds = 0.0f;

    // Optional archetype id whose Appearance this actor LERPS TOWARD
    // over its arrival-wait period. Used by render code to visualize
    // a transformation in progress -- today the Foundling burn (fresh
    // pale-and-small -> Gorged Foundling red-and-full-sized as they
    // wait at the shore), tomorrow keeper falls / class form changes / etc.
    // Empty (default) = no transformation; render uses this
    // archetype's appearance verbatim. The lerp uses the actor's
    // arrival_wallclock + arrival_action_delay_seconds, both stamped
    // by the spawn-flow on arrival. resolveAppearance interpolates
    // every appearance axis (color, body_scale, all future fields)
    // simultaneously from the same progress fraction.
    std::string transform_target_archetype;
};

// nlohmann JSON I/O for these structs. Defined in EnemyArchetype.cpp
// to keep this header from pulling the full json.hpp.
void to_json(nlohmann::json& j, const EnemyAction& a);
void from_json(const nlohmann::json& j, EnemyAction& a);
void to_json(nlohmann::json& j, const EnemyArchetype& a);
void from_json(const nlohmann::json& j, EnemyArchetype& a);

// Process-wide archetype registry. Loaded once at startup; read by
// gameplay code via archetypes(). Sprint 3 shipped with "limbo_shade";
// as of 2026-07-07 the registry also holds foundling, gorged_foundling,
// gorged_foundling_feeder plus the Wood entries. Scales without bound.
class EnemyArchetypeRegistry
{
  public:
    // Load every *.json in `dir` as an archetype. Files whose JSON
    // parse fails are logged and skipped — never throws.
    void loadDirectory(const std::filesystem::path& dir);

    // Walk every archetype's actions and populate
    // EnemyAction.resolved_effective_reach from clip geometry.
    // Must be called AFTER both loadDirectory() and the per-skeleton
    // clip registries are populated (initSkeletalAssets at boot).
    // Logs a [reach] line per action with its computed value so
    // archetype tuning is observable in combat-debug.log.
    //
    // Safe to call more than once; recomputes from scratch each
    // time (used by hot-reload paths if/when those land).
    void resolveAllActionReach();

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
