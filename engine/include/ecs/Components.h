#pragma once

#include <cstdint>
#include <entt/entt.hpp>
#include <string>

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

struct Transform
{
    float x = 0.0f;
    float y = 0.0f;
    float rotation = 0.0f; // degrees
    float scale = 1.0f;
};

// Snapshot of the previous tick's position for render interpolation.
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
};

struct Collider
{
    float width = 0.0f;
    float height = 0.0f;
    bool is_solid = true;
};

// Tag -- human-readable label for debug output and editor tooling.
struct Tag
{
    std::string name;
};

// Camera -- marks an entity as the active viewpoint.
struct Camera
{
    float x = 0.0f;
    float y = 0.0f;
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

// FacingDirection -- normalized direction the entity is facing.
struct FacingDirection
{
    float dx = 1.0f;
    float dy = 0.0f;
    float render_dx = 1.0f;
    float render_dy = 0.0f;

    // Set by game systems (MovementSystem for player, AggroSystem for AI).
    // Read by AnimationSystem for walk animation speed-up.
    bool sprinting = false;
};

// SolidColor -- overrides sprite rendering with a flat colored square.
struct SolidColor
{
    float r = 1.0f;
    float g = 1.0f;
    float b = 1.0f;
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
    South = 0,
    West,
    East,
    North
};

struct AnimStateData
{
    int row = 0;
    int frames = 1;
    float duration = 0.0f;
};

// Animation -- runtime animation state for an animated entity.
// anim.state is written each tick by AnimStateSystem (game); AnimationSystem (engine)
// reads it for frame advancement and detects changes via prev_state.
struct Animation
{
    AnimState state = AnimState::Idle;
    AnimState prev_state = AnimState::Idle; // used to detect state changes
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
struct BodyPart
{
    entt::entity parent = entt::null;
    bool direction_from_facing = false;
};

// Marks an entity as a navigation agent for FlowFieldSystem/SteeringSystem.
struct NavAgent
{
    float separation_strength = 1.0f;
};

// Particle -- entities that age, shrink, and self-destruct.
struct Particle
{
    float lifetime = 1.0f;
    float age = 0.0f;
    float start_scale = 1.0f;
    float end_scale = 0.3f;
};
