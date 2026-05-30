#pragma once

#include <glm/vec3.hpp>

#include <string>
#include <vector>

namespace selva::gameplay
{

// World-space AABB that AI actors from FOREIGN regions cannot enter.
// Per [[doctrine]] in the Selva cosmology: Hell entities (e.g. limbo
// shades) cannot cross the threshold into the chapel / descent corridor
// / chapel_exterior plateau because those zones are OUTSIDE Hell. The
// player passes through freely; only AI is constrained.
//
// Authored in each region.json's `ai_block_volumes` array. The owning
// region's AI actors ignore THEIR OWN region's barriers (a chapel-
// interior actor isn't blocked by chapel-interior's volumes). Foreign-
// region AI actors are stopped at the boundary.
struct AiBlockVolume
{
    glm::vec3 center{0.0f};
    glm::vec3 half_extents{0.0f};
    std::string owner_region_id; // matches JsonRegion::regionId()
    std::string debug_name;
};

// Register a barrier volume. Called by JsonRegion::registerModifiers()
// at boot (the same lifecycle phase that wires terrain modifiers --
// before any AI tick can run, after region.json parsing). Idempotent
// only over the same JsonRegion instance; the registry is append-only
// across the program's lifetime per the resident-all-scenes
// architecture.
void registerAiBlockVolume(const AiBlockVolume& v);

// Total count (for diagnostic logging at boot).
int aiBlockVolumeCount();

// Query: is `world_pos` inside any registered block volume that is
// NOT owned by `actor_region_id`? Returns the first matching volume's
// pointer, or nullptr if the position is free. Used by AI locomotion
// to clamp/reject moves that would carry a foreign actor across the
// boundary.
//
// The returned pointer is stable across program lifetime (the registry
// is append-only).
const AiBlockVolume* findAiBlockingVolume(const glm::vec3& world_pos,
                                          const std::string& actor_region_id);

} // namespace selva::gameplay
