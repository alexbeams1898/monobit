#pragma once

#include <cstdint>
#include <string>

// entt forward-declaration for Hitbox.owner.
// The full header is pulled in by EntityManager.h; components must not
// depend on entt directly to stay as lightweight as possible.
#include <entt/entt.hpp>

// ---------------------------------------------------------------------------
// Core ECS components — pure data only, no methods, no logic.
// Systems operate on these; components just hold state.
//
// All structs are small and default-constructible so entt can manage them
// efficiently in its sparse-set storage.
// ---------------------------------------------------------------------------

struct Transform
{
    float x = 0.0f;
    float y = 0.0f;
    float rotation = 0.0f; // degrees
    float scale = 1.0f;
};

// Snapshot of the previous tick's position for render interpolation.
// Engine copies Transform → PreviousTransform at the start of each fixed update.
// RenderSystem blends between the two using the accumulator remainder (alpha).
struct PreviousTransform
{
    float x = 0.0f;
    float y = 0.0f;
};

struct Velocity
{
    float dx = 0.0f;
    float dy = 0.0f;
};

struct Health
{
    int current = 0;
    int max = 0;
};

// Sprite — identifies which texture to draw and which region of it.
// texture_path is the relative path to the PNG asset (resolved by TextureManager).
// texture_id is the runtime GL handle — filled in by TextureManager::load() at render time.
// layer controls draw order: lower = drawn first (background), higher = foreground.
struct Sprite
{
    std::string texture_path; // e.g. "assets/sprites.png" — loaded from JSON config
    uint32_t texture_id = 0;  // GL texture handle — set at runtime, not in JSON
    int src_x = 0;            // source rect within the texture atlas (pixels)
    int src_y = 0;
    int src_w = 0;
    int src_h = 0;
    int layer = 0;
};

struct Collider
{
    float width = 0.0f;
    float height = 0.0f;
    bool is_solid = true;
};

// Tag — human-readable label for debug output and editor tooling.
// Not used for game logic; query by component type instead.
struct Tag
{
    std::string name;
};

// Camera — marks an entity as the active viewpoint.
// CameraSystem snaps x/y to the tracked entity's Transform each frame.
// Only one Camera with active=true should exist at a time.
struct Camera
{
    float x = 0.0f; // world-space centre of the view (updated by CameraSystem)
    float y = 0.0f;
    bool active = true; // false = ignored by CameraSystem and RenderSystem
};

// Input — marks an entity as player-controlled and carries its movement intent.
// move_x/move_y are set each frame by InputSystem from raw keyboard state.
// MovementSystem reads these values and translates them into Velocity.
//
// Using floats rather than bools keeps the door open for analog input (gamepad
// sticks) without changing this struct or any downstream system.
struct Input
{
    float move_x = 0.0f; // -1.0 = left,  0.0 = none, +1.0 = right
    float move_y = 0.0f; // -1.0 = up,    0.0 = none, +1.0 = down

    // Combat inputs — set by InputSystem each frame.
    bool attack = false;             // LMB / E — held state, gated by swing cooldown
    bool dodge = false;              // Space tap (<200ms) — edge trigger, fires once per tap
    bool sprint = false;             // Space hold (>200ms) — continuous while held
    bool skill = false;              // Q — held state, gated by skill cooldown
    bool block_held = false;         // RMB held
    bool block_just_pressed = false; // RMB edge-detect (low→high this frame)

    // Auto-attack toggle — P key handled in InputSystem, stored on AutoAttackMode component.
    // Kept here only as a transient "toggle pressed this frame" flag.
    bool auto_toggle_just_pressed = false;

    // Dodge cooldown — tracked here because only the player-controlled entity dodges.
    float dodge_cooldown_remaining = 0.0f;

    // Last non-zero movement direction — persists so attacks fire forward
    // even when the player stops moving.  Updated by InputSystem.
    float last_facing_x = 1.0f;
    float last_facing_y = 0.0f;

    // Footstep cadence timer — counts down; plays a step sound when it hits zero.
    float step_timer = 0.0f;

    // Wall bump sound cooldown — prevents spamming on sustained wall contact.
    float wall_bump_cooldown = 0.0f;

