#pragma once

#include "anim/PoseSampler.h"
#include "anim/SkeletonJointMap.h"
#include "combat/HurtboxDecl.h"
#include "gameplay/BossState.h"
#include "gameplay/Faction.h"
#include "gameplay/Perception.h"
#include "physics/PhysicsWorld.h"

#include <glm/vec2.hpp>
#include <glm/vec3.hpp>

#include <cstdint>
#include <limits>
#include <random>
#include <string>
#include <unordered_map>
#include <vector>

namespace selva::gameplay
{

// Unified actor model — the PC/NPC symmetry rule, made concrete in
// code, not just schema. Both the player and enemies are instances
// of the same `Actor` struct in a single pool. Every system that
// touches actors (animation tick, hip-delta apply, collision,
// hurtbox build, damage application, hit reactions) iterates the
// pool once and acts on every actor symmetrically.
//
// Behavior differs by `Controller` tag — Input for the player, AI
// for enemies. Systems below the controller layer (sampler update,
// hip-delta apply, collision, etc.) don't care which controller
// drives the actor; they're actor-agnostic by construction.
//
// See docs/design/pc-vs-npc.md for the design rule and
// docs/design/bestiary.md for its bestiary-side consequence
// (figura umana — one rig, deformed by sin).

// Faction enum + factionsHostile() rule live in gameplay/Faction.h
// (included above) so they can be referenced from EnemyArchetype.h
// without pulling the full Actor.h.

// Mortal pool. Current drops when damaged; max is derived from
// Body::base_hp + FormulaConfig::hp scaling. HP <= 0 marks the actor for
// death cleanup.
struct Health
{
    int current = 100;
    int max = 100;
};

// Action pool. Drains on attack / dodge / sprint / jump; regenerates while
// not committing to actions.
//
// recovery_timer counts down from FormulaConfig::stamina::recovery_delay any
// time stamina is spent. Regen runs only when the timer hits 0. Driving regen
// off a countdown (not a wallclock comparison) keeps the system tight: every
// spend resets it; no other system needs to know "when did spending last
// happen?"
//
// sprint_locked is the post-exhaustion lockout. Set true when a sprint frame
// drains the bar to 0. While locked, sprint input is ignored. Lock clears
// only when stamina recovers to full. This is the standard mechanism that
// makes holding WASD+Space at empty stamina silently walk while the bar
// refills, instead of repeatedly re-stamping the recovery timer.
struct Stamina
{
    float current = 100.0f;
    float max = 100.0f;
    float recovery_timer = 0.0f;
    bool sprint_locked = false;
};

// Stagger reservoir. Souls-convention: starts at max, drains by
// per-attack poise_damage on each hit. When current hits 0, the
// hit triggers a knockdown chain (knockdown clip → getting_up clip)
// and poise resets to max. While not taking hits, poise refills
// (full refill after poise_decay_window_seconds of no damage).
//
// `current` is float for the same reason Stamina::current is -- continuous
// per-frame refill at sub-1 units/frame would quantize to 60x intended rate
// as int.
struct Poise
{
    float current = 100.0f;
    float max = 100.0f;
    // Wallclock time of the last poise-damage event. Drives the
    // decay-window refill timer. -1 = never hit.
    float last_damage_time = -1.0f;
};

// Stats quad. Matches engine::ecs::Stats exactly so formulas loaded from
// FormulaConfig compose without translation.
struct Stats
{
    int str = 1; // Strength:  scales STR-weapon damage, gates heavy weapons
    int dex = 1; // Dexterity: scales DEX-weapon damage, gates light weapons
    int end = 1; // Endurance: scales max stamina + equip load + max HP
    int lck = 1; // Luck:      drop rate, quality rolls
    // Mind-section stats per cognition-system v1. Universal across
    // all classes including Unburdened (these grow from cognitive
    // engagement, not sangue installation). All start at 1.
    int per = 1;  // Perception:   grows from observations. Affects combat
                  //               reading, examine depth, NPC subtext.
    int cog = 1;  // Cognition:    grows from inferences (any). Drives
                  //               Mind pool max + regen.
    int intl = 1; // Intelligence: grows from warranted inferences.
                  //               Scales ability potency; gates reveals.
};

// Per-archetype "what species are you" properties. Not leveled.
// Distinguishes a hulking glutton-shade from a gaunt heretic-shade
// before per-instance stats stack on top. For the player today this
// holds the same humanoid defaults; tomorrow's class system layers
// onto Stats, not Body.
// Orientation of the body's collider capsule.
//   Vertical -- capsule axis is world-Y. Standard humanoid (X_Bot,
//     limbo shades). collider_length is ignored; the capsule height
//     is implicitly the actor's standing height.
//   AlongYaw -- capsule axis is the actor's facing direction
//     (rotates with yaw, parallel to ground). Used for quadrupeds
//     (wolf, lion, etc.) whose body is horizontal nose-to-tail.
//     collider_length is the capsule length along the spine; the
//     two end-caps land at p0 = pos - 0.5*length*forward and
//     p1 = pos + 0.5*length*forward.
enum class CapsuleAxis : std::uint8_t
{
    Vertical,
    AlongYaw,
};

struct Body
{
    int base_hp = 50;
    int base_stamina = 80;
    int base_poise = 30;
    int base_defense = 0; // flat damage reduction; clamped to >= 1 incoming
    float unarmed_damage = 6.0f;
    float unarmed_poise_damage = 8.0f; // baseline poise damage per fist hit
    float collider_radius = 0.35f;     // capsule radius (both axis modes)
    // Capsule axis. Default Vertical preserves humanoid behavior
    // (every existing actor is humanoid); AlongYaw used by quadrupeds.
    CapsuleAxis collider_axis = CapsuleAxis::Vertical;
    // Capsule length along the chosen axis. Only consulted when
    // collider_axis == AlongYaw. For Vertical, the capsule height
    // is implicit from the actor's standing pose.
    float collider_length = 0.0f;
    // Capsule total height (cylinder + caps) for Jolt character body
    // creation. Vertical-axis actors use this directly. AlongYaw
    // actors (quadrupeds) use a vertical-capsule approximation of
    // their bounds for Jolt purposes -- this is the approximated
    // height. Default 1.8m matches typical humanoid; wolf overrides
    // to ~1.2m for the approximation.
    float collider_height = 1.8f;
    // Per-actor hurtbox layout. Authored per skeleton (player from
    // config/skeletons/player_hurtboxes.json; enemies from their
    // archetype JSON's hurtboxes array). Empty = no hurtboxes
    // (actor takes no hits anywhere; intentional or
    // misconfiguration).
    std::vector<selva::combat::HurtboxDecl> hurtbox_decls;
};

// Derived max HP / stamina / poise from Body + Stats. Linear scaling for v1;
// coefficients live in engine::ecs::FormulaConfig (loaded by
// selva::formulas::current() from config/balance/formulas.json). Souls-style
// diminishing curves replace these when balance work begins; call sites
// don't change.
int computeMaxHp(const Body& body, const Stats& stats);
float computeMaxStamina(const Body& body, const Stats& stats);
float computeMaxPoise(const Body& body, const Stats& stats);

// Initialize the actor's mortal + action + stagger pools to full
// from the archetype Body + Stats. Call once at spawn; thereafter
// `current` changes through gameplay (damage, regen) while `max`
// stays put until stats change (level-up later).
void initActorPools(Health& hp, Stamina& stamina, Poise& poise, const Body& body,
                    const Stats& stats);

// Apply per-Form defaults to Body + Stats before initActorPools runs.
// Called at spawn time; archetype-level overrides (max_hp_override,
// etc.) layer on top of the form-defaults. Form is the cosmological-
// category axis ([[gameplay/Faction.h]] + bestiary.md
// *Soul-form vs animal-form*); each form has a baseline body shape:
//
//   UnjudgedSoul    -- Vagrant, Guide. Low HP, low poise. Fragile.
//   DamnedSoul      -- shades. Mid HP/poise. Sin-defined.
//   Animal          -- Lupa, legends. High HP/poise/raw damage.
//   HellMachinery   -- keepers. Legendary HP/poise. (Reserved.)
//   Divine          -- Beatrice. Boss-class. (Reserved.)
//
// The defaults are starting points; archetypes still author their
// own per-instance feel. Function is idempotent and overrides the
// passed Body/Stats with form-baseline values; call BEFORE archetype
// per-field overrides so the overrides win.
void applyFormDefaults(Body& body, Stats& stats, Form form);

// Per-frame stamina tick:
//   1. If sprint_locked: demote loco_tier Sprint -> Jog (sprint input
//      ignored until lock releases at full stamina).
//   2. If loco_tier == Sprint + stamina available: drain by sprint_effort * dt
//      and arm the recovery timer. If the drain hits 0: set sprint_locked.
//   3. Else if recovery_timer > 0: decrement (no regen yet).
//   4. Else if current < max: regen at recovery_rate * dt.
//   5. If sprint_locked AND current >= max: clear the lock.
void tickActorStamina(Actor& a, float dt);

// Predicate: does the actor have enough stamina to pay `cost`? Call this at
// every action-fire site BEFORE the action fires (attacks, dodge, jump).
// Returns true for cost <= 0 (free actions).
bool canSpendStamina(const Actor& a, float cost);

// Deduct `cost` from stamina and arm the recovery timer. Caller is
// responsible for checking canSpendStamina first; this just subtracts.
// Clamps current at 0.
void spendStamina(Actor& a, float cost);

// Apply raw incoming damage to `hp`, mediated by `body.base_defense`.
// Damage is clamped to at least 1 so even heavily-armored targets
// take a chip on every hit. Health::current floors at 0 — death is
// detected separately by a caller checking current <= 0.
void applyDamage(Health& hp, const Body& body, int raw_damage);

// Pure damage computation for an attack. base / str_scale / dex_scale
// come from the weapon (or unarmed profile on the attacker's Body).
// Returns an integer damage value to feed into applyDamage().
//
// Linear scaling for v1; diminishing-returns curve replaces this when
// balance work begins -- the call site doesn't change, only the body.
int computeAttackDamage(const Stats& attacker, float base, float str_scale, float dex_scale);

// What kind of intent driver an actor uses. Each per-frame tick
// reads `actor.controller` and dispatches to the correct intent
// source (input + camera for Input, AI for AI tags, none for
// Corpse). All systems below the dispatch layer are controller-
// agnostic.
enum class Controller : std::uint8_t
{
    Input,         // the PC; intent from keyboard / mouse
    AI_Stationary, // placeholder enemies: idle, no AI yet
    Corpse,        // dead actor; no intent, holds death pose
};

// Three-tier locomotion: walk (LAlt-held), jog (default WASD), sprint
// (Space-held). Backward + strafe sprint clips don't exist; those
// demote to the jog family.
enum class LocoTier
{
    Walk,
    Jog,
    Sprint,
};

// One actor. Player + every enemy is an Actor instance in the
// shared pool. The Controller tag is the ONLY thing that
// differentiates them at the per-frame system level — schemas,
// animation, collision, combat are all identical.
struct Actor
{
    // --- Schema (same shape on player + enemy) ---
    Health hp;
    Stamina stamina;
    Poise poise;
    Stats stats;
    Body body;
    Faction faction = Faction::Hostile;

