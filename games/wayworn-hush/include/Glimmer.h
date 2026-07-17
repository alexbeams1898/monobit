#pragma once

#include "Formulas.h"
#include "Growth.h"
#include "Observations.h"

#include <string>
#include <unordered_set>

#include <entt/entt.hpp>

class EntityManager;

// The warm world-space glow that hints at an OBSERVABLE spot. One rule: a spot glows while it
// is the InteractionSystem's resolved target (you're in reach / hovering it), and its PEAK
// brightness = formulas::glowBrightness(Perception) -- the SAME for every spot. The stat
// renders the world through the character's eyes: low Perception -> a faint (or no) glow;
// higher Perception -> the hint reads stronger. Nothing lingers: step away and it fades.
// Items are NOT glimmered -- they render their floor icon + a rim Outline. See
// docs/design/OBSERVATION-SYSTEM.md.

// A glow entity's smoothed display level + tint. `base_alpha` lerps toward the target each
// frame (glow when active+visible, else 0); Sprite.alpha = base_alpha + breathing, so the
// pulse rides on a clean fade and never flashes on first appearance.
struct Glimmer
{
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
    int size = 64;           // sprite cell size (px, square) -- matches the PNG
    float fade_speed = 6.0f; // alpha lerp rate toward the target (per second)
    // Peak brightness is NOT here -- it's formulas::glowBrightness(Perception) (config/
    // formulas.json), so the stat drives the glow. This block is pure feel (fade + breathe).
    float pulse_amp = 0.12f; // breathing depth (fraction of base alpha)
    float pulse_hz = 0.7f;   // breathing rate
    float warm_r = 1.0f;     // "notice me" warm tint (multiplied into the glow texture)
    float warm_g = 0.94f;
    float warm_b = 0.78f;
};

// Load the glow feel from config/glimmer.json (silent no-op -> defaults if missing).
void load(Config& cfg, const std::string& path);

// Spawn one glow entity per Observe-mode observable (warm tint, alpha 0, layer 3 above the
// prop it marks). Enter-mode encounters fire ambiently and carry no glimmer.
//
// `gone` is the set of placement ids this pilgrim has removed for good
// (savegame::World::gone): a spot consumed on a previous visit is skipped, so it doesn't
// come back when the world is rebuilt from the authored map.
void spawn(EntityManager& em, const observations::State& obs, const Config& cfg,
           const std::unordered_set<std::string>& gone = {});

// Per-frame: each encounter glimmer glows (lerps toward its peak) only while its Interactable
// is the active target; the peak = formulas::glowBrightness(Perception), the same for every
// spot. Otherwise it fades to 0. Breathing rides on top. dt = frame seconds.
// Set each interactable's `present` from live encounter visibility -- call BEFORE the
// interaction resolve, so a hidden encounter is no target (neither verb) and a revealed one
// becomes interactable the same frame. The one gate that keeps observe + act in agreement.
void refreshPresence(EntityManager& em, const observations::State& obs,
                     const growth::GrowthState& growth);

void update(EntityManager& em, const growth::GrowthState& growth, const formulas::Config& formulas,
            const Config& cfg, float dt);
} // namespace glimmer
