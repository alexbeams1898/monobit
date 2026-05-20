#pragma once

namespace selva::anim
{
class ClipRegistry;
class PoseSampler;
} // namespace selva::anim

namespace selva::combat
{

class WeaponClassRegistry;

// Startup pass: scan every loaded WeaponAttack, compute and cache
// resolved cancel-open / chain-link-start times in the registry.
// Logs a per-chain summary to combat-debug.log when combat-debug is on.
//
// Mutates the registry; call after sampler + clips + weaponClasses
// are loaded but before fireAttack runs.
void resolveAttackCancelOpenTimes(WeaponClassRegistry& weapon_classes,
                                  const selva::anim::ClipRegistry& clips,
                                  const selva::anim::PoseSampler& sampler);

} // namespace selva::combat
