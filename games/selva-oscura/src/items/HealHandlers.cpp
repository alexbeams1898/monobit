#include "items/HealHandlers.h"

#include "AppStateGlobal.h"
#include "Formulas.h"
#include "ecs/Items.h"
#include "gameplay/Actor.h"
#include "items/ItemRegistry.h"
#include "items/UseHandlers.h"
#include "ops/InventoryOps.h"

#include <algorithm>
#include <cstdio>
#include <string>

namespace selva::items
{

namespace
{

// Item config_paths -> tier %. Returns 0.0 for unknown paths (the
// handler then no-ops gracefully).
float tierPctFor(const std::string& config_path)
{
    const auto& heal = selva::formulas::current().heal;
    if (config_path == "config/items/consumables/poultice.json")
        return heal.poultice_pct;
    if (config_path == "config/items/consumables/salve.json")
        return heal.salve_pct;
    if (config_path == "config/items/consumables/electuary.json")
        return heal.electuary_pct;
    if (config_path == "config/items/consumables/theriac.json")
        return heal.theriac_pct;
    return 0.0f;
}

// Apply tier % of max HP to the player. Caller handles consumption +
// the inventory mutation. Returns the actual amount healed (clamped
// at max-current).
int applyHealPct(float pct)
{
    auto& p = selva::gameplay::player();
    const int max_hp = p.hp.max;
    const int before = p.hp.current;
    const int desired = static_cast<int>(static_cast<float>(max_hp) * pct + 0.5f);
    const int after = std::min(before + desired, max_hp);
    p.hp.current = after;
    return after - before;
}

} // namespace

UseResult healAction(engine::ecs::Inventory& inv, engine::ecs::ItemInstanceId item_id)
{
    UseResult r;
    selva::PlayerProfile* profile = selva::activePlayerProfile();
    if (profile == nullptr)
    {
        r.rejection_reason = "No active character.";
        return r;
    }
    const engine::ecs::ItemInstance* inst = engine::ops::inventory::findById(inv, item_id);
    if (inst == nullptr)
    {
        r.rejection_reason = "Item not found.";
        return r;
    }
    const float pct = tierPctFor(inst->config_path);
    if (pct <= 0.0f)
    {
        r.rejection_reason = "This item has no heal value.";
        return r;
    }
    // Refuse-to-waste: full HP rejects with a player-facing reason.
    // Action does NOT consume the item; the caller honors fired=false.
    const auto& player = selva::gameplay::player();
    if (player.hp.current >= player.hp.max)
    {
        r.rejection_reason = "Already at full health.";
        return r;
    }

    const int healed = applyHealPct(pct);
    std::fprintf(stderr, "[heal] %s -> +%d hp (cap %d/%d)\n", inst->config_path.c_str(), healed,
                 selva::gameplay::player().hp.current, selva::gameplay::player().hp.max);
    std::fflush(stderr);

    // Route through itemDisplayName so "Used Fine Poultice" carries
    // the quality stamp the player saw in the scrip / inventory.
    const std::string name = selva::items::itemDisplayName(*inst, selva::items::itemRegistry());
    r.fired = true;
    r.success_message = "Used " + name;
    return r;
}

void registerHealHandlers()
{
    registerUseAction("heal_consumable", healAction);
}

} // namespace selva::items