    // --- Transform / motion ---
    glm::vec3 pos = glm::vec3(0.0f);
    float yaw = 0.0f;
    glm::vec2 velocity_xz = glm::vec2(0.0f);
    // Vertical velocity for gravity / falling. Negative = falling.
    // Snaps to 0 when the actor lands on ground.
    float velocity_y = 0.0f;

    // --- Animation ---
    // Per-actor pose state. Each actor's clips advance in their
    // own sampler, blended against the shared skeleton + mesh.
    selva::anim::PoseSampler sampler;

    // --- Controller / behavior ---
    Controller controller = Controller::AI_Stationary;

    // --- Per-controller intent flags ---
    // Three-tier locomotion intent (Input controller only): Walk
    // (LAlt-held), Jog (default), Sprint (Space-held). Walk and
    // Sprint are mutually exclusive at input-edge level so this
    // single field never carries a contradiction.
    LocoTier loco_tier = LocoTier::Jog;

    // Lock-on target index into actors() (Input controller only).
    // -1 = unlocked; >=0 = combat mode: yaw snaps to target each frame,
    // WASD becomes target-relative, camera derives from player↔target
    // axis, idle picks unarmed_combat_idle, locomotion picks 1 of 4
    // directional combat clips. Cleared by middle-mouse toggle, by
    // sprint engaging, or when the target dies. Index-based (not
    // pointer) so it survives push_back reallocations during enemy
    // spawn — actors() is a std::vector and pointers into it are not
    // stable across grow.
    int lock_target_idx = -1;

