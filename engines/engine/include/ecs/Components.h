#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include <entt/entt.hpp>

// ---------------------------------------------------------------------------
// ECS components -- pure data only, no methods, no logic.
// Systems operate on these; components just hold state.
//
// This file contains ENGINE components only -- infrastructure any 2D game
// could use without modification. Game-specific components (Weapon, Stats,
// AIController, GameInput, combat transients, etc.) live in
// game/include/ecs/GameComponents.h.
//
// Hard rule: NOTHING in this file may be specific to one genre, mechanic, or
// game. If you are adding a field and asking "but what other game would use
// this?" -- it belongs in GameComponents.h.
// ---------------------------------------------------------------------------

// Transform -- position + orientation in world space. Universally shared
// across 2D and 3D games: 2D code reads x/y/rotation and ignores z/pitch/roll;
// 3D code uses all six. Defaults keep 2D semantics intact (z=0, pitch=0,
// roll=0, scale=1). `rotation` is the 2D yaw shorthand (degrees) and is the
// same axis as `pitch`/`roll`'s missing 3D yaw — when the engine grows real
// 3D systems we may either rename or split; today the 2D-friendly `rotation`
// name lets every 2D system stay readable without a mass-rename.
struct Transform
{
    float x = 0.0f;
    float y = 0.0f;
    float z = 0.0f;        // 3D depth/height; 2D games leave at 0
    float rotation = 0.0f; // 2D yaw, in degrees
    float pitch = 0.0f;    // 3D pitch, in degrees
    float roll = 0.0f;     // 3D roll, in degrees
    float scale = 1.0f;
};

// Snapshot of the previous tick's position for render interpolation.
struct PreviousTransform
{
    float x = 0.0f;
    float y = 0.0f;
    float z = 0.0f;
};

struct Velocity
{
    float dx = 0.0f;
    float dy = 0.0f;
    float dz = 0.0f;
};

struct Health
{
    int current = 0;
    int max = 0;
};

// Sprite -- identifies which texture to draw and which region of it.
struct Sprite
{
    std::string texture_path;
    uint32_t texture_id = 0;
    int src_x = 0;
    int src_y = 0;
    int src_w = 0;
    int src_h = 0;
    int layer = 0;
    int sub_layer = 0;
    bool flip_x = false;
    bool use_sort_anchor = false;
    float sort_anchor = 0.0f;
    // Rotation in radians around the sprite center. Positive = clockwise
    // (screen-space y-down). Defaults to 0 so existing sprites are unaffected.
    float rotation = 0.0f;
    // Geometry-level horizontal mirror. Unlike flip_x (UV-only), this composes
    // correctly with rotation — the mirror happens before the rotation.
    bool geo_mirror_x = false;
};

struct Collider
{
    float width = 0.0f;
    float height = 0.0f;
    bool is_solid = true;
    // Entities in the same non-zero group emit collision events but skip MTV
    // resolution, allowing them to pass through each other. Group 0 (default)
    // resolves with everything.
    uint8_t collision_group = 0;
};

// Tag -- human-readable label for debug output and editor tooling.
struct Tag
{
    std::string name;
};

// Camera -- marks an entity as the active viewpoint.
// CameraSystem snaps x/y to the entity's Transform each tick. prev_x/prev_y
// are snapshotted before each tick for render interpolation.
// Game-applied offsets (lock-on blend) go in offset_x/y so they share the
// same interpolation base as the entity position — no step-size mismatch.
struct Camera
{
    float x = 0.0f;
    float y = 0.0f;
    float prev_x = 0.0f;
    float prev_y = 0.0f;
    // Additive offset blended by game code (e.g. lock-on camera).
    // Interpolated separately at render time and added to the base position.
    float offset_x = 0.0f;
    float offset_y = 0.0f;
    float prev_offset_x = 0.0f;
    float prev_offset_y = 0.0f;
    bool active = true;
};

// MovementIntent -- raw directional intent before collision resolution.
// Set each tick by InputMappingSystem (player) and AI systems (enemies).
// Read by AnimationSystem for walk-direction snapping, avoiding the direction-flip
// bug caused by reading post-collision Velocity when the entity presses against a wall.
struct MovementIntent
{
    float dx = 0.0f;
    float dy = 0.0f;
};

// FacingDirection -- visual facing + aim direction for an entity.
// dx/dy = visual facing (determines sprite direction via render_dx/render_dy).
// aim_dx/aim_dy = targeting direction (mouse/lock-on for player, same as dx/dy for AI).
struct FacingDirection
{
    float dx = 1.0f;
    float dy = 0.0f;
    float render_dx = 1.0f;
    float render_dy = 0.0f;

    // Aim direction -- where the entity is targeting. Used for hitbox placement,
    // projectile direction, dodge, shield arc, backstab detection.
    // Game code keeps this synced: for players, mouse/lock-on; for AI, same as dx/dy.
    float aim_dx = 1.0f;
    float aim_dy = 0.0f;

    // Set by game systems (MovementSystem for player, AggroSystem for AI).
    // Read by AnimationSystem for walk animation speed-up / reverse playback.
    bool sprinting = false;
    bool backpedaling = false;

