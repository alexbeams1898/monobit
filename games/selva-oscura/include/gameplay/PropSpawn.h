#pragma once

// Prop spawn funnel -- diamond site for "PropDecl + archetype lookup
// -> something in the world."
//
// v1 implementation is lightweight: for category=Tree props, the
// funnel appends a CylinderCollider to the region's collision list
// with forced_variant_idx + forced_scale set from the archetype.
// The existing render path (WorldRenderer::renderTrees) iterates
// cylinders and draws the tree -- so the prop spawn pipeline is
// PURE AUTHORING migration. No new render path.
//
// Future categories (Light, future prop types) will extend this
// funnel with their own per-category attachment logic. The funnel
// is the single site any archetype-driven world insertion goes
// through.

#include "gameplay/PropArchetype.h"

#include <vector>

namespace selva::world
{
struct CollisionRegion;
}

namespace selva::gameplay
{

// Walk the supplied decl list, look up each one's archetype, and
// realize it into the region's runtime state. For trees: appends
// to `region.cylinders`. For lights: deferred until step 6 (Limbo
// conversion). Idempotent across reset cycles -- safe to call
// multiple times against a freshly-cleared region.
//
// `region` is the singleton CollisionRegion that drives
// world::currentRegion(). Passed in by reference so unit tests can
// drive it without touching globals (none today; future seam).
//
// Per-decl failures (missing archetype, unknown category) are
// logged and skipped -- never throws. A typo'd archetype id won't
// crash boot.
void spawnPropsFromDecls(selva::world::CollisionRegion& region, const char* region_name,
                         const std::vector<PropDecl>& decls);

// Evaluate procgen scatter rules. Each rule's mode dispatches to
// the matching producer (aisle / disc); both produce one cylinder
// per sample via the same archetype lookup path used by single-
// instance props. Skipped samples (carve-out hit, terrain region
// mismatch, distance-to-existing) follow the same rules the legacy
// populateHubTrees enforced.
void runPropScatterRules(selva::world::CollisionRegion& region,
                         const std::vector<PropScatterRule>& rules);

} // namespace selva::gameplay
