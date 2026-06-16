#pragma once

#include "ecs/Items.h"

// Mid-combat hand-slot cycling. Z (left hand) / C (right hand) cycle
// forward through the items in inventory that fit the slot; Shift+key
// cycles backward. An "(empty)" position is part of the cycle, so the
// player can step to "no weapon" without opening the menu.
//
// Bindings (locked 2026-06-15):
//   Z / Shift+Z  -> LeftHand cycle forward / backward
//   C / Shift+C  -> RightHand cycle forward / backward
//   X            -> reserved (future two-hand toggle / quick-slot)
//
// Slot fit is delegated to selva::items::itemFitsSlot so the equip
// popup, the cycle hotkey, and any future "what can equip here" UI
// share one source of truth.

namespace selva::combat
{

#include <vector>

enum class CycleDirection
{
    Forward,
    Backward
};

// PURE math: given `candidate_ids` (the fitting-items list in insertion
// order), the currently-equipped `current_id` (kInvalid if empty), and
// a direction, return the next id in the cycle. Cycle order is
// [empty, candidate_0, candidate_1, ..., candidate_N-1] -> wraps.
//
// - Empty candidates list -> kInvalid (no movement possible).
// - current_id not in candidate_ids (stale) -> treats current as empty
//   (forward lands on candidate_0; backward lands on candidate_N-1).
//
// Exposed for testing: cycleHand() wraps this with active-profile +
// inventory queries; the math itself is verifiable in isolation.
engine::ecs::ItemInstanceId
nextCyclePosition(const std::vector<engine::ecs::ItemInstanceId>& candidate_ids,
                  engine::ecs::ItemInstanceId current_id, CycleDirection direction);

// Advance the equipped item in `slot` to the next (or previous) entry
// in the cycle. The cycle is: empty -> fitting items in insertion
// order -> empty -> ... (wraps). Equips into the slot (via
// engine::ops::inventory::equipItemToSlot, which auto-unequips the
// item from any other slot it was in). Empty step unequips.
//
// Returns the ItemInstanceId now equipped, or kInvalidItemInstanceId
// if the new position is the empty slot. Returns kInvalid + no
// mutation if there's no active player profile.
engine::ecs::ItemInstanceId cycleHand(engine::ecs::EquipSlot slot, CycleDirection direction);

} // namespace selva::combat
