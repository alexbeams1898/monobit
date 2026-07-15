#pragma once

#include "Growth.h"
#include "Observations.h"

#include <string>

#include <entt/entt.hpp>

class EntityManager;

// The world-space glow at each observable: an UNOBSERVED spot glows warm + pulses
// when faced ("notice me"); once OBSERVED it keeps a muted persistent glow. See
// docs/design/OBSERVATION-SYSTEM.md.

// Marks a glow entity as the glimmer for a specific observable. `base_alpha` is
// the smoothed fade-in level (lerped toward the state's steady target); the
// displayed Sprite.alpha = base_alpha + breathing, recomputed each frame so the
// pulse rides ON TOP of a clean fade and never flashes on first appearance.
struct Glimmer
{
    std::string observable_id;
    float base_alpha = 0.0f;
};

namespace glimmer
{
// Glow feel (config over constants -- mirrors HeadMarkerConfig). All tunable; edit
// config/glimmer.json + relaunch (dev reads assets from source). See glimmer.json.
struct Config
{
    std::string sprite = "assets/sprites/glimmer.png";
    int size = 64;              // sprite cell size (px, square) -- matches the PNG
    float fade_speed = 6.0f;    // alpha lerp rate toward the target (per second)
    float bright_max = 0.55f;   // peak glow alpha when an unobserved spot is in reach
    float pulse_amp = 0.12f;    // breathing depth (fraction of base alpha)
    float pulse_hz = 0.7f;      // breathing rate
    float observed_dim = 0.18f; // muted persistent glow once observed
    float faced_boost = 1.6f;   // observed glow multiplier while the player is in reach
    float warm_r = 1.0f;        // "notice me" warm tint (multiplied into the glow texture)
    float warm_g = 0.94f;
    float warm_b = 0.78f;
};

// Load the glow feel from config/glimmer.json (silent no-op -> defaults if missing).
void load(Config& cfg, const std::string& path);

// Spawn one glow entity per observable (alpha starts at 0, below the character).
void spawn(EntityManager& em, const observations::State& obs, const Config& cfg);

// Per-frame: fade each glimmer's alpha toward its state target (unobserved vs
// observed), with breathing on top. dt = frame seconds; px/py = player pos (the glow
// lights within interact_reach of an observable's box).
void update(EntityManager& em, const observations::State& obs, const growth::GrowthState& growth,
            const Config& cfg, float px, float py, float dt);
} // namespace glimmer
