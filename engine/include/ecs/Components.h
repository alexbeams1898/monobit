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
