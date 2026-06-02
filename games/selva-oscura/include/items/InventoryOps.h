#pragma once

#include "items/Inventory.h"

#include <string>

namespace selva::items
{

// Possession items. Idempotent for already-owned. Returns true if
// newly granted, false if already owned. Asserts the item's kind is
// Possession.
bool grant(Inventory& inv, const std::string& item_id);

// Stack items. Returns the new total count for this stack (after the
// add). Creates the stack if absent. Asserts the item's kind is Stack.
int addStack(Inventory& inv, const std::string& item_id, int amount = 1);

// Stack items. Returns the new total count (after the remove). Removes
// the stack entry entirely if count drops to 0. Returns -1 if the
// stack doesn't exist. Asserts the item's kind is Stack.
int removeStack(Inventory& inv, const std::string& item_id, int amount = 1);

// Instanced items. Appends a new instance to the category. Caller
// receives a reference to mutate the per-instance state (upgrade_level,
// future fields). Asserts the item's kind is Instanced.
InstancedEntry& addInstanced(Inventory& inv, const std::string& item_id);

// Universal: does the player have at least one of this item? Works
// across all three kinds.
bool has(const Inventory& inv, const std::string& item_id);

// Universal: remove the first entry matching item_id. For Possession
// removes the entry. For Stack removes one count (or the whole stack if
// count == 1). For Instanced removes the first instance. Returns true
// if an entry was removed.
bool remove(Inventory& inv, const std::string& item_id);

// Iteration helper: yield all entries in a category, in insertion
// order. Returns nullptr if the category has no entries (or doesn't
// exist in this inventory).
const std::vector<Entry>* entriesIn(const Inventory& inv,
                                    const std::string& category_id);

} // namespace selva::items