    // Which lockon point on the locked target is active. Index into
    // actorLockOnPoints(target). Reset to the default-flagged point
    // when lock_target_idx changes; cycled by the lockon-cycle input
    // (mouse wheel). -1 if no target locked. Per-archetype point lists
    // live on EnemyArchetype.lockon_points; the skeleton-default list
    // lives on SkeletonJointMap.default_lockon_points. Most actors
    // have a single point ("chest") and never cycle; bosses like
    // Lupa have multiple (head/torso/hindleg_*).
    int lock_point_idx = 0;

    // Circle-strafe direction for AI duel mode. 0 = no committed
    // side (set on lock acquire to ±1 via actor.rng). +1 = strafe to
    // the target's right (player's left); -1 = strafe to the target's
    // left. Sticky for the duration of the engagement so the AI
    // doesn't flip sides every frame. Player ignores this field.
    int duel_strafe_dir = 0;

    // --- Scripted-event state ---
    // World position the actor should walk toward, driven by a Scene
    // or scripted event (e.g. the Guide walking out of the chapel
    // toward the player during the Lupa-rescue Scene). NaN sentinel
    // (all components = NaN) means "no scripted target set"; non-NaN
    // means LeafFollowScriptedTarget activates and writes intent
    // toward this position. Cleared by the leaf when within
    // scripted_stop_range OR by the Scene ending.
    glm::vec3 scripted_target_pos{
        std::numeric_limits<float>::quiet_NaN(),
        std::numeric_limits<float>::quiet_NaN(),
        std::numeric_limits<float>::quiet_NaN(),
    };
    float scripted_stop_range = 2.0f; // meters; default = standard NPC approach distance
    // Ordered queue of REMAINING waypoints AFTER the current
    // scripted_target_pos -- next-up first, final destination last.
    // When the actor reaches scripted_target_pos,
    // LeafFollowScriptedTarget pops the front of this list into
    // scripted_target_pos and keeps walking; only when both
    // scripted_target_pos has been reached AND this queue is empty
    // does the leaf clear to the NaN sentinel and fire the
    // arrival event (spawn-flow's on_arrival_action).
    //
    // Together with scripted_target_pos this forms a (current, rest)
    // walk queue. Decl-side (EnemySpawnDecl) authors the inverse
    // shape -- a `scripted_path_waypoints` list of INTERMEDIATES plus
    // a separate `scripted_target_pos` DESTINATION -- which
    // spawnEnemyFromDecl flattens into this runtime shape.
    std::vector<glm::vec3> scripted_path_waypoints;
    // True if this actor was spawned with a scripted_target_pos
    // authored on its spawn decl. Used by arrival-detecting systems
    // (e.g. the larva conversion trigger) to distinguish "never had
    // a target" (persistent NaN -> ignore) from "had one and arrived"
    // (was non-NaN, leaf cleared to NaN -> fire arrival event).
    bool had_scripted_target = false;
    // Wallclock stamp of when this actor arrived at its scripted
    // target. -1 = hasn't arrived yet. Set by the spawn-flow system
    // (selva::spawn::FlowSpawner) on the frame the arrival sentinel
    // fires. Read by the same system to gate delayed on_arrival
    // actions (e.g. "convert this fresh larva to aged after 30s of
    // standing at the shore"). Per [[project_soul_larvae_cosmology]].
    float arrival_wallclock = -1.0f;
    // Set once the delayed on_arrival action has fired. Prevents
    // re-firing on subsequent ticks. Cleared on archetype swap (the
    // new archetype is its own actor lifecycle, gets fresh arrival
    // state if it ever scripts another target).
    bool arrival_action_fired = false;
    // The on_arrival_delay duration copied from the spawn flow that
    // spawned this actor. Used by render-side systems that want to
    // visualize the burn-progress (e.g. fresh larvae tint from white
    // to red as they wait to turn aged). -1 = no delay info; render
    // uses static tint only. Stamped by FlowSpawner at spawn time;
    // unchanged across the actor's lifetime.
    float arrival_action_delay_seconds = -1.0f;

