#pragma once

#include <glm/vec3.hpp>

#include <string>
#include <unordered_set>
#include <vector>

// Hazard zones: spatial regions of the world that some actor-kinds
// avoid. Authored per-region in region JSON as `hazard_zones`. Each
// zone has a string `kind` tag (e.g. "acheron", "lava", "frozen")
// and an AABB extent. Actors declare which kinds they avoid via the
// archetype field `avoids_hazards` (also a list of string tags).
//
// Two consumers:
//   * BT decision logic -- skip chase when the target's position is
//     inside an avoid-zone (the actor can't reach the player without
//     entering a hazard it can't survive).
//   * Locomotion intent guard -- per-frame velocity clamp when the
//     prospective motion would carry the actor INTO an avoid-zone,
//     analogous to the existing AI-block-volume check. Belt-and-
//     braces for the BT-side gate.
//
// Both consumers delegate to actorAvoidsPos() so the avoidance test
// is one truth.
//
// Cosmologically rooted in [[project_soul_larvae_cosmology]] (the
// river-dissolves-on-contact mechanic): damned souls avoid Acheron.
// Future extensions: lava in Phlegethon's later sections, frozen
// ground in Cocytus, wind-blast zones in Lust. Schema is generic so
// adding a new hazard kind only requires authoring + archetype
// opt-in; no code change.

namespace selva::hazard
{

struct HazardZone
{
    std::string kind; // tag, e.g. "acheron"
    glm::vec3 center;
    glm::vec3 half_extents;
};

// Add a zone to the global registry. Called by region loaders when
// parsing region.json's `hazard_zones` field. Zones persist across
// regions (multi-resident architecture); a single global registry
// is sufficient.
void registerZone(const HazardZone& zone);

// Clear the registry. Called from hardResetWorldForCharacter so a
// character switch starts with no zones until the regions re-load.
void clearAllZones();

// Read-only access for diagnostics + tests.
const std::vector<HazardZone>& allZones();

// True if `pos` lies inside any registered hazard zone whose kind
// is in `avoided_kinds`. AABB inclusion test (closed interval --
// touching the boundary counts as inside).
bool positionIsInAvoidedZone(const glm::vec3& pos,
                             const std::unordered_set<std::string>& avoided_kinds);

// Convenience overload taking a list (matches the archetype's
// std::vector<std::string> avoids_hazards directly). Internally
// converts to set; fine for the typical case of 0-3 avoided kinds
// per actor.
bool positionIsInAvoidedZone(const glm::vec3& pos, const std::vector<std::string>& avoided_kinds);

} // namespace selva::hazard
