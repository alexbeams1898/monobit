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
// Q hotkey: drives the same path, picking the highest-known tier the
// player has at least one of. Wired separately from the inventory UI
// path so the hotkey works without opening the menu.

namespace selva::items
{

// Boot-time: register the "heal_consumable" Use-action handler. Call
// once after registerUseAction is available.
void registerHealHandlers();

// Q-hotkey entrypoint. Picks the highest-tier heal consumable the
// player has (in ladder order Theriac > Electuary > Salve > Poultice)
// and applies its heal, consuming one. No-op if no heal is held.
// Returns true if a heal was applied.
bool tryQuickHeal();

} // namespace selva::items