    // Walk animation frame duration multiplier. < 1.0 = faster, > 1.0 = slower.
    // Set by game MovementSystem based on sprint/backpedal state.
    float walk_anim_speed = 1.0f;

    // Attack animation frame duration multiplier. < 1.0 = faster, > 1.0 = slower.
    // Set by game CombatSystem each tick from the active AttackLocked window so
    // a heavy weapon's swing animation stretches to match its longer cooldown.
    // 1.0 = use the sheet's native attack frame duration.
    float attack_anim_speed = 1.0f;

    // Crosshair position override. When aim_override_blend > 0, RenderSystem
    // lerps the crosshair from mouse toward (aim_override_x, aim_override_y).
    // 0.0 = fully at mouse, 1.0 = fully at override position.
    // Game code controls all ramping; engine just reads and lerps.
    float aim_override_x = 0.0f;
    float aim_override_y = 0.0f;
    float aim_override_blend = 0.0f;
};

// SolidColor -- overrides sprite rendering with a flat colored square.
struct SolidColor
{
    float r = 1.0f;
    float g = 1.0f;
    float b = 1.0f;
};

// Glow -- rendered as a larger, semi-transparent halo behind the entity.
struct Glow
{
    float scale = 2.5f;
    float alpha = 0.25f;
};

// Dead -- emplaced when Health reaches zero. Universal lifecycle marker.
// Timer is set from the death animation duration; entity is destroyed when it expires.
struct Dead
{
    float timer = 0.0f;
};

// TintOverride -- game systems emplace this to override the default render tint.
// TintSystem (game) owns full tint priority logic and writes this each frame.
// RenderSystem (engine) reads it. Clear via reg.clear<TintOverride>() each TintSystem frame.
struct TintOverride
{
    float r = 1.0f;
    float g = 1.0f;
    float b = 1.0f;
};

// ---------------------------------------------------------------------------
// Animation system components
// ---------------------------------------------------------------------------

enum class CardinalDir : uint8_t
{
    South = 0,
    West,
    East,
    North,
};

// Animation -- runtime animation state for an animated entity.
//
// Three categories of data:
//   Sheet layout  -- set once by ConfigLoader from animation JSON; never changes.
//   Playback      -- written each tick by game AnimStateSystem; read by engine.
//   Internal      -- managed solely by engine AnimationSystem.
//
// The engine has no concept of "Idle", "Walk", "Attack" etc. It only knows
// "play row N with M frames at D seconds per frame". Game code owns the
// mapping from game states to row/frames/duration.
struct Animation
{
    // --- Sheet layout (set once from config) ---
    int frame_width = 32;
    int frame_height = 32;
    int max_frames_per_state = 1;
    int row_count = 6;       // total rows in the spritesheet
    int direction_count = 4; // 1 (omnidirectional, static sprite) or 4 (cardinal)

    // --- Playback (written by game AnimStateSystem each tick) ---
    int current_row = 0;           // spritesheet row to play
    int current_frames = 1;        // number of frames in this row
    float current_duration = 0.0f; // seconds per frame (0 = static)
    bool freeze_on_last = false;   // true = one-shot (hold last frame), false = loop
    bool reverse = false;          // play frames in reverse order
    float speed_multiplier = 1.0f; // <1 = faster, >1 = slower

    // Per-frame column remap. When non-empty, the visible column is
    // frame_mask[frame_index] instead of frame_index directly. The mask
    // length overrides current_frames for playback purposes.
    // Empty = play columns 0..current_frames-1 normally.
    std::vector<int> frame_mask;

    // --- Internal (managed by engine AnimationSystem) ---
    CardinalDir dir = CardinalDir::South;
    int frame_index = 0;
    float frame_timer = 0.0f;
    int prev_row = -1; // detect row changes; -1 sentinel forces reset on first frame
};

// Marks an entity as a navigation agent for FlowFieldSystem/SteeringSystem.
struct NavAgent
{
    float separation_strength = 1.0f;
    // Set by ChaseSystem each frame: 1.0 = full speed, 0.0 = arrived at target.
    // SteeringSystem multiplies separation by this to prevent crowd repulsion
    // from dominating when the entity is slowing down for arrival.
    float arrival_scale = 1.0f;
    // Smoothed steering force — exponentially blended each frame to prevent
    // flickery direction changes when wall/crowd forces oscillate rapidly.
    float smooth_steer_x = 0.0f;
    float smooth_steer_y = 0.0f;
};

// CameraPan -- drives a smooth camera movement to a target and back.
// Emplaced on a Camera entity to trigger a cutscene pan. CameraPanSystem
// owns Camera.x/y while this is active; CameraSystem skips the entity.
struct CameraPan
{
    enum class Phase
    {
        ToTarget,
        Hold,
        Return,
        Done
    };

    float target_x = 0.0f;
    float target_y = 0.0f;
    float start_x = 0.0f;
    float start_y = 0.0f;
    float progress = 0.0f;
    float pan_speed = 1.5f;
    float hold_duration = 0.8f;
    Phase phase = Phase::ToTarget;
};

// Particle -- entities that age, shrink, and self-destruct.
struct Particle
{
    float lifetime = 1.0f;
    float age = 0.0f;
    float start_scale = 1.0f;
    float end_scale = 0.3f;
};