    // Hazard-zone entry tracking. Toggled by tickEnemyLocomotion's
    // hazard-violation check; used to edge-trigger the
    // [hazard-violation] debug log so it fires once on entry/exit
    // instead of spamming per-frame while the actor sits in the zone.
    bool was_in_avoided_hazard = false;

    // The spawn flow that produced this actor, or empty if the actor
    // was spawned by non-flow code (region JSON, scripted event, hand).
    // Gates FlowSpawner::tickArrivals so arrival logic only runs for
    // actors that the flow itself spawned. Without this, two flows
    // sharing an archetype id would both try to run arrival actions
    // on each other's actors, AND a hand-spawned actor with the same
    // archetype would get its movement intent munged by a flow it
    // never enrolled in. Stamped by FlowSpawner immediately post-spawn.
    std::string spawning_flow_id;

    // For flow-spawned actors with a successor pattern (e.g. soul larvae:
    // a fresh trickle-spawned to replace an aged eats the aged's corpse
    // during its arrival/conversion window). spawn_decl_id of the corpse
    // this actor was paired with at trickle-spawn time, popped from the
    // flow's pending_corpses FIFO. On conversion, consumePairedCorpse
    // rewinds that specific corpse's death_time so the right body fades.
    // Empty for actors with no corpse to consume (initial population,
    // first-cycle trickle before any deaths). Per
    // [[project_soul_larvae_cosmology]] feeding-as-conversion doctrine.
    std::string feeding_on_actor_id;

    // Per-actor override for the locomotion-track clip pick. When
    // non-empty, pickEnemyLocomotionClip returns this clip key
    // regardless of speed/awareness — used when the actor must visibly
    // do something specific that the normal gait picker wouldn't
    // produce (a fresh larva crouched over a corpse, eating). Cleared
    // by applyArchetypeSwap so a converted actor reverts to standard
    // gait picking. Resolves via the actor's per-skeleton clip
    // registry, same as the archetype-clip funnel.
    std::string idle_clip_override;

    // Set by FlowSpawner::tickPostDeathQueueing once this actor's
    // spawn_decl_id has been pushed onto its flow's pending_corpses
    // FIFO. Prevents double-enqueue if the per-frame scan sees the
    // same corpse twice.
    bool flow_corpse_queued = false;

