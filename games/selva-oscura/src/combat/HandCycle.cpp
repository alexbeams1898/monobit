#include "combat/HandCycle.h"

#include "AppState.h"
#include "AppStateGlobal.h"
#include "items/ItemRegistry.h"
#include "ops/InventoryOps.h"

#include <tracy/Tracy.hpp>

namespace selva::combat
{

engine::ecs::ItemInstanceId
nextCyclePosition(const std::vector<engine::ecs::ItemInstanceId>& candidate_ids,
                  engine::ecs::ItemInstanceId current_id, CycleDirection direction)
{
    // Cycle order: [empty, candidate_0, candidate_1, ...]. Empty sits
    // at index 0 so backward-from-first lands on empty (intuitive:
    // "unequip is one step left of the first weapon").
    const int n_positions = static_cast<int>(candidate_ids.size()) + 1;
    if (n_positions <= 1)
        return engine::ecs::kInvalidItemInstanceId;

    int current_pos = 0;
    if (current_id != engine::ecs::kInvalidItemInstanceId)
    {
        for (int i = 0; i < static_cast<int>(candidate_ids.size()); ++i)
        {
            if (candidate_ids[i] == current_id)
            {
                current_pos = i + 1;
                break;
            }
        }
        // current_id not in candidate_ids (stale): fall through with
        // current_pos = 0 so the next forward step lands on
        // candidate_ids[0].
    }

    const int step = (direction == CycleDirection::Forward) ? 1 : -1;
    const int next_pos = ((current_pos + step) % n_positions + n_positions) % n_positions;

    if (next_pos == 0)
        return engine::ecs::kInvalidItemInstanceId;
    return candidate_ids[next_pos - 1];
}

engine::ecs::ItemInstanceId cycleHand(engine::ecs::EquipSlot slot, CycleDirection direction)
{
    // Perf-trace correlation: weapon swap fires here. Pair with
    // spike zones in Tracy to identify whether first-of-kind
    // weapon setup (mesh/material/etc) is causing render hitches.
    TracyMessageL("cycleHand");
    selva::PlayerProfile* profile = selva::activePlayerProfile();
    if (profile == nullptr)
        return engine::ecs::kInvalidItemInstanceId;

    auto& inv = profile->inventory;
    auto& eq = profile->equipment;
    const auto& items = selva::items::itemRegistry();

    const auto fitting = selva::items::collectItemsFittingSlot(inv, items, slot);

    // Exclude items currently equipped in the OTHER hand slot from the
    // cycle. Without this, cycling left-hand to an item already in the
    // right hand would AUTO-MOVE it to the left (because
    // equipItemToSlot enforces one-instance-one-slot by auto-
    // unequipping from the prior slot). Player expects the cycle to
    // show only items they could pick up with THIS hand right now,
    // not silently rearrange the other hand.
    const engine::ecs::EquipSlot other_hand = (slot == engine::ecs::EquipSlot::LeftHand)
                                                  ? engine::ecs::EquipSlot::RightHand
                                                  : engine::ecs::EquipSlot::LeftHand;
    const engine::ecs::ItemInstanceId other_hand_id =
        engine::ops::inventory::slotIdConst(eq, other_hand);

    std::vector<engine::ecs::ItemInstanceId> candidate_ids;
    candidate_ids.reserve(fitting.size());
    for (const auto* p : fitting)
    {
        if (p->id == other_hand_id)
            continue;
        candidate_ids.push_back(p->id);
    }

    const engine::ecs::ItemInstanceId current_id = engine::ops::inventory::slotIdConst(eq, slot);
    const engine::ecs::ItemInstanceId new_id =
        nextCyclePosition(candidate_ids, current_id, direction);

    if (new_id == engine::ecs::kInvalidItemInstanceId)
        engine::ops::inventory::unequipSlot(eq, slot);
    else
        engine::ops::inventory::equipItemToSlot(inv, eq, new_id, slot);
    return new_id;
}

} // namespace selva::combat