    // Debug stat-allocation — pressed this frame (set by InputSystem, consumed by LevelingSystem).
    bool alloc_str = false;
    bool alloc_dex = false;
    bool alloc_end = false;
    bool alloc_lck = false;
};

// ---------------------------------------------------------------------------
// Stat system — universal rulebook applied to ALL entities (player and enemies).
// ---------------------------------------------------------------------------

// Stats — base stats for any entity.  All systems that care about combat
// read these values directly; none store derived copies.
// Derived values (maxHP, moveSpeed, DEF) are computed on-the-fly from formulas.
struct Stats
{
    int str = 1; // Attack power, heavy weapon speed, DEF contribution
    int dex = 1; // Move speed, light weapon speed, attack speed
    int end = 1; // Max health
    int lck = 1; // Drop rate, ranged accuracy
};

// Experience — tracks level progression and unspent stat allocation points.
// Only entities the player can level up carry this (player + future boss units).
// Enemies typically don't carry Experience but their level is inferred from
// their Stats for DEF calculation purposes.
struct Experience
{
    int current_xp = 0;
    int xp_to_next = 100; // threshold recomputed by LevelingSystem on level-up
    int level = 1;
    int stat_points = 0; // unspent — allocated via debug keys 1/2/3/4
};

// Weapon — equipped weapon state and runtime cooldown timers.
// All static weapon data (weight, scaling) comes from entity config JSON.
// Scaling values are raw floats (e.g. 1.0 = B-tier, 1.5 = S-tier).
// Grade letters (S/A/B/C/D/E) are computed from these floats at display time
// using the grade_thresholds table in FormulaConfig.
struct Weapon
{
    std::string name; // display name (e.g. "Fist", "Iron Sword")
    float weight = 0.5f;
    float str_scaling = 0.25f; // per-point STR damage multiplier
    float dex_scaling = 0.25f; // per-point DEX damage multiplier
    int str_requirement = 0;
    int dex_requirement = 0;
    float base_damage = 5.0f;

    // Runtime timers — decremented by CombatSystem each frame.
    float swing_cooldown_remaining = 0.0f;
    float skill_cooldown_remaining = 0.0f;
};

// FacingDirection — normalized direction the entity is facing.
// Updated by MovementSystem from velocity (all entities); InputSystem also
// maintains last_facing_x/Y on the Input component for the player-controlled case.
struct FacingDirection
{
    float dx = 1.0f; // default: face right
    float dy = 0.0f;
    // Smoothed visual facing for render (dot indicator, future sprite selection).
    // Blended toward dx/dy each frame — filters micro-tremor while staying fluid.
    // Gameplay systems (CombatSystem) read dx/dy directly for instant response.
    float render_dx = 1.0f;
    float render_dy = 0.0f;
};

// ---------------------------------------------------------------------------
// Combat transient components
// ---------------------------------------------------------------------------

// Hitbox — a one-frame entity spawned by CombatSystem on each attack swing.
// Carries the computed damage and the owner so DamageSystem can avoid self-hits
// and so parry can stagger the correct attacker.
// Destroyed by CombatSystem at the start of the NEXT frame.
struct Hitbox
{
    float damage = 0.0f;
    entt::entity owner = entt::null;
};

// SolidColor — overrides sprite rendering with a flat colored square.
// Uses the engine's 1×1 white texture tinted to (r, g, b).
// Width/height are taken from the entity's Sprite src_w/src_h.
struct SolidColor
{
    float r = 1.0f;
    float g = 1.0f;
    float b = 1.0f;
};

// RestSpot — a static entity the player can stand on to restore HP to full.
// Stub: no animation, no cost, just heals on proximity.
// cooldown prevents re-triggering every frame while the player stands on it.
struct RestSpot
{
    float radius = 64.0f;  // world-space heal trigger radius
    float cooldown = 0.0f; // seconds until next heal; 0 = ready
};

// Pickup — an XP or money drop left by dead enemies.
// Auto-collected when the player enters the collection radius.
struct Pickup
{
    int xp_value = 0;
    int money_value = 0;
    float radius = 48.0f; // world-space collection radius
};