    // Wallclock at which Engaged -> Dying fires. -1 outside scripted
    // death. Set by setBossState(Engaged) when archetype declares
    // scripted_death_seconds > 0.
    float scripted_death_at_wallclock = -1.0f;

    // Wallclock at which the HP-drain curve hits 0. Spans the full
    // (Engaged + Dying) window so the bar drains continuously across
    // the transition. -1 outside scripted death.
    float scripted_death_drain_end_wallclock = -1.0f;

    // Wallclock at which Dying -> Felled fires. -1 outside Dying.
    float dying_until_wallclock = -1.0f;

    // --- Combat reaction state ---
    // Wallclock time of last damage event. Drives in-world HP bar
    // visibility. -1 = never damaged.
    float last_damage_time = -1.0f;
    // Wallclock time of last hit-react fire. Drives the cooldown
    // gate so rapid multi-hits don't restart the animation every
    // frame.
    float last_hit_react_time = -1.0f;

    // --- Death state ---
    // True from the moment the death one-shot fires. Subsequent
    // hits become no-ops; the body holds the death pose. Cleared
    // by the dev respawn timer when it elapses.
    bool is_dead = false;
    float death_time = -1.0f;
    // Per-actor death clip. Enemies use "death" (sword-and-shield
    // fall). The PC overrides to "second_death" (the electrocution-
    // style suffering clip — Hell's killing-protocol firing on him,
    // per setting.md *Second death*). Each actor controls its own
    // visual on death; the firing path reads this field.
    std::string death_clip_name = "death";

    // Per-actor death SFX (registered name in config/audio.json).
    // Plays IMMEDIATELY when fireEnemyDeath fires — the "dread bed"
    // that runs under the death animation. Empty = silent at clip
    // start. Enemies typically empty until per-archetype audio.
    std::string death_sfx_name;

    // Optional peak-aligned death SFX layers. Each is scheduled (not
    // played immediately) so its declared peak_offset_seconds lands
    // at death_time + peak_align_target_seconds — used for the PC's
    // second-death where multiple SFX (synth + soul-steal) must hit
    // together at the moment the card snaps in. Empty = no scheduled
    // layers.
    std::vector<std::string> death_peak_sfx_names;
    float death_peak_align_seconds = 0.0f;

    // --- Knockdown state ---
    // Set true when a hit breaks poise and the knockdown clip fires.
    // The actor is hit-immune while down; the knockdown clip freezes
    // on its last frame; tickActors clears is_knocked_down after
    // `enemy_recovery_after_knockdown_seconds` and the sampler blends
    // back to combat idle in place.
    bool is_knocked_down = false;
    float knockdown_start_time = -1.0f;

    // --- Spawn pose (for respawn) ---
    glm::vec3 spawn_pos = glm::vec3(0.0f);
    float spawn_yaw = 0.0f;

    // --- Region-scoped spawn identity ---
    // Stable identifier authored in region.json's enemy_spawns array,
    // qualified with the owning region_id ("region_id:spawn_id"). Lets
    // save data refer to specific enemy instances ("which shades did
    // the player kill on this cycle?") without relying on pool index
    // (volatile across spawns/respawns) or position (drifts with
    // patrol). Empty for the player and for any actor not authored
    // via region JSON (test/debug spawns).
    std::string spawn_id;

    // Region this actor was spawned in -- its cosmological law-domain.
    // Used by the territory chase-gate (engine/world/Territory.h): an
    // actor drops chase when the player is standing in (or the chase
    // line crosses) a region OTHER than this one. Empty for the player
    // (the player has no domain restriction) and for non-region actors.
    std::string spawn_region_id;

    // Skeleton key (matches SkeletalAssets registry: "player" for
    // humanoids reusing the X_Bot rig; "wolf" / etc. for distinct
    // skeletons). Used by render + matrix-build call sites that need
    // the actor's mesh-specific foot_offset_y. Empty defaults to
    // "player" in selva::anim::meshByKey -- backward-compat for any
    // call site that doesn't set this.
    std::string skeleton_id;

    // If true, this actor stays dead across cycle resets -- the
    // canonical Souls "felled keeper does not respawn" contract per
    // setting.md cycle structure. Shades default false (cycle-flow
    // model: each cycle Hell re-streams souls into their punishment
    // positions). Keepers set true in region.json.
    bool permanent_on_death = false;

    // --- Boss-backend fields (per docs/design/ideas/boss_backend.md) ---

    // Mirrors archetype->is_boss for hot-path lookups (GUI / lockout
    // poll the actor pool every frame; avoiding archetype indirection
    // per-actor per-frame). Set in spawnEnemyFromDecl from archetype.
    bool is_boss = false;

    // Mirrors archetype->is_npc for hot-path lookups (interaction
    // prompt scan walks the pool every frame for nearby NPCs;
    // avoiding archetype indirection per-actor per-frame). Set in
    // spawnEnemyFromDecl from archetype.
    bool is_npc = false;

