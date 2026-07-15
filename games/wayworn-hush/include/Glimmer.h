#pragma once

#include "Growth.h"
#include "Observations.h"

#include <string>

#include <entt/entt.hpp>

class EntityManager;

// The world-space glow that highlights an interactable: it glows when it is the
// InteractionSystem's resolved target ("the thing you're about to act on") and holds a
// steady idle level otherwise. Generic over WHAT it marks -- an observation dims once
// observed and tints warm; a pickup tints by item rarity and has no observed state. The
// per-kind behavior lives in each system's target-setting step (setObservationTargets, or
// pickups set once at spawn); this component + update() are kind-agnostic. See
// docs/design/OBSERVATION-SYSTEM.md + GAME-SYSTEMS.md.

// A glow entity's plain-data targets, set by its owning system and lerped toward each
// frame by glimmer::update. `active_alpha` is the glow while this is the resolved
// interaction target; `idle_alpha` the steady glow otherwise (0 = off; a muted value for
// an already-observed spot). `tint_*` is the glow color (warm for observations, rarity
// color for pickups). `base_alpha` is the smoothed displayed level -- the Sprite.alpha =
// base_alpha + breathing, so the pulse rides ON TOP of a clean fade and never flashes.
struct Glimmer
{
    float active_alpha = 0.55f;
    float idle_alpha = 0.0f;
    float tint_r = 1.0f;
    float tint_g = 1.0f;
    float tint_b = 1.0f;
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
    float warm_r = 1.0f;        // observation "notice me" warm tint (into the glow texture)
    float warm_g = 0.94f;
    float warm_b = 0.78f;
};

// Load the glow feel from config/glimmer.json (silent no-op -> defaults if missing).
void load(Config& cfg, const std::string& path);

// Spawn one glow entity per Observe-mode observable (warm tint; idle/active targets are
// refreshed each frame by setObservationTargets). Enter-mode observables fire ambiently
// and carry no glimmer. Alpha starts at 0, layer 3 (above the prop it marks). Items are NOT
// glimmered -- they render their floor icon (world_items::spawn), the item is its own cue.
void spawn(EntityManager& em, const observations::State& obs, const Config& cfg);

// Refresh each observation glimmer's idle/active targets from its observed/unobserved
// signal (unobserved: off until active, then bright; observed: a muted persistent glow,
// brighter while active). The observation-specific dimming; pickups skip this (their
// targets are fixed at spawn). Call before update() each frame.
void setObservationTargets(EntityManager& em, const observations::State& obs,
                           const growth::GrowthState& growth, const Config& cfg);

// Per-frame: lerp each glimmer's alpha toward active_alpha (when its Interactable is the
// resolved target) or idle_alpha (otherwise), ride the breathing pulse on top, and lerp
// the tint. Kind-agnostic -- reads only the Glimmer component + Interactable.active. dt =
// frame seconds.
void update(EntityManager& em, const Config& cfg, float dt);
} // namespace glimmer
