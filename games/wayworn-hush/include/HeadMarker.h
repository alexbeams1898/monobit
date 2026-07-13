#pragma once

#include <string>

#include <entt/entt.hpp>

class EntityManager;

// A small white thought bubble that pops over the player's head when a thought is
// on screen -- a cloud with a short stem whose base sits at the head. It's a
// world-anchored sprite (like the observable Glimmer) parented to the player; it
// holds while the reading is up and fades when dismissed. The bubble is a minimal
// "he's having a thought" signal; the text (and its faculty hue) plays in the box.
// See docs/design/HUD.md.

// Tunable feel + placement (config over constants), loaded from JSON.
struct HeadMarkerConfig
{
    std::string sprite = "assets/sprites/thought_bubble.png";
    int size = 40;             // sprite cell size (px, square)
    float head_offset = 44.0f; // world px above the player Transform where the stem meets the head
    float side_offset = 18.0f; // world px right of center (the stem rises from beside the head)
    float peak_alpha = 0.9f;   // fully-shown opacity
    float fade_speed = 8.0f;   // alpha lerp rate toward the target (per second)
    float pulse_amp = 0.10f;   // breathing depth (fraction of base alpha)
    float pulse_hz = 0.8f;     // breathing rate
};

// Component on the single player-attached bubble entity. `shown` is set true while
// a thought is on screen (drives fade-in) and false otherwise (fade-out).
struct HeadMarker
{
    bool shown = false;
    float base_alpha = 0.0f; // smoothed fade level; display alpha rides on top
};

namespace head_marker
{
// Load feel/placement from config (silent no-op keeping defaults if missing).
void load(HeadMarkerConfig& out, const std::string& path);

// Spawn the single bubble entity (alpha 0, hidden). Call once at startup.
void spawn(EntityManager& em, const HeadMarkerConfig& cfg);

// Set whether a thought is currently on screen (drives fade in/out). Called each
// frame from the thought-box state.
void set(EntityManager& em, bool shown);

// Per-frame: keep the bubble over the player's head and fade toward its target
// (shown -> peak, hidden -> 0), with breathing on top. px/py = player world pos.
void update(EntityManager& em, const HeadMarkerConfig& cfg, float px, float py, float dt);
} // namespace head_marker
