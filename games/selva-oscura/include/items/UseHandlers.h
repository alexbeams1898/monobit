#pragma once

#include "items/Inventory.h"

#include <functional>
#include <string>

namespace selva::items
{

// Returned by a use-condition handler. Empty disabled_reason + enabled=true
// = the Use button is active. Otherwise the button renders disabled and
// disabled_reason is shown as a tooltip.
struct UseGate
{
    bool enabled = true;
    std::string disabled_reason;
};

// Use-action handler: called when the player triggers Use on an item.
// Owns all side effects -- consuming the item (via remove()), setting
// profile flags, opening downstream UI, firing scripted events.
using UseActionFn = std::function<void(Inventory& inv, const std::string& item_id)>;

// Use-condition handler: called every UI frame to decide whether the
// Use button is active. Returning enabled=false renders the button
// disabled with disabled_reason as tooltip.
using UseConditionFn = std::function<UseGate()>;

// Register a handler by string key. Boot-time; called once before any
// item JSON loads so item-defs can reference these keys safely.
// Overwriting an existing key logs + replaces.
void registerUseAction(const std::string& key, UseActionFn fn);
void registerUseCondition(const std::string& key, UseConditionFn fn);

// Lookup. Returns nullptr if the key isn't registered. Inventory UI
// uses these to drive the Use button per selected item.
const UseActionFn* getUseAction(const std::string& key);
const UseConditionFn* getUseCondition(const std::string& key);

} // namespace selva::items
