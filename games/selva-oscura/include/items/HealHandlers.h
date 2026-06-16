#pragma once

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

} // namespace selva::items
