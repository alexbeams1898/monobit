#pragma once

// Per-frame post-physics clamp: keep ring-bound damned souls inside
// their own region's law-domain. Doctrine clamp -- a DamnedSoul actor
// whose post-physics position classifies as foreign territory gets
// pushed OUT of the winning foreign volume along its nearest face.
//
// Form gate: only DamnedSoul actors are clamped. UnjudgedSoul (Guide,
// Vagrant-projected NPCs), Animal (Lupa, descendant fauna),
// HellMachinery (keepers), Divine (Beatrice) all carry their own
// movement authority -- the territory clamp is Hell's measurement
// machinery, and Hell's measurement only grips imprinted damned souls
// (same invariant as the rest of the substance economy per
// [[project_imprint_handle_required_for_sangue]] +
// [[project_territory_system_doctrine]]).
//
// Player + actors without a spawn_region_id are skipped (the player
// has no domain restriction; flow-spawned or hand-spawned actors
// without a region id are intentionally region-free).
//
// Note: the actor's pos may sit INSIDE a larger own-region volume
// while ALSO being inside a smaller foreign volume that wins by
// smallest-volume ownership resolution (e.g. the descent corridor
// sits inside Limbo's disc; corridor is chapel_interior's; smallest-
// wins gives the corridor to chapel_interior). The clamp tests
// regionIdAtPosition -- the resolved owner -- not raw containment.

namespace selva::gameplay
{

void clampActorsToOwnTerritory();

} // namespace selva::gameplay
