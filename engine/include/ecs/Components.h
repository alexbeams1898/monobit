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
// textureId is an opaque handle issued by the (not-yet-built) texture manager.
// layer controls draw order: lower = drawn first (background), higher = foreground.
struct Sprite
{
    uint32_t textureId = 0;
    int srcX = 0;
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