    // Handle into selva::interact registry for NPCs. Registered at
    // spawn (Talk-kind interactable that opens dialog with this NPC).
    // 0 = not registered. The registry is world-state (closures
    // capture spawn_decl_id, not character data) and survives
    // character switches; no per-cycle reset needed.
    std::uint32_t interactable_id = 0;

    // Mirrors archetype->form (or set explicitly for the player at
    // init -- UnjudgedSoul). Used by combat-permission rules
    // (soul-on-soul forbidden in Wood, etc.) and per-form stat-spread
    // defaults. Default DamnedSoul preserves the legacy shade
    // behavior for unauthored archetypes. Per [[gameplay/Faction.h]]
    // Form enum + [[soul-animal-form-combat-doctrine]].
    Form form = Form::DamnedSoul;

    // Spawn-decl id (Lupa = "lupa"). Used as the persistent identity
    // for save's felled_bosses list -- when this actor dies with
    // is_boss=true, this string is appended to the active profile's
    // felled_bosses. Stable across cycles, unlike pool indexes.
    std::string spawn_decl_id;

    // Post-flag spawn overrides (mirrored from EnemySpawnDecl at
    // spawn time). resetCycleEnemies walks this list and overrides
    // pos/yaw if the active profile has the flag set. Empty = always
    // spawn at base spawn_pos.
    struct FlagPosition
    {
        std::string flag;
        glm::vec3 pos = glm::vec3(0.0f);
        bool pos_y_auto_terrain = false;
        float yaw = 0.0f;
    };
    std::vector<FlagPosition> post_flag_positions;

    // Pattern B (already-there bosses): legacy string state, kept as
    // a MIRROR of boss_state during migration. Empty = combat-ready;
    // non-empty (e.g. "sitting") = pre-engage hold. AI tick reads
    // this string; setBossState() writes it. Future cleanup migrates
    // AI tick to read boss_state directly and drops this field.
    std::string current_boss_state;

    // SINGLE source of truth for the boss lifecycle (per
    // [[gameplay/BossState.h]]). All transitions funnel through
    // selva::gameplay::setBossState(). Default Dormant is fine for
    // non-boss actors (nothing reads it for them). For boss actors
    // (is_boss=true), spawnEnemyFromDecl calls setBossState(Dormant)
    // explicitly to set up mirrors; engage trigger sets Engaged;
    // disengage timer / death set Disengaged / Felled.
    BossState boss_state = BossState::Dormant;

    // --- AI perception state ---
    // Updated by tickPerception each frame. Behavior tree (future)
    // and locomotion-intent (future) read awareness + last-known-
    // player-pos from here.
    PerceptionState perception;

    // --- AI scheduler ---
    // Wallclock time of the next scheduled decision tick. Behavior
    // tree (future) only re-evaluates when wallClock() >= this.
    // Spawned with a phase offset so a wave of actors doesn't all
    // tick on the same frame. See gameplay/AiTick.h.
    float next_ai_tick_time = 0.0f;

    // --- AI archetype binding ---
    // Pointer to the loaded archetype data (action list, perception
    // overrides). nullptr = use defaults (test-dummy fallback).
    // Sprint 4 will read actions[] here to drive the behavior tree.
    const struct EnemyArchetype* archetype = nullptr;

    // --- AI locomotion intent (Sprint 4a) ---
    // Written by the decision tick (tickEnemyDecision) and consumed
    // every frame by tickEnemyLocomotion. Mirrors the player's
    // input-driven moveIntent so the locomotion path stays
    // controller-agnostic.
    glm::vec2 intent_xz = glm::vec2(0.0f); // XZ target direction × speed
    float turn_intent_yaw = 0.0f;          // yaw the actor wants to face
    // True for the frame in which the yaw-acknowledgment overlay
    // (see tickEnemyDecision) drove turn_intent_yaw. Read by
    // stepYawTowardIntent to scale the turn rate down -- ack-driven
    // turns are slow + attentive, combat/scripted-walk turns use the
    // full base rate. Cleared at the start of each decision tick so
    // it accurately reflects THIS frame's intent source.
    bool yaw_intent_from_acknowledgment = false;

    // Per-action runtime state — cooldown timestamps for each
    // archetype-declared action. Lazy: actions are inserted on first
    // lookup (LeafPickAction). Keyed by EnemyAction::id from the
    // archetype JSON. Souls-style cooldowns keep weighted-random
    // selection from spamming the strongest action.
    struct ActionRuntime
    {
        float cooldown_until_time = 0.0f; // wallclock; can fire when now >= this
    };
    std::unordered_map<std::string, ActionRuntime> action_state;

    // Per-actor RNG for weighted-random action picks. Seeded at
    // spawn from std::random_device — different actors of the same
    // archetype roll independently so they don't synchronize.
    std::mt19937 rng;