// Loot — reward data dropped when an entity dies.
// xp_drop is the base XP value; DeathSystem scales it by the entity's stat sum.
// Semantically separate from AIController (loot != AI behavior).
struct Loot
{
    int xp_drop = 20;
    int money_drop = 0;
};

// Dead — emplaced by DamageSystem when Health.current reaches zero.
// DeathSystem ticks the timer down each frame; entity is destroyed when it
// reaches zero. If an Animation component exists, the timer is set to the
// death animation's total duration so the anim plays out before removal.
// Non-animated entities get timer=0 and are destroyed immediately.
struct Dead
{
    float timer = 0.0f;
};

// Dodging — active while the player is in a dodge roll.
// Grants i-frames: DamageSystem skips hits against entities with this component.
struct Dodging
{
    float remaining = 0.0f; // seconds until dodge ends
};

// AttackLocked — animation commitment after a swing.
// Blocks new attacks and dodges until the timer expires.
struct AttackLocked
{
    float remaining = 0.0f;
};

// Shield — one-handed shield equipped in the off-hand slot.
// Blocks incoming damage while Input.block_held is true and guard_health > 0.
// Guard break: absorbing too many consecutive blocked hits staggers the player.
struct Shield
{
    float guard_health = 100.0f;
    float max_guard = 100.0f;
    bool blocking = false; // propagated from Input.block_held by DamageSystem
};

// Parrying — brief invulnerability + stagger window opened by a timed block press.
// Expires after the window (0.15 s).  Any hit that lands during this window
// negates damage and emplaces Staggered on the attacker.
struct Parrying
{
    float remaining = 0.0f;
};

// Staggered — guard-break or parry result.  Entity cannot act until it expires.
// MovementSystem skips input-based velocity for staggered entities.
struct Staggered
{
    float remaining = 0.0f;
};

// Poise — determines how many hits an entity can absorb before staggering.
// Poise damage accumulates per hit (scaled from attacker's weapon weight).
// When current >= max (or max == 0), Staggered is applied and current resets.
// After decay_window seconds with no hits, current resets naturally.
// max = 0 means any hit staggers (no armor / naked state).
// Armor contributes to max when equipped — deferred until armor system.
struct Poise
{
    float max = 0.0f;         // stagger threshold; 0 = stagger on any hit
    float current = 0.0f;     // accumulated poise damage this window
    float decay_timer = 0.0f; // time since last poise hit
};

// DamageFeedback — red flash applied by DamageSystem when an entity takes damage.
// Ticked down by CombatSystem each frame; removed when expired.
struct DamageFeedback
{
    float remaining = 0.0f;
};

// AttackFeedback — yellow flash applied by CombatSystem when an entity swings.
// Ticked down by CombatSystem each frame; removed when expired.
struct AttackFeedback
{
    float remaining = 0.0f;
};

// AutoAttackMode — when enabled, CombatSystem fires weapons automatically
// toward the nearest enemy instead of requiring player input.
// Unlocked via the meta-store; toggled with P (debug) during development.
struct AutoAttackMode
{
    bool enabled = false;
};

// AIController — drives non-player entity behavior.
//
// Performance note: the player target is NOT stored here. ChaseSystem fetches
// the player position once per frame (via the Input component tag) and sweeps
// all AIControllers in one tight loop — no per-entity indirection, no random
// memory lookups. This keeps the hot path O(n) and cache-friendly at 1000+
// enemies.
//
// State is read from config at spawn ("behavior": "chase") and can be mutated
// at runtime by any system (e.g. aggro: Idle → Chase on proximity).
struct AIController
{
    enum class State
    {
        Idle,
        Chase,
        Attack
    };

    State state = State::Idle;

    // How quickly this entity blends toward its desired velocity each second.
    // Higher = snappier direction changes (guardlike); lower = sluggish turns.
    // 0.0 = instant snap (no blending — legacy behaviour, useful for testing).
    // Typical range: 4.0 (slow patrol) – 20.0 (fast aggro enemy).
    float turn_speed = 8.0f;

    // Distance at which this entity transitions from Idle to Chase.
    // 0.0 = no aggro check — entity starts in whatever state the config sets.
    // AggroSystem performs the Idle→Chase transition each frame.
    float aggro_radius = 0.0f;

