#pragma once

#include "ecs/Items.h"

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

// Outcome reported by a use-action. Tells the caller whether the
// action actually fired (so it knows whether to consume the item +
// what to toast):
//   fired = true  -> action took effect; CALLER consumes the item;
//                    if success_message is non-empty, toast it.
//   fired = false -> action rejected; CALLER does NOT consume the
//                    item; if rejection_reason is non-empty, toast
//                    it as a warning. Use this for "already at full
//                    HP" / "not poisoned, can't cure" / etc.
//
// Per design lock 2026-06-15: rejection is hard (item not consumed),
// matching Souls' "you can't waste a flask at full HP" convention.
struct UseResult
{
    bool fired = false;
    std::string rejection_reason;
    std::string success_message;
};

// Use-action handler: called when the player triggers Use on an item.
// Owns the SIDE EFFECT (apply HP, set a flag, etc.) but does NOT
// consume the item -- the caller does that based on the returned
// UseResult.fired. This split lets a handler reject ("already full
// HP") without burning the item.
//
// `inv` is the active character's engine inventory. `item_id` is the
// ItemInstance::id of the selected item -- look up via
// engine::ops::inventory::findById for read.
using UseActionFn = std::function<UseResult(engine::ecs::Inventory& inv,
                                            engine::ecs::ItemInstanceId item_id)>;

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

// One-shot Use orchestration: resolves the handler for the item, fires
// it, consumes one stack on fired=true, and pushes a toast for either
// the success_message OR the rejection_reason (if non-empty). Returns
// the UseResult so callers can do additional work (e.g. clearing the
// selected-item id when fired). Used by the inventory detail panel
// Use button AND the quick-slot Q press so both surfaces produce
// identical feedback (toast color, consume policy).
//
// `inv` must be the active profile's inventory; orchestration uses
// the active profile's equipment for stack-removal slot-clearing.
UseResult useItem(engine::ecs::Inventory& inv, engine::ecs::ItemInstanceId item_id);

} // namespace selva::items
