#include "combat/QuickSlot.h"

#include "AppState.h"
#include "AppStateGlobal.h"
#include "combat/HandCycle.h"
#include "items/ItemRegistry.h"
#include "items/UseHandlers.h"
#include "ops/InventoryOps.h"

#include <algorithm>
#include <cstdio>

namespace selva::combat
{

bool assignToQuickSlot(const std::string& config_path)
{
    selva::PlayerProfile* profile = selva::activePlayerProfile();
    if (profile == nullptr || config_path.empty())
        return false;
    auto& assigned = profile->quick_slot_assigned;
    if (std::find(assigned.begin(), assigned.end(), config_path) != assigned.end())
        return false;
    if (static_cast<int>(assigned.size()) >= selva::kQuickSlotCapacity)
        return false;
    assigned.push_back(config_path);
    if (profile->quick_slot_primed_index < 0)
        profile->quick_slot_primed_index = 0;
    return true;
}

bool unassignFromQuickSlot(const std::string& config_path)
{
    selva::PlayerProfile* profile = selva::activePlayerProfile();
    if (profile == nullptr)
        return false;
    auto& assigned = profile->quick_slot_assigned;
    const auto it = std::find(assigned.begin(), assigned.end(), config_path);
    if (it == assigned.end())
        return false;
    const int removed_idx = static_cast<int>(std::distance(assigned.begin(), it));
    assigned.erase(it);
    // Adjust primed_index: if the rotation is now empty, -1. If we
    // removed the entry at or before the primed index, decrement to
    // keep the SAME logical entry primed (or 0 if removed_idx was 0
    // and there's still something there). Clamp to new size.
    if (assigned.empty())
    {
        profile->quick_slot_primed_index = -1;
    }
    else if (removed_idx <= profile->quick_slot_primed_index)
    {
        profile->quick_slot_primed_index = std::max(0, profile->quick_slot_primed_index - 1);
        if (profile->quick_slot_primed_index >= static_cast<int>(assigned.size()))
            profile->quick_slot_primed_index = static_cast<int>(assigned.size()) - 1;
    }
    return true;
}

bool isAssignedToQuickSlot(const std::string& config_path)
{
    const selva::PlayerProfile* profile = selva::activePlayerProfile();
    if (profile == nullptr)
        return false;
    const auto& a = profile->quick_slot_assigned;
    return std::find(a.begin(), a.end(), config_path) != a.end();
}

int cyclePrimed(CycleDirection direction)
{
    selva::PlayerProfile* profile = selva::activePlayerProfile();
    if (profile == nullptr)
        return -1;
    const int n = static_cast<int>(profile->quick_slot_assigned.size());
    if (n == 0)
        return -1;
    const int step = (direction == CycleDirection::Forward) ? 1 : -1;
    // Treat -1 (unset) as 0 for forward / n-1 for backward.
    int from = profile->quick_slot_primed_index;
    if (from < 0)
        from = (step > 0) ? -1 : 0; // forward -> 0, backward -> n-1 via wrap below
    const int next = ((from + step) % n + n) % n;
    profile->quick_slot_primed_index = next;
    return next;
}

std::string primedConfigPath()
{
    const selva::PlayerProfile* profile = selva::activePlayerProfile();
    if (profile == nullptr)
        return std::string{};
    const auto& a = profile->quick_slot_assigned;
    const int idx = profile->quick_slot_primed_index;
    if (idx < 0 || idx >= static_cast<int>(a.size()))
        return std::string{};
    return a[static_cast<std::size_t>(idx)];
}

bool primedHasInstance()
{
    const selva::PlayerProfile* profile = selva::activePlayerProfile();
    if (profile == nullptr)
        return false;
    const std::string path = primedConfigPath();
    if (path.empty())
        return false;
    return engine::ops::inventory::countItem(profile->inventory, path) > 0;
}

void tryAutoAssignOnGrant(const std::string& config_path)
{
    if (config_path.empty())
        return;
    if (!selva::saveData().settings.auto_assign_consumables_to_quick_slot)
        return;
    const auto& items = selva::items::itemRegistry();
    const engine::ecs::ItemDef* def = items.find(config_path);
    if (def == nullptr || def->category != engine::ecs::ItemCategory::Consumable)
        return;
    // assignToQuickSlot's own guards handle the "already assigned" +
    // "capacity reached" cases; this just narrows to consumables.
    assignToQuickSlot(config_path);
}

bool useprimed()
{
    selva::PlayerProfile* profile = selva::activePlayerProfile();
    if (profile == nullptr)
        return false;
    const std::string path = primedConfigPath();
    if (path.empty())
        return false;

    // Find an instance of this config_path in the inventory.
    auto& inv = profile->inventory;
    engine::ecs::ItemInstanceId target_id = engine::ecs::kInvalidItemInstanceId;
    for (const auto& [cat, bucket] : inv.by_category)
    {
        for (const auto& it : bucket)
        {
            if (it.config_path == path && it.quantity > 0)
            {
                target_id = it.id;
                break;
            }
        }
        if (target_id != engine::ecs::kInvalidItemInstanceId)
            break;
    }
    if (target_id == engine::ecs::kInvalidItemInstanceId)
        return false;

    // useItem handles handler dispatch, consume-on-fired, and the
    // success/rejection toast. Same orchestration as the inventory
    // Use button so both surfaces give identical feedback.
    const auto result = selva::items::useItem(inv, target_id);
    return result.fired;
}

} // namespace selva::combat