    // How aggressively this entity steers away from nearby enemies.
    // Controls two forces that work together:
    //   1. Grid crowd repulsion — steers away from cells with high occupancy
    //      (medium-range, O(1) per entity).
    //   2. Same-cell separation — within-cell offset push for co-located enemies
    //      (close-range, O(n) total — see SteeringSystem).
    // 0.0 = disabled (enemies stack). 0.6 = CO (moderate ring). 1.2 = warden.
    // Raise for a looser mob; lower for a tighter, denser pack.
    // Config field: "separation_strength".
    float separation_strength = 1.0f;

    // Arrival softening radius: start slowing this entity when it enters this
    // distance from the player.  Speed scales linearly from full at arrival_radius
    // down to ~zero at the player's position.  The ring radius emerges naturally
    // from the balance between the softened chase force and crowd separation —
    // there is no hard stop boundary.  Larger values = softer, wider approach.
    // 0 = disabled (full speed all the way in). Config field: "arrival_radius".
    float arrival_radius = 0.0f;

    // Ring radius for the Attack formation.  When this entity transitions to
    // Attack state (AggroSystem fires when dist ≤ arrival_radius), it targets a
    // point on the ring at this distance from the player — in its own approach
    // direction.  Each entity holds a different slot on the ring, so surrounding
    // emerges naturally without coordination.  The entity orbits the ring as the
    // player moves; transitions back to Chase if the player breaks engagement.
    // 0 = disabled (entity stays in Chase indefinitely). Config: "attack_radius".
    float attack_radius = 0.0f;

    // Difficulty tier applied once at spawn by LevelingSystem::applyInitialDerivations.
    // All base stats are multiplied by this before HP and other derivations run.
    // tier=1 is the baseline (default). Zone spawners set tier=2, 3, etc. to produce
    // harder variants of the same enemy type without needing a separate config file.
    int tier = 1;

    // Sprint: when in Chase state and the player is beyond sprint_threshold px,
    // this entity sprints at sprint_multiplier × base speed.
    // 0.0 sprint_multiplier = disabled (no sprint). Config fields: "sprint_multiplier",
    // "sprint_threshold". Runtime flag set each frame by AggroSystem.
    float sprint_multiplier = 0.0f;
    float sprint_threshold = 0.0f;
    bool sprint = false;
};

// ---------------------------------------------------------------------------
// Animation system components
// ---------------------------------------------------------------------------

enum class AnimState : uint8_t
{
    Idle = 0,
    Walk,
    Attack,
    Hit,
    Death
};

enum class CardinalDir : uint8_t
{
    South = 0, // facing down (toward camera) — default
    West,
    East,
    North
};

// Per-state frame metadata loaded from sprite sheet sidecar JSON.
struct AnimStateData
{
    int row = 0;           // which row in the sheet this state occupies
    int frames = 1;        // number of frames in this strip
    float duration = 0.0f; // seconds per frame (0 = static, no advance)
};

// Animation — runtime animation state for an animated entity.
// AnimationSystem reads this to update the Sprite src rect each frame.
//
// Sprite sheet layout:
//   Row = state (from JSON). Column = dir * max_frames + frame_index.
//   Direction column blocks: South 0..N-1, West N..2N-1, East 2N..3N-1, North 3N..4N-1
//   where N = max_frames_per_state.
struct Animation
{
    AnimState state = AnimState::Idle;
    CardinalDir dir = CardinalDir::South;
    int frame_index = 0;
    float frame_timer = 0.0f;

    static constexpr int STATE_COUNT = 5;
    AnimStateData states[STATE_COUNT]{};

    int frame_width = 32;
    int frame_height = 32;
    int max_frames_per_state = 1;
};

// Links a child entity to a parent for split-body rendering.
// The child has its own Sprite + Animation; AnimationSystem resolves state
// and direction from the parent's gameplay components.
//   faces_aim = false  (lower body): state = Dead > Hit > Walk > Idle, dir from Velocity
//   faces_aim = true   (upper body): state = Dead > Hit > Attack > Idle, dir from FacingDirection
struct BodyPart
{
    entt::entity parent = entt::null;
    bool faces_aim = false;
};
