// Tests for selva::combat::QuickSlot ops that mutate an active
// PlayerProfile (setQuickSlot, getQuickSlot, cyclePrimed,
// isAssignedToQuickSlot). The pure cycle math is covered by
// hand_cycle_test; these exercise the active-profile-mutating
// wrappers via the ActiveProfileScope harness.

#include "AppState.h"
#include "AppStateGlobal.h"
#include "combat/HandCycle.h"
#include "combat/QuickSlot.h"
#include "test_helpers.h"

#include <catch2/catch_test_macros.hpp>

using selva::combat::CycleDirection;
using selva::combat::cyclePrimed;
using selva::combat::getQuickSlot;
using selva::combat::isAssignedToQuickSlot;
using selva::combat::setQuickSlot;

TEST_CASE("setQuickSlot at index 0 with empty rotation populates slot + primes it",
          "[combat][quick-slot]")
{
    selva::tests::ActiveProfileScope scope{"PILGRIM"};
    REQUIRE(setQuickSlot(0, "config/items/consumables/poultice.json"));
    REQUIRE(getQuickSlot(0) == "config/items/consumables/poultice.json");
    REQUIRE(scope.profile().quick_slot_primed_index == 0);
}

TEST_CASE("setQuickSlot pads with empty entries when index > current size", "[combat][quick-slot]")
{
    selva::tests::ActiveProfileScope scope{"PILGRIM"};
    // Skip to slot 3 with empty rotation. setQuickSlot should pad
    // slots 0..2 with empty so the vector has size 4 with non-empty
    // only at index 3 -- BUT the impl tidies trailing empties from
    // the BACK, so slots 0..2 stay as empty strings while slot 3
    // sits at the end.
    REQUIRE(setQuickSlot(3, "config/items/consumables/salve.json"));
    REQUIRE(getQuickSlot(3) == "config/items/consumables/salve.json");
    REQUIRE(getQuickSlot(0).empty());
    REQUIRE(getQuickSlot(1).empty());
    REQUIRE(getQuickSlot(2).empty());
    REQUIRE(scope.profile().quick_slot_assigned.size() == 4);
}

TEST_CASE("setQuickSlot tidies trailing empties when last slot cleared", "[combat][quick-slot]")
{
    selva::tests::ActiveProfileScope scope{"PILGRIM"};
    REQUIRE(setQuickSlot(0, "config/items/consumables/poultice.json"));
    REQUIRE(setQuickSlot(1, "config/items/consumables/salve.json"));
    REQUIRE(scope.profile().quick_slot_assigned.size() == 2);
    // Clear slot 1 -- now the LAST slot. Tidying should pop it,
    // shrinking size to 1.
    REQUIRE(setQuickSlot(1, std::string{}));
    REQUIRE(scope.profile().quick_slot_assigned.size() == 1);
    REQUIRE(getQuickSlot(0) == "config/items/consumables/poultice.json");
}

TEST_CASE("setQuickSlot preserves layout when middle slot cleared", "[combat][quick-slot]")
{
    selva::tests::ActiveProfileScope scope{"PILGRIM"};
    REQUIRE(setQuickSlot(0, "config/items/consumables/poultice.json"));
    REQUIRE(setQuickSlot(1, "config/items/consumables/salve.json"));
    REQUIRE(setQuickSlot(2, "config/items/consumables/electuary.json"));
    // Clear slot 1 (middle). Slot 2's electuary stays at index 2 --
    // the player's "Electuary is my third slot" mental model
    // survives. Size stays 3.
    REQUIRE(setQuickSlot(1, std::string{}));
    REQUIRE(scope.profile().quick_slot_assigned.size() == 3);
    REQUIRE(getQuickSlot(0) == "config/items/consumables/poultice.json");
    REQUIRE(getQuickSlot(1).empty());
    REQUIRE(getQuickSlot(2) == "config/items/consumables/electuary.json");
}

