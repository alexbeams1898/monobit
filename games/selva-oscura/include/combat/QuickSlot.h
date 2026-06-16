#pragma once

#include "ecs/Items.h"

#include <string>
#include <vector>

// Quick-slot consumable rotation (Souls/ER convention).
//
// The player ASSIGNS consumable items (by config_path, not instance)
// to a fixed-capacity rotation (kQuickSlotCapacity in AppState.h).
// During combat:
//   X / Shift+X  -> cycle the "primed" index forward / backward
//   Q            -> use the currently-primed item (decrements stack)
//
// Assignment is by config_path so it survives crafting new instances
// of the same item type (you don't have to re-assign every time you
// craft a Poultice). The use lookup queries inventory at use time for
// any instance of the primed config_path.
//
// State lives on PlayerProfile (quick_slot_assigned +
// quick_slot_primed_index); persists across save/load.

namespace selva::combat
{

enum class CycleDirection; // defined in HandCycle.h

// Append `config_path` to the active profile's quick_slot_assigned if
// not already present AND there's capacity. Returns true on insert,
// false if already assigned, capacity reached, or no active profile.
// If this is the FIRST assignment, primed_index becomes 0.
bool assignToQuickSlot(const std::string& config_path);

// Remove `config_path` from the active profile's quick_slot_assigned
// if present. Adjusts primed_index so it remains valid (clamps to
// new size; sets to -1 when list becomes empty). Returns true on
// removal, false on not-found / no profile.
bool unassignFromQuickSlot(const std::string& config_path);

// True if `config_path` is in the active profile's quick_slot_assigned.
// Defensive: returns false on null profile.
bool isAssignedToQuickSlot(const std::string& config_path);

// Cycle the primed index forward (or backward). Wraps modulo size.
// No-op if the rotation is empty. Returns the new primed index, or
// -1 if no profile / empty rotation.
int cyclePrimed(CycleDirection direction);

// Use the currently-primed item: looks up its config_path, finds an
// inventory instance, fires its use_handler, consumes one. Returns
// true if a use fired, false if no profile / empty rotation / primed
// item has zero count in inventory / no use_handler registered for
// that item.
bool useprimed();

// True if the active profile has at least one assigned item AND the
// primed item has at least one instance in inventory. Combat HUD
// uses this to dim the slot when use would no-op.
bool primedHasInstance();

// QoL hook: called from any "item granted to inventory" path
// (pickups, crafting). If Settings.auto_assign_consumables_to_quick_slot
// is on AND the item is a Consumable AND the rotation has spare
// capacity AND the item isn't already assigned, append it. Idempotent.
// Off by default per Souls convention (assignment is deliberate).
void tryAutoAssignOnGrant(const std::string& config_path);

// Convenience: the currently-primed item's config_path, or empty
// string if no profile / empty rotation / primed_index invalid.
std::string primedConfigPath();

} // namespace selva::combat
