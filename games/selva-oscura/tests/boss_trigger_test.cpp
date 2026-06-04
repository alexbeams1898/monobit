// Boss-backend trigger tests: RegionTrigger TriggerAction enum +
// Custom action payload. Per
// games/selva-oscura/docs/design/ideas/boss_backend.md section 3 +
// step 3 of the impl plan.
//
// The trigger JSON parsing lives inside JsonRegion::commitPrepared
// which depends on the whole region/physics system. These tests
// verify the data shape end-to-end via struct manipulation; the
// JSON-string round-trip is covered by region_schema_test.cpp's
// integration approach.

#include "world/Region.h"

#include <catch2/catch_test_macros.hpp>

using engine::world::RegionTrigger;
using engine::world::TriggerAction;

TEST_CASE("RegionTrigger: default action is RegionTransition", "[boss-backend]")
{
    RegionTrigger t;
    REQUIRE(t.action == TriggerAction::RegionTransition);
    REQUIRE(t.action_payload.empty());
}

TEST_CASE("RegionTrigger: Custom action carries payload", "[boss-backend]")
{
    RegionTrigger t;
    t.id = "lupa_engage";
    t.action = TriggerAction::Custom;
    t.action_payload = "engage:lupa";

    REQUIRE(t.action == TriggerAction::Custom);
    REQUIRE(t.action_payload == "engage:lupa");
}

TEST_CASE("RegionTrigger: Custom triggers don't need target", "[boss-backend]")
{
    // A Custom trigger may have target = kInvalidRegion and that's
    // fine -- the game routes by action_payload, not target. Engine
    // suppresses the "unknown target_region" warning for Custom
    // triggers in JsonRegion::commitPrepared.
    RegionTrigger t;
    t.action = TriggerAction::Custom;
    t.action_payload = "spawn:dire_wolf";
    // No target set; that's the point.

    REQUIRE(t.action == TriggerAction::Custom);
    REQUIRE(t.target.id == engine::world::kInvalidRegion.id);
}

TEST_CASE("RegionTrigger: payload convention is verb:arg parseable", "[boss-backend]")
{
    // The action_payload convention is "<verb>:<arg>" (free-form
    // string). Game-side dispatcher splits on ':'. Verifying the
    // string is preserved verbatim through assignment, including
    // edge cases the dispatcher will need to handle.
    RegionTrigger t;
    t.action = TriggerAction::Custom;

    SECTION("Standard verb:arg")
    {
        t.action_payload = "engage:lupa";
        REQUIRE(t.action_payload.find(':') != std::string::npos);
    }
    SECTION("Verb with no arg (malformed but should not crash)")
    {
        t.action_payload = "engage";
        REQUIRE(t.action_payload == "engage");
    }
    SECTION("Empty payload (malformed but should not crash)")
    {
        t.action_payload = "";
        REQUIRE(t.action_payload.empty());
    }
}
