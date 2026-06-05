#pragma once

#include <glm/gtc/quaternion.hpp>
#include <glm/vec3.hpp>

#include <string>
#include <vector>

namespace engine::world
{

// World-space AABB declaring "this volume is region X's law-domain."
// Authored in region.json's `territory` array. Several volumes per
// region are allowed (e.g. chapel_interior owns both the chapel
// building's interior AND the descent corridor that drops into Limbo's
// airspace; both volumes share owner_region_id = "chapel_interior").
//
// Replaces the older AI-barrier mechanism. The set of all territories
// across all regions forms the spatial partition of the world: any
// point either belongs to exactly one region (resolved by priority
// then by smallest-volume) or to none (the void).
struct Territory
{
    glm::vec3 center{0.0f};
    glm::vec3 half_extents{0.0f};
    // Orientation. Identity quaternion = axis-aligned box (default,
    // covers most cases). Non-identity = oriented bounding box (OBB)
    // for ramps and any rotated structures. All queries (containment,
    // nearest-point, corner generation for wireframe) transform world
    // points into the OBB's local frame using the conjugate of this
    // quaternion. JSON authoring is Euler XYZ in degrees, converted
    // to quat by the parser; identity-rotation entries omit the
    // `rotation_euler_deg` field entirely.
    glm::quat orientation{1.0f, 0.0f, 0.0f, 0.0f};
    std::string owner_region_id;
    std::string debug_name;
    // Higher priority wins overlap. Ties broken by smallest volume
    // (innermost). 0 = default; explicit overrides go above (e.g. the
    // corridor inside Limbo's disc could declare priority 20 to
    // outrank Limbo's priority 0 disc, but smallest-wins also gives
    // the right answer because the corridor IS smaller).
    int priority = 0;
};

// Register a territory volume. Called by JsonRegion::registerModifiers
// at boot. Idempotent only over the same JsonRegion instance; the
// registry is append-only across program lifetime.
void registerTerritory(const Territory& t);

int territoryCount();

// Indexed access for iterators (e.g. the per-actor own-territory
// nearest-point clamp). Indices are stable across program lifetime
// (registry is append-only).
const Territory& territoryAt(int idx);

// Resolve the region whose territory contains world_pos. Returns
// empty string if no registered territory claims this point (the
// "void" — typically above ceilings or beyond region boundaries).
//
// Tie-break: highest priority wins, then smallest volume.
// Boundary convention: min-inclusive, max-exclusive (a point on a
// shared face belongs to exactly one volume, never both).
const std::string& regionIdAtPosition(const glm::vec3& world_pos);

} // namespace engine::world
