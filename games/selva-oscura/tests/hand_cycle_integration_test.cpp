// Integration tests for selva::combat::cycleHand against a real
// active profile. The pure cycle math (nextCyclePosition) is covered
// by hand_cycle_test; this exercises the OTHER-HAND-EXCLUDE filter +
// the live equip slot mutation, which the pure math layer doesn't
// see.

#include "AppState.h"
#include "AppStateGlobal.h"
#include "combat/HandCycle.h"
#include "ecs/Items.h"
#include "items/ItemRegistry.h"
#include "ops/InventoryOps.h"
#include "test_helpers.h"

#include <string>

#include <catch2/catch_test_macros.hpp>

namespace
{

// Register a weapon ItemDef into the live items registry. Idempotent
// across test runs (registry overwrites by config_path).
void registerWeapon(const std::string& path, const std::string& name)
{
    engine::ecs::ItemDef def;
    def.config_path = path;
    def.name = name;
    def.category = engine::ecs::ItemCategory::Weapon;
    def.stackable = false;
    def.max_stack = 1;
    selva::items::itemRegistry().defs[path] = def;
}

// Add a weapon instance to the active profile's inventory; returns
// its assigned id.
engine::ecs::ItemInstanceId addWeapon(selva::PlayerProfile& profile, const std::string& path)
{
    engine::ecs::ItemInstance inst;
    inst.config_path = path;
    inst.quantity = 1;
    return engine::ops::inventory::addItem(profile.inventory, inst, selva::items::itemRegistry());
}

} // namespace

TEST_CASE("cycleHand forward from empty lands on first carried weapon",
          "[combat][hand-cycle][integration]")
{
    selva::tests::ActiveProfileScope scope{"PILGRIM"};
    auto& profile = scope.profile();
    registerWeapon("config/items/weapons/test_mace.json", "Test Mace");
    const auto mace_id = addWeapon(profile, "config/items/weapons/test_mace.json");

    const auto landed = selva::combat::cycleHand(engine::ecs::EquipSlot::RightHand,
                                                 selva::combat::CycleDirection::Forward);
    REQUIRE(landed == mace_id);
    REQUIRE(profile.equipment.right_hand == mace_id);
}

TEST_CASE("cycleHand wraps to empty after stepping past last weapon",
          "[combat][hand-cycle][integration]")
{
    selva::tests::ActiveProfileScope scope{"PILGRIM"};
    auto& profile = scope.profile();
    registerWeapon("config/items/weapons/test_mace.json", "Test Mace");
    const auto mace_id = addWeapon(profile, "config/items/weapons/test_mace.json");

    // Start with mace equipped; forward step should land on empty.
    profile.equipment.right_hand = mace_id;
    const auto landed = selva::combat::cycleHand(engine::ecs::EquipSlot::RightHand,
                                                 selva::combat::CycleDirection::Forward);
    REQUIRE(landed == engine::ecs::kInvalidItemInstanceId);
    REQUIRE(profile.equipment.right_hand == engine::ecs::kInvalidItemInstanceId);
}

TEST_CASE("cycleHand on left hand EXCLUDES item already in right hand",
          "[combat][hand-cycle][integration][exclude-other]")
{
    selva::tests::ActiveProfileScope scope{"PILGRIM"};
    auto& profile = scope.profile();

    registerWeapon("config/items/weapons/test_mace.json", "Test Mace");
    registerWeapon("config/items/weapons/test_knife.json", "Test Knife");
    const auto mace_id = addWeapon(profile, "config/items/weapons/test_mace.json");
    const auto knife_id = addWeapon(profile, "config/items/weapons/test_knife.json");

    // Mace in right hand; left hand starts empty. Cycling left
    // forward should land on the knife (the ONLY non-other-hand
    // weapon), NOT silently swap the mace over to the left hand.
    profile.equipment.right_hand = mace_id;
    const auto landed = selva::combat::cycleHand(engine::ecs::EquipSlot::LeftHand,
                                                 selva::combat::CycleDirection::Forward);
    REQUIRE(landed == knife_id);
    REQUIRE(profile.equipment.left_hand == knife_id);
    // CRITICAL: right hand still holds mace; the cycle didn't steal
    // it away.
    REQUIRE(profile.equipment.right_hand == mace_id);
}

TEST_CASE("cycleHand wraps back to empty when only candidate is in other hand",
          "[combat][hand-cycle][integration][exclude-other]")
{
    selva::tests::ActiveProfileScope scope{"PILGRIM"};
    auto& profile = scope.profile();
    registerWeapon("config/items/weapons/test_mace.json", "Test Mace");
    const auto mace_id = addWeapon(profile, "config/items/weapons/test_mace.json");

    // Mace in right; only weapon. Cycling LEFT forward has zero
    // candidates after the exclude-other filter -> cycle returns
    // empty (kInvalid) and left stays empty.
    profile.equipment.right_hand = mace_id;
    const auto landed = selva::combat::cycleHand(engine::ecs::EquipSlot::LeftHand,
                                                 selva::combat::CycleDirection::Forward);
    REQUIRE(landed == engine::ecs::kInvalidItemInstanceId);
    REQUIRE(profile.equipment.left_hand == engine::ecs::kInvalidItemInstanceId);
    REQUIRE(profile.equipment.right_hand == mace_id);
}

TEST_CASE("cycleHand backward steps from empty to last candidate (excluding other hand)",
          "[combat][hand-cycle][integration]")
{
    selva::tests::ActiveProfileScope scope{"PILGRIM"};
    auto& profile = scope.profile();
    registerWeapon("config/items/weapons/test_mace.json", "Test Mace");
    registerWeapon("config/items/weapons/test_knife.json", "Test Knife");
    registerWeapon("config/items/weapons/test_staff.json", "Test Staff");
    addWeapon(profile, "config/items/weapons/test_mace.json");
    const auto knife_id = addWeapon(profile, "config/items/weapons/test_knife.json");
    const auto staff_id = addWeapon(profile, "config/items/weapons/test_staff.json");

    // No other-hand exclusion (right is empty). Backward from empty
    // lands on last candidate. Insertion order is mace -> knife ->
    // staff; backward from empty is staff. But: inventory iteration
    // ORDER over by_category is map-iteration (unordered_map), so
    // the exact "last" candidate depends on hash. We can only assert
    // it's NOT kInvalid and IS one of the three weapons we just
    // added. A stronger order assertion would require a sorted
    // collectItemsFittingSlot; not the contract.
    const auto landed = selva::combat::cycleHand(engine::ecs::EquipSlot::RightHand,
                                                 selva::combat::CycleDirection::Backward);
    REQUIRE(landed != engine::ecs::kInvalidItemInstanceId);
    REQUIRE((landed == knife_id || landed == staff_id ||
             landed == profile.inventory.next_id - 3 /* mace id */));
}
