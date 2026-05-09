#pragma once

#include "anim/ClipRegistry.h"
#include "combat/AttackChain.h"
#include "combat/PlayerEquipment.h"
#include "combat/WeaponClass.h"

namespace selva::combat
{

// Resolved attack: the WeaponAttack record from the data model + the
// loaded clip pointer. `chain_size` is how many entries are in the
// chain that this attack came from (so the caller knows whether
// stepping to chain_index+1 is valid). nullptrs indicate "no swing
// this frame."
struct ResolvedAttack
{
    const selva::anim::AnimationClip* clip = nullptr;
    const WeaponAttack* attack = nullptr;
    int chain_size = 0;
};

// Result of dispatching a button press to the technique list. `locked`
// is the technique_index to commit (>=0) or -1 to keep the chain
// unlocked (multiple techniques still match). `valid` is false when no
// technique accepts the press at the given step — caller treats that
// as a chain miss.
struct TechniqueDispatch
{
    int locked = -1;
    bool valid = false;
};

// Pick the technique-list for (grip, kind), select the
// `technique_index` technique, return its `chain_index`-th attack
// resolved against the clip registry.
ResolvedAttack resolveAttackChainEntry(const PlayerEquipment& eq, HandSide hand,
                                       AttackKind kind, int chain_index, int technique_index,
                                       const selva::anim::ClipRegistry& clips);

// Pick which technique to commit to (if any) given the press button
// at step `chain_index`. When already locked, just validate that the
// locked technique accepts this button.
TechniqueDispatch dispatchTechniqueForPress(const PlayerEquipment& eq, HandSide hand,
                                            AttackKind kind, int chain_index,
                                            int current_locked, const char* button);

} // namespace selva::combat
