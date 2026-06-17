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

// Set the entry at slot `index` to `config_path`. Pads the rotation
// with empty entries up to `index` if needed (so the player can
// assign to slot 4 even if slots 0..3 are unset). To clear a slot,
// pass an empty config_path -- the slot is set to empty string and
// shifts down only if it's the LAST slot (otherwise the layout
// stays stable so the player's mental "slot 0 = poultice" survives).
//
// Returns true on successful write. Returns false if:
//   - no active profile
//   - index < 0 or index >= kQuickSlotCapacity
//   - config_path is already assigned at a different index (we don't
//     allow the same item in two slots; rejecting is safer than the
//     auto-clear-old policy a player might not expect)
//
// If config_path becomes assigned at index AND primed_index was -1,
// primed_index advances to that index.
bool setQuickSlot(int index, const std::string& config_path);

// Read the entry at slot `index`. Returns empty string for an empty
// slot, an out-of-range index, or no active profile.
std::string getQuickSlot(int index);

// Slot-emptying intent note: there is NO automatic unassign when a
// stack runs out (intentional, mirroring Souls). The slot stays
// assigned -- the combat HUD shows the count as zero / dim so the
// player knows their loadout intent persists between crafts. The
// only path to remove an item from a scrip slot is the Borne
// sub-page's per-slot "(None)" picker, which routes through
// setQuickSlot(idx, "").

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
