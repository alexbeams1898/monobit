// Tests for selva::combat::nextCyclePosition -- the pure cycle math
// behind the Z/C hand-cycle hotkeys. The wrapper cycleHand() depends
// on activePlayerProfile() global state; the math itself is tested
// here in isolation against synthetic candidate-id lists.

#include "combat/HandCycle.h"
#include "ecs/Items.h"

#include <catch2/catch_test_macros.hpp>

#include <vector>

using engine::ecs::ItemInstanceId;
using engine::ecs::kInvalidItemInstanceId;
using selva::combat::CycleDirection;
using selva::combat::nextCyclePosition;

TEST_CASE("nextCyclePosition: empty candidate list returns kInvalid",
          "[combat][hand-cycle]")
{
    const std::vector<ItemInstanceId> empty;
    REQUIRE(nextCyclePosition(empty, kInvalidItemInstanceId, CycleDirection::Forward) ==
            kInvalidItemInstanceId);
    REQUIRE(nextCyclePosition(empty, 42, CycleDirection::Forward) == kInvalidItemInstanceId);
    REQUIRE(nextCyclePosition(empty, kInvalidItemInstanceId, CycleDirection::Backward) ==
            kInvalidItemInstanceId);
}

TEST_CASE("nextCyclePosition: forward from empty steps to first item",
          "[combat][hand-cycle]")
{
    const std::vector<ItemInstanceId> ids = {10, 20, 30};
    REQUIRE(nextCyclePosition(ids, kInvalidItemInstanceId, CycleDirection::Forward) == 10);
}

TEST_CASE("nextCyclePosition: forward through the list and wraps to empty",
          "[combat][hand-cycle]")
{
    const std::vector<ItemInstanceId> ids = {10, 20, 30};
    REQUIRE(nextCyclePosition(ids, 10, CycleDirection::Forward) == 20);
    REQUIRE(nextCyclePosition(ids, 20, CycleDirection::Forward) == 30);
    REQUIRE(nextCyclePosition(ids, 30, CycleDirection::Forward) == kInvalidItemInstanceId);
    // Wrap: from empty back to first.
    REQUIRE(nextCyclePosition(ids, kInvalidItemInstanceId, CycleDirection::Forward) == 10);
}

TEST_CASE("nextCyclePosition: backward from empty steps to last item",
          "[combat][hand-cycle]")
{
    const std::vector<ItemInstanceId> ids = {10, 20, 30};
    REQUIRE(nextCyclePosition(ids, kInvalidItemInstanceId, CycleDirection::Backward) == 30);
}

TEST_CASE("nextCyclePosition: backward through the list and wraps to empty",
          "[combat][hand-cycle]")
{
    const std::vector<ItemInstanceId> ids = {10, 20, 30};
    REQUIRE(nextCyclePosition(ids, 30, CycleDirection::Backward) == 20);
    REQUIRE(nextCyclePosition(ids, 20, CycleDirection::Backward) == 10);
    REQUIRE(nextCyclePosition(ids, 10, CycleDirection::Backward) == kInvalidItemInstanceId);
}

TEST_CASE("nextCyclePosition: single-item list toggles between item and empty",
          "[combat][hand-cycle]")
{
    const std::vector<ItemInstanceId> ids = {42};
    REQUIRE(nextCyclePosition(ids, kInvalidItemInstanceId, CycleDirection::Forward) == 42);
    REQUIRE(nextCyclePosition(ids, 42, CycleDirection::Forward) == kInvalidItemInstanceId);
    REQUIRE(nextCyclePosition(ids, kInvalidItemInstanceId, CycleDirection::Backward) == 42);
    REQUIRE(nextCyclePosition(ids, 42, CycleDirection::Backward) == kInvalidItemInstanceId);
}

TEST_CASE("nextCyclePosition: stale current_id treated as empty",
          "[combat][hand-cycle]")
{
    // current_id 99 isn't in the candidate list (e.g. item was
    // consumed). Forward should land on the first candidate; backward
    // on the last.
    const std::vector<ItemInstanceId> ids = {10, 20, 30};
    REQUIRE(nextCyclePosition(ids, 99, CycleDirection::Forward) == 10);
    REQUIRE(nextCyclePosition(ids, 99, CycleDirection::Backward) == 30);
}

// The exclude-other-hand-from-cycle behavior is implemented in the
// cycleHand() wrapper (it filters candidate_ids before passing to
// nextCyclePosition). Direct unit-test of cycleHand requires an
// active profile, which we don't have at this layer. The pure
// nextCyclePosition math IS already validated against a pre-filtered
// candidate list above; the wrapper's filter step is verified by the
// existing engine equip-slot tests + an in-game F7 of "L hand starts
// empty, R hand holds mace, press Z, both hands now have different
// (or no) items rather than the swap-bug behavior".
