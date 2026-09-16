#pragma once

// Post-physics clamp holding region-bound actors inside their own region.
//
// Gated on Form::DamnedSoul, and skipped entirely for actors with no
// spawn_region_id -- every other form carries its own movement authority.
//
// Tests regionIdAtPosition, the RESOLVED owner of a point, rather than raw
// containment: a position can sit inside a large volume and a smaller one at
// once and the smaller wins, so containment alone would clamp against the
// wrong region.

namespace selva::gameplay
{

void clampActorsToOwnTerritory();

} // namespace selva::gameplay
