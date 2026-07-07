#pragma once

#include <glm/vec3.hpp>

#include <vector>

namespace engine::world
{

// A single point light in the world. Emits in a sphere around its
// position, attenuating to ~zero at `radius`. Color in linear RGB,
// intensity scales the color's contribution.
//
// Designed to scale to future features without breaking callers:
//   - cast_shadow flag: when shadow-cubemap support lands, lights
//     with cast_shadow=true get a depth pass per light. Default-off
//     lights remain cheap.
//   - flicker_amp / flicker_freq: when the flicker shader path lands,
//     these drive a sin/noise multiplier on intensity. Default 0 =
//     steady.
//   - region_name: when a light is scoped to one terrain region
//     (e.g. a torch that only exists in Limbo), the renderer can
//     cull lights from other regions. nullptr = global.
struct LightSource
{
    glm::vec3 position{0.0f, 0.0f, 0.0f};
    glm::vec3 color{1.0f, 1.0f, 1.0f}; // linear RGB
    float intensity = 1.0f;
    float radius = 10.0f; // meters; brightness falls to ~zero past this

    // Forward-compatibility fields. Default values keep current
    // simple-point-light behavior; future shader features read these
    // when they exist.
    // Flicker drives sprite brightness AND the underlying point-light
    // intensity that lights the ground, so the visible flame and its
    // illumination stay in sync.
    //   intensity(t) = base × (1 + amp × sin(2π·freq·t + phase))
    // Pick amp ≤ 1 to avoid the light briefly going negative.
    float flicker_amp = 0.0f;  // 0..1, fraction of intensity that oscillates
    float flicker_freq = 0.0f; // Hz
    bool cast_shadow = false;  // future: shadow cubemap per light

    // Optional region tag (static-storage string literal, like
    // TerrainModifier::region_name). nullptr = applies globally.
    const char* region_name = nullptr;

    // Static-storage debug label. nullptr = unlabeled.
    const char* debug_name = nullptr;
};

// Register a light. Called by world-setup code (per-region declarations
// loaded from config, scripted lights from gameplay events, future
// player-crafted torches). Order is preserved but doesn't matter for
// rendering (lights are additive).
void registerLight(const LightSource& light);

// Register OR update a light identified by a stable string id. First
// call with a new id appends; subsequent calls overwrite the entry's
// fields. Use for moving lights (held torches, actor-carried
// lanterns) whose position must be re-set per frame. Passing an
// empty id is equivalent to registerLight() — a permanent entry
// without an update handle. Calls without an intervening
// clearLights() persist across frames.
void registerOrUpdateLight(const char* id, const LightSource& light);

// Remove a single light by its id. No-op if the id was never
// registered via registerOrUpdateLight (or has already been
// unregistered). Used for despawn (weapon un-equipped, actor
// removed).
void unregisterLight(const char* id);

// Remove all registered lights. Called on region unload.
void clearLights();

// Count + indexed access. Renderer iterates these per draw call to
// upload the active light set as a shader uniform array. Variable
// count (not capped at a compile-time max) so future content can
// scale up without engine refactor.
int lightCount();
const LightSource& lightAt(int idx);

// Range-based iteration for the renderer + debug overlays.
const std::vector<LightSource>& allLights();

// Time-modulated intensity for a single light. Applies sin-based
// flicker via `flicker_amp + flicker_freq + per-light phase`. Returns
// `intensity` unchanged when flicker_amp <= 0 (steady light).
//
// Phase comes from the light's index in the registry — keeps per-
// light flickers desynced without storing phase explicitly on each
// LightSource. Callers should pass the same `time_seconds` for one
// frame to keep all shaders + sprites in sync.
float flickerIntensity(int light_index, float time_seconds);

} // namespace engine::world
