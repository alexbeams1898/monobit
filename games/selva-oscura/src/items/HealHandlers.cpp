#include "items/HealHandlers.h"

#include "AppStateGlobal.h"
#include "Formulas.h"
#include "ecs/Items.h"
#include "gameplay/Actor.h"
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

// Ladder order, highest first. Q-hotkey walks this list and uses the
// first one in inventory.
const char* const kHealLadder[] = {
    "config/items/consumables/theriac.json",
    "config/items/consumables/electuary.json",
    "config/items/consumables/salve.json",
    "config/items/consumables/poultice.json",
};

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

void healAction(engine::ecs::Inventory& inv, engine::ecs::ItemInstanceId item_id)
{
    selva::PlayerProfile* profile = selva::activePlayerProfile();
    if (profile == nullptr)
        return;
    const engine::ecs::ItemInstance* inst = engine::ops::inventory::findById(inv, item_id);
    if (inst == nullptr)
        return;
    const float pct = tierPctFor(inst->config_path);
    if (pct <= 0.0f)
        return;
    const int healed = applyHealPct(pct);
    // Consume one of this item. `consumeItems` also handles stack
    // decrement / removal cleanly.
    engine::ops::inventory::consumeItems(inv, profile->equipment, inst->config_path, 1);
    std::fprintf(stderr, "[heal] %s -> +%d hp (cap %d/%d)\n", inst->config_path.c_str(), healed,
                 selva::gameplay::player().hp.current, selva::gameplay::player().hp.max);
    std::fflush(stderr);
}

} // namespace

void registerHealHandlers()
{
    registerUseAction("heal_consumable", healAction);
}

bool tryQuickHeal()
{
    selva::PlayerProfile* profile = selva::activePlayerProfile();
    if (profile == nullptr)
        return false;
    auto& inv = profile->inventory;
    // Walk ladder highest -> lowest; pick first the player holds.
    for (const char* path : kHealLadder)
    {
        if (engine::ops::inventory::countItem(inv, path) <= 0)
            continue;
        const float pct = tierPctFor(path);
        if (pct <= 0.0f)
            continue;
        const int healed = applyHealPct(pct);
        engine::ops::inventory::consumeItems(inv, profile->equipment, path, 1);
        std::fprintf(stderr, "[heal:quick] %s -> +%d hp\n", path, healed);
        std::fflush(stderr);
        return true;
    }
    return false;
}

} // namespace selva::items
