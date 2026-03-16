#pragma once

#include <cstdint>
#include <string>

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
// texturePath is the relative path to the PNG asset (resolved by TextureManager).
// textureId is the runtime GL handle — filled in by TextureManager::load() at render time.
// layer controls draw order: lower = drawn first (background), higher = foreground.
struct Sprite
{
    std::string texturePath; // e.g. "assets/sprites.png" — loaded from JSON config
    uint32_t textureId = 0;  // GL texture handle — set at runtime, not in JSON
    int srcX = 0;            // source rect within the texture atlas (pixels)
    int srcY = 0;
    int srcW = 0;
    int srcH = 0;
    int layer = 0;
};

struct Collider
{
    float width = 0.0f;
    float height = 0.0f;
    bool isSolid = true;
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
// moveX/moveY are set each frame by InputSystem from raw keyboard state.
// MovementSystem reads these values and translates them into Velocity.
//
// Using floats rather than bools keeps the door open for analog input (gamepad
// sticks) without changing this struct or any downstream system.
struct Input
{
    float moveX = 0.0f; // -1.0 = left,  0.0 = none, +1.0 = right
    float moveY = 0.0f; // -1.0 = up,    0.0 = none, +1.0 = down
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
    float speed = 100.0f;

    // How quickly this entity blends toward its desired velocity each second.
    // Higher = snappier direction changes (guardlike); lower = sluggish turns.
    // 0.0 = instant snap (no blending — legacy behaviour, useful for testing).
    // Typical range: 4.0 (slow patrol) – 20.0 (fast aggro enemy).
    float turnSpeed = 8.0f;

    // Distance at which this entity transitions from Idle to Chase.
    // 0.0 = no aggro check — entity starts in whatever state the config sets.
    // AggroSystem performs the Idle→Chase transition each frame.
    float aggroRadius = 0.0f;

    // How aggressively this entity steers away from nearby enemies.
    // Controls two forces that work together:
    //   1. Grid crowd repulsion — steers away from cells with high occupancy
    //      (medium-range, O(1) per entity).
    //   2. Same-cell separation — within-cell offset push for co-located enemies
    //      (close-range, O(n) total — see SteeringSystem).
    // 0.0 = disabled (enemies stack). 0.6 = CO (moderate ring). 1.2 = warden.
    // Raise for a looser mob; lower for a tighter, denser pack.
    // Config field: "separation_strength".
    float separationStrength = 1.0f;

    // Arrival softening radius: start slowing this entity when it enters this
    // distance from the player.  Speed scales linearly from full at arrivalRadius
    // down to ~zero at the player's position.  The ring radius emerges naturally
    // from the balance between the softened chase force and crowd separation —
    // there is no hard stop boundary.  Larger values = softer, wider approach.
    // 0 = disabled (full speed all the way in). Config field: "arrival_radius".
    float arrivalRadius = 0.0f;

    // Ring radius for the Attack formation.  When this entity transitions to
    // Attack state (AggroSystem fires when dist ≤ arrivalRadius), it targets a
    // point on the ring at this distance from the player — in its own approach
    // direction.  Each entity holds a different slot on the ring, so surrounding
    // emerges naturally without coordination.  The entity orbits the ring as the
    // player moves; transitions back to Chase if the player breaks engagement.
    // 0 = disabled (entity stays in Chase indefinitely). Config: "attack_radius".
    float attackRadius = 0.0f;
};