    // --- Footstep detector state ---
    // Per-foot ground-contact tracker used by tickFootsteps. State
    // machine cycles Airborne <-> Grounded as the foot bone's world Y
    // crosses ground+epsilon. Joint indices are lazily resolved on
    // first tick (cached so we only scan the skeleton once per actor).
    struct FootContact
    {
        int joint_idx = -2; // -2 = unresolved, -1 = absent in skeleton
        float prev_y = 0.0f;
        float prev_vy = 0.0f;         // foot Y velocity last frame (m/s)
        float peak_descent_vy = 0.0f; // max |descent velocity| in current descent (m/s)
        float last_fire_time = -1000.0f;
        bool initialized = false; // skip first frame's bogus velocity
    };
    FootContact foot_left;
    FootContact foot_right;
    // Wallclock of the last fire across EITHER foot. Drives the
    // "first-step rescue" lower threshold so the leading step of a
    // fresh stride from idle isn't suppressed by the steady-state
    // threshold tuned for inter-stride wobble rejection.
    float last_footstep_fire_time = -1000.0f;

    // --- Active attack hitbox tracking ---
    // When the actor fires a swing, this stores the spawned hitbox's
    // id + the bone joint that drives its world position + the tip
    // offset along the joint's forward axis. Per-frame, the
    // tickActiveAttackHitboxes pass re-anchors the hitbox to the
    // joint's current pose so the volume tracks the swinging hand.
    // 0 = no active hitbox. The hitbox-update path is the SAME for
    // PC and NPC — both attacking actors get the same treatment.
    // Without this tracking, the hitbox is frozen at the spawn-
    // frame joint position (usually a windup pose with the hand at
    // the hip) — visually the swing extends through space but the
    // hitbox sits behind the attacker. Bug surfaced when AI enemies
    // started attacking and didn't land their hits.
    std::uint32_t active_attack_hitbox_id = 0;
    int active_attack_joint_idx = -1;
    float active_attack_tip_offset_z = 0.0f;
    // True when the currently-active one-shot was fired by a
    // hard-lock action (EnemyAction.locks_movement=true). Velocity
    // is zeroed for the entire one-shot regardless of cancel_fraction.
    // Cleared automatically by tickEnemyLocomotion when the one-shot
    // ends. Per-action authoring axis: heavy committed swings lock,
    // light tracking jabs don't.
    bool action_locks_movement = false;

    // True after this actor has already fired its aggro_clip once.
    // Prevents the perception-transition hook from re-firing it on
    // subsequent Suspicious->Alerted edges (e.g. after Combat decay
    // back to Alerted then re-Alert). Also used by archetype-conversion
    // events (fresh larva -> aged larva): the conversion event fires
    // the aggro_clip itself and sets this flag so the perception hook
    // doesn't double-fire on the actor's first sighting of the player.
    bool aggro_already_fired = false;

    // Pending (deferred) hitbox spawn for windup-modeled attacks.
    // LeafPickAction queues this on fire; tickPendingAttackSpawns
    // promotes it to a live hitbox when the wallclock crosses
    // fire_at_time (= fire wallclock + action.windup_seconds).
    // fire_at_time < 0 means none pending. Single-slot (at most one
    // outstanding pending swing per actor; a new fireAction overwrites)
    // -- matches the existing single-slot active_attack_hitbox_id
    // model. lifetime stores the active-window length so the spawner
    // can pass it through as remaining_seconds.
    struct PendingAttackSpawn
    {
        std::string joint_name;
        float radius = 0.18f;
        float tip_offset_z = 0.0f;
        int raw_damage = 0;
        int poise_damage = 0;
        float lifetime_seconds = 0.0f;
        float fire_at_time = -1.0f;
    };
    PendingAttackSpawn pending_attack;