TEST_CASE("setQuickSlot rejects duplicate assignment of same config to different slot",
          "[combat][quick-slot]")
{
    selva::tests::ActiveProfileScope scope{"PILGRIM"};
    REQUIRE(setQuickSlot(0, "config/items/consumables/poultice.json"));
    REQUIRE_FALSE(setQuickSlot(1, "config/items/consumables/poultice.json"));
    // Slot 1 stays empty; slot 0 unchanged.
    REQUIRE(getQuickSlot(0) == "config/items/consumables/poultice.json");
    REQUIRE(getQuickSlot(1).empty());
}

TEST_CASE("setQuickSlot rejects out-of-range index", "[combat][quick-slot]")
{
    selva::tests::ActiveProfileScope scope{"PILGRIM"};
    REQUIRE_FALSE(setQuickSlot(-1, "config/items/consumables/poultice.json"));
    REQUIRE_FALSE(
        setQuickSlot(selva::kQuickSlotCapacity, "config/items/consumables/poultice.json"));
    REQUIRE(scope.profile().quick_slot_assigned.empty());
}

TEST_CASE("isAssignedToQuickSlot returns true only when path is in the rotation",
          "[combat][quick-slot]")
{
    selva::tests::ActiveProfileScope scope{"PILGRIM"};
    setQuickSlot(0, "config/items/consumables/poultice.json");
    REQUIRE(isAssignedToQuickSlot("config/items/consumables/poultice.json"));
    REQUIRE_FALSE(isAssignedToQuickSlot("config/items/consumables/salve.json"));
}

TEST_CASE("cyclePrimed wraps modulo rotation size", "[combat][quick-slot][cycle]")
{
    selva::tests::ActiveProfileScope scope{"PILGRIM"};
    setQuickSlot(0, "config/items/consumables/poultice.json");
    setQuickSlot(1, "config/items/consumables/salve.json");
    setQuickSlot(2, "config/items/consumables/electuary.json");
    auto& profile = scope.profile();
    profile.quick_slot_primed_index = 0;

    REQUIRE(cyclePrimed(CycleDirection::Forward) == 1);
    REQUIRE(cyclePrimed(CycleDirection::Forward) == 2);
    REQUIRE(cyclePrimed(CycleDirection::Forward) == 0);  // wrapped
    REQUIRE(cyclePrimed(CycleDirection::Backward) == 2); // wrapped backward
}

TEST_CASE("cyclePrimed returns -1 with empty rotation", "[combat][quick-slot][cycle]")
{
    selva::tests::ActiveProfileScope scope{"PILGRIM"};
    REQUIRE(cyclePrimed(CycleDirection::Forward) == -1);
    REQUIRE(scope.profile().quick_slot_primed_index == -1);
}

TEST_CASE("cyclePrimed forward from -1 lands on first slot", "[combat][quick-slot][cycle]")
{
    selva::tests::ActiveProfileScope scope{"PILGRIM"};
    setQuickSlot(0, "config/items/consumables/poultice.json");
    setQuickSlot(1, "config/items/consumables/salve.json");
    // setQuickSlot at index 0 auto-primes; reset to -1 to test the
    // cold-start path.
    scope.profile().quick_slot_primed_index = -1;
    REQUIRE(cyclePrimed(CycleDirection::Forward) == 0);
}

TEST_CASE("setQuickSlot empty path on never-touched slot is a no-op-but-success",
          "[combat][quick-slot]")
{
    selva::tests::ActiveProfileScope scope{"PILGRIM"};
    REQUIRE(setQuickSlot(2, std::string{}));
    // Trailing-empty tidying means assigned ends up empty -- the
    // padding we added at index 0..2 was all empty strings and they
    // all got trimmed from the back.
    REQUIRE(scope.profile().quick_slot_assigned.empty());
    REQUIRE(scope.profile().quick_slot_primed_index == -1);
}
