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

bool setQuickSlot(int index, const std::string& config_path)
{
    selva::PlayerProfile* profile = selva::activePlayerProfile();
    if (profile == nullptr)
        return false;
    if (index < 0 || index >= selva::kQuickSlotCapacity)
        return false;
    auto& assigned = profile->quick_slot_assigned;

    // Reject if this item is already assigned at a DIFFERENT index.
    if (!config_path.empty())
    {
        for (int i = 0; i < static_cast<int>(assigned.size()); ++i)
        {
            if (i != index && assigned[i] == config_path)
                return false;
        }
    }

    // Pad with empty strings up to index if the rotation is shorter.
    while (static_cast<int>(assigned.size()) <= index)
        assigned.emplace_back();

    assigned[static_cast<std::size_t>(index)] = config_path;

    // Tidy trailing-empty entries so the rotation reflects the
    // highest occupied slot. The vector is "slot index = vector
    // index" up to its size; trailing empties carry no information.
    while (!assigned.empty() && assigned.back().empty())
        assigned.pop_back();

    // primed_index housekeeping: if nothing was primed and we just
    // populated something, prime this index. If primed_index now
    // points past the (possibly shrunken) end, snap to the new last
    // entry or -1.
    if (!config_path.empty() && profile->quick_slot_primed_index < 0)
        profile->quick_slot_primed_index = index;
    if (profile->quick_slot_primed_index >= static_cast<int>(assigned.size()))
        profile->quick_slot_primed_index =
            assigned.empty() ? -1 : static_cast<int>(assigned.size()) - 1;
    return true;
}

std::string getQuickSlot(int index)
{
    const selva::PlayerProfile* profile = selva::activePlayerProfile();
    if (profile == nullptr)
        return std::string{};
    const auto& a = profile->quick_slot_assigned;
    if (index < 0 || index >= static_cast<int>(a.size()))
        return std::string{};
    return a[static_cast<std::size_t>(index)];
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