    // Jolt kinematic character body. Created at spawn (via
    // selva::world::createCharacterBody), destroyed on death + on
    // pool reset. Every actor in the world has one -- player + every
    // enemy + every NPC -- so the same physics pipeline integrates
    // them all. id=0 (kInvalidBody) means "not yet created" or
    // "destroyed." Per the real-physics doctrine + the universal-
    // actor-physics refactor.
    //
    // Driven each frame: BT or input writes intent_xz, the locomotion
    // tick translates that to velocity, setCharacterVelocity hands
    // it to Jolt, updatePhysics resolves contact + gravity, the
    // post-step pass reads characterPosition back into actor.pos.
    // No per-frame groundHeight snap; physics owns Y.
    engine::physics::BodyHandle character_body{};
};

// Apply the actor's sampler-consumed hip-XZ delta to its world
// position. THE one place this math lives — every caller (player
// path, enemy tick, anywhere else translating an actor by clip-
// authored hip motion) routes through here so the contract is
// unified. `hip_delta_scale` lets the caller scale the applied
// translation (used for the walking-jump variant where authored
// clip travel is scaled to 55% so the same clip covers shorter
// distance at the same playback rate).
// Consume the actor's per-frame authored hip-XZ delta and convert it
// to a velocity contribution on actor.velocity_xz (added on top of
// whatever the locomotion tick wrote). The velocity is what the
// physics step will then integrate via setCharacterVelocity. dt is
// needed because hip-delta is per-frame; velocity is per-second.
//
// Caller decides WHEN to call this (always for AI actors; only
// during one-shots for the Input-controlled player). hip_delta_scale
// is an optional multiplier for one-shot scaling (jumps).
//
// Old behavior (`actor.pos += hip_world * scale`) was direct
// position writes that bypassed physics. The unified-physics
// refactor routes hip motion through Jolt instead.
void applyActorClipHipDelta(Actor& actor, float dt, float hip_delta_scale = 1.0f);

// Resolve `actor.lock_target_idx` to a pointer into actors(), or
// nullptr if unlocked / index stale. Pool can reallocate on enemy
// spawn so callers must re-resolve per-frame rather than caching.
// Used by both PC (lock-on combat mode) and AI (engagement target).
Actor* resolveLockTarget(const Actor& actor);

// Resolved lockon-point list for an actor: archetype's points if it
// authored its own, else the actor's skeleton's default_lockon_points.
// Stable reference for the actor's lifetime. Returns an empty vector
// if neither set (caller checks .empty() before indexing).
const std::vector<selva::anim::LockOnPointDecl>& actorLockOnPoints(const Actor& actor);

// Index into actorLockOnPoints(actor) of the entry whose is_default
// is true; falls back to 0 if none flagged. Returns -1 if the list
// is empty.
int defaultLockOnPointIndex(const Actor& actor);

// Choose a directional locomotion clip given a facing basis and
// movement intent. Shared between PC (lock-on combat mode) and AI
// (engagement strafe). Caller supplies the facing fwd / right
// vectors (already-normalized XZ unit vectors) plus the world-frame
// intent vector. Returns nullptr if intent is effectively zero.
// Strafe wins any nonzero lateral input — diagonals are strafes.
const char* directionalLocoClip(const glm::vec3& fwd, const glm::vec3& right,
                                const glm::vec3& intent, LocoTier tier);

// Reparent the actor's active attack hitbox (if any) to the bone
// joint that drives it, using the current pose. Called per-frame
// for every actor that might have an active swing. Without this
// the hitbox stays frozen at its spawn-frame position (the windup
// pose, hand at hip) and the swing visually arcs through space
// without the volume tracking — visible as "AI punches don't
// connect." Works identically for PC and NPC; takes the place of
// the historical sActiveAttackHitboxId/...joint-idx file-statics
// that the PC's PerFrameTick maintained.
void updateActiveAttackHitbox(Actor& actor);

// The actor pool. Player is conventionally at index 0; enemies
// follow at 1..N. Future systems (companions, NPCs, projectiles)
// join the same pool.
std::vector<Actor>& actors();

// Convenience accessor for the player. Returns actors()[0].
// Assumes the pool has at least one entry — call after the
// player has been initialized.
Actor& player();

// Find an actor by spawn_decl_id. Returns nullptr if no actor in the
// pool matches (including: actor never spawned, actor died and was
// cleaned, decl_id typo). Includes dead actors in the search -- filter
// is_dead at the call site if needed. O(N) scan over the pool;
// centralized so the dozen interactable closures + scripted-event
// handlers + boss HUD lookups don't each hand-write the same loop.
Actor* actorByDeclId(const std::string& spawn_decl_id);

// False when this actor's in-flight hitboxes must be invalidated --
// dead, knocked down, or in a non-fighting boss-state (Dying /
// Felled / Disengaged). Read by tickHitboxes each frame.
bool actorCanLandHits(const Actor& a);

// Per-actor mesh foot offset. Resolves via the actor's skeleton_id
// through the SkeletalAssets registry. Empty skeleton_id falls back
// to the player's mesh (backward-compat for any call site that
// hasn't been updated yet). Used by render + matrix-build sites
// (buildActorModelMatrix, lock-on target screen-project, etc.) so a
// wolf's mesh lands at the right foot height vs the humanoid's.
float actorFootOffsetY(const Actor& a);

// Initialize the pool with the player at index 0. Called once at
// startup, before any gameplay tick. Idempotent: clears + spawns.
void initActorPool();

// Per-frame tick driving actor-agnostic systems. Iterates the
// pool, advances each actor's animation, applies the consumed
// hip delta to world position (the contract that keeps feet
// planted, see feedback_hip_delta_two_sides.md), then applies
// actor-vs-world + actor-vs-actor collision push-out.
void tickActors(float dt);

} // namespace selva::gameplay
