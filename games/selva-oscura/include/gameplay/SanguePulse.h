#pragma once

#include <glm/vec2.hpp>
#include <glm/vec3.hpp>

#include <cstdint>
#include <vector>

// Per [[project_crucible_censer_leveling_system]] auto-magnetization.
//
// On enemy death we decompose the dropped sangue amount into
// Roman-numeral additive tier particles (see RomanTiers.h). Each
// particle is its own world-anchored entity that:
//   - spawns at the corpse with a small random delay (staggered stream)
//   - projects to a 2D screen point at spawn time
//   - follows a 2D arc with mid-flight bulge toward screen-center
//   - sucks into the HUD vessel counter near the top-left
//   - on arrival, adds its denomination to the active profile's
//     sangue_vessel (and lifetime ledger via grantOnKill)
//
// The counter ticks one denomination at a time as particles land,
// so a +1000 kill visibly resolves over a beat with 10 medium-red
// particles, and a +1 trash kill is a single small bright particle.
// Per-tier color/size scales with denomination so the player reads
// magnitude at a glance.

namespace selva::gameplay
{

// One in-flight substance particle. Each carries a denomination (the
// amount granted on arrival) and a tier index (drives visual). The
// 2D source/target are screen-space coords captured at spawn; the
// pulse renderer interpolates between them with arc + suction.
struct SanguePulse
{
    glm::vec2 source_screen{0.0f, 0.0f}; // corpse projected to screen at spawn
    glm::vec2 target_screen{0.0f, 0.0f}; // HUD vessel counter screen pos
    float delay = 0.0f;                  // wait before flight begins (stagger)
    float elapsed = 0.0f;                // total elapsed since spawn
    float lifetime = 0.55f;              // flight time once delay elapses
    std::uint32_t denomination = 0u;     // sangue granted on arrival
    int tier_index = 0;                  // -> color/size lookup
    bool delivered = false;              // set true the frame grant fires
};

// Queue a sangue drop from world-space (e.g. fireEnemyDeath). The drop
// sits in a pending queue until the render layer materializes it into
// pulses with screen-space coords. This decoupling keeps the gameplay
// layer free of view-projection / HUD-layout concerns.
void queueSangueDrop(const glm::vec3& corpse_world_pos, std::uint32_t amount);

// Materialize pending world-space drops into per-particle screen-space
// pulses, using the supplied projector callback to convert each
// drop's world position to a screen point. Called once per frame by
// the render layer right before tickSanguePulses. The projector
// returns false for off-screen positions; those drops still spawn
// pulses but the source_screen falls back to the target so they
// appear to originate at the counter (rare-edge cosmetic fallback).
using WorldToScreenFn = bool (*)(const glm::vec3& world, glm::vec2& out_screen, void* ctx);
void materializePendingDrops(const glm::vec2& target_screen_pos, WorldToScreenFn project,
                             void* ctx);

// Spawn one pulse per Roman-tier particle decomposed from `amount`,
// using the supplied screen-space coords. Useful for tests and the
// debug-spawn hotkey that bypasses world projection.
//
// Per-particle stagger: spawn delays are distributed across a
// stream_duration that scales gently with particle count so a +1 fires
// instantly and a +12 stream lasts about half a second.
void spawnSanguePulses(const glm::vec3& corpse_world_pos, const glm::vec2& source_screen_pos,
                       const glm::vec2& target_screen_pos, std::uint32_t amount);

// Production tick: re-anchors live target each frame, routes grants
// to the active profile's vessel + lifetime ledger.
void tickSanguePulses(float dt, const glm::vec2& live_target_screen_pos);

// Inner tick exposed for tests.
std::size_t tickSanguePulsesWith(float dt, const glm::vec2& target_screen_pos,
                                 void (*grant)(std::uint32_t amount, void* ctx), void* ctx);

// Test-only: drains the pending-drop queue without materializing.
void clearPendingDropsForTest();

// Read-only access for the HUD/overlay renderer.
const std::vector<SanguePulse>& sanguePulses();

// Test-only: wipe the pool.
void clearSanguePulsesForTest();

} // namespace selva::gameplay
