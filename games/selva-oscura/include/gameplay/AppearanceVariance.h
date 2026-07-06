#pragma once

#include <string>
#include <unordered_map>

namespace selva::gameplay
{

struct Appearance;

// Roll random face-morph weights into ``out`` for an enemy actor whose
// archetype opts in via EnemyArchetype::random_face_morphs. Draws from
// a hardcoded ranges table (see AppearanceVariance.cpp) covering the
// identity-defining fine morphs: nose (hump/volume/width), cheek bones
// (per-side asymmetry), chin (prominent/width), eyes (per-side
// asymmetric position), mouth (lip volume, position). Deliberately
// EXCLUDES the scale morphs (head-scale-*, l/r-eye-scale-*,
// mouth-scale-horiz-*) so no actor grows a giant / tiny head or eyes
// -- variance is FACE-STRUCTURE only, silhouette stays constant.
//
// Values chosen to be subtly perceptible without pushing into caricature:
// each morph gets a range roughly 0..0.3 for incr/decr pairs. Symmetric
// morphs (nose-hump-incr vs nose-hump-decr) are treated as ONE bipolar
// axis -- the roller picks one side and leaves the other at zero, so
// we never end up with "hump both incr AND decr at 0.3" (which would
// cancel authored deltas or overshoot the mesh's fitting range).
//
// Existing entries in ``out`` for morphs the table covers are
// OVERWRITTEN; morphs the table does not touch (head-scale, eye-scale,
// mouth-scale, and any player-creator-only entries) stay untouched.
// So an actor whose base appearance already sets head-scale-vert-incr
// keeps it, but the fresh random face structure lands on top.
void rollRandomFaceMorphs(std::unordered_map<std::string, float>& out);

} // namespace selva::gameplay
