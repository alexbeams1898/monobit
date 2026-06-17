#pragma once

#include "items/UseHandlers.h"

// Heal-consumable Use handler. Registers the "heal_consumable" key
// with selva::items::registerUseAction so that any ItemDef declaring
// `use_handler: "heal_consumable"` (today: Poultice / Salve /
// Electuary / Theriac per [[project_healing_system_locked_2026_06_14]])
// applies a tier-appropriate % of max HP when the Use button fires.
//
// The handler dispatches on the consumed item's config_path -- the
// engine layer's ItemInstance carries that path verbatim. Per-tier
// heal percentages live in formulas.json (heal.poultice_pct etc).
//
// Use surfaces: the inventory Use button + the quick-slot Q hotkey.
// Both route through the generic selva::items::UseHandlers pipeline;
// HealHandlers just registers the "heal_consumable" key.

namespace selva::items
{

// Boot-time: register the "heal_consumable" Use-action handler. Call
// once after registerUseAction is available. The heal action is
// dispatched by the generic UseHandlers pipeline (inventory Use
// button + quick-slot Q press); HealHandlers no longer owns the
// hotkey entrypoint.

void registerHealHandlers();

// Exposed for tests: the use_action body called when "heal_consumable"
// fires. Production code reaches it via selva::items::useItem ->
// UseActionFn registry dispatch and SHOULD NOT call this directly.
// Reads player().hp + writes player().hp; does NOT consume the item
// (the caller does, on fired=true).
UseResult healAction(engine::ecs::Inventory& inv, engine::ecs::ItemInstanceId item_id);

} // namespace selva::items
