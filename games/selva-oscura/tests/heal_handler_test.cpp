// Tests for selva::items::healAction's reject-vs-fire behavior. The
// critical contract: full HP returns fired=false + a player-facing
// rejection reason (so the caller does NOT consume the item). Sub-max
// HP returns fired=true with a "Used <quality-name>" message and
// raises hp.current by the tier %.

#include "AppState.h"
#include "AppStateGlobal.h"
#include "ecs/Items.h"
#include "gameplay/Actor.h"
#include "items/HealHandlers.h"
#include "items/ItemRegistry.h"
#include "items/UseHandlers.h"
#include "ops/InventoryOps.h"
#include "test_helpers.h"

#include <string>

#include <catch2/catch_test_macros.hpp>

namespace
{

// Make sure the poultice ItemDef exists in the registry so
// applyHealPct's tierPctFor lookup hits a known config_path.
void registerPoulticeDef()
{
    engine::ecs::ItemDef def;
    def.config_path = "config/items/consumables/poultice.json";
    def.name = "Poultice";
    def.category = engine::ecs::ItemCategory::Consumable;
    def.stackable = true;
    def.max_stack = 99;
    selva::items::itemRegistry().defs[def.config_path] = def;
}

// Push a fresh player actor at the front of the pool and return its
// reference. Caller is responsible for clearing the pool.
selva::gameplay::Actor& seedPlayer(int max_hp, int current_hp)
{
    selva::gameplay::actors().clear();
    selva::gameplay::Actor a;
    a.hp.max = max_hp;
    a.hp.current = current_hp;
    selva::gameplay::actors().push_back(std::move(a));
    return selva::gameplay::actors().front();
}

engine::ecs::ItemInstanceId addPoultice(selva::PlayerProfile& profile)
{
    engine::ecs::ItemInstance inst;
    inst.config_path = "config/items/consumables/poultice.json";
    inst.quantity = 1;
    return engine::ops::inventory::addItem(profile.inventory, inst, selva::items::itemRegistry());
}

} // namespace

TEST_CASE("healAction at full HP returns fired=false with player-facing rejection reason",
          "[items][heal][full-hp-reject]")
{
    selva::tests::ActiveProfileScope scope{"PILGRIM"};
    registerPoulticeDef();
    auto& player = seedPlayer(100, 100);
    auto& profile = scope.profile();
    const auto pid = addPoultice(profile);

    const auto r = selva::items::healAction(profile.inventory, pid);
    REQUIRE_FALSE(r.fired);
    REQUIRE(r.rejection_reason == "Already at full health.");
    REQUIRE(player.hp.current == 100); // unchanged
    // Item must remain in inventory; consume happens only when fired.
    const auto* still = engine::ops::inventory::findById(profile.inventory, pid);
    REQUIRE(still != nullptr);
    REQUIRE(still->quantity == 1);

    selva::gameplay::actors().clear();
}

TEST_CASE("healAction below full HP fires + populates Used <name> message", "[items][heal]")
{
    selva::tests::ActiveProfileScope scope{"PILGRIM"};
    registerPoulticeDef();
    auto& player = seedPlayer(100, 50);
    auto& profile = scope.profile();
    const auto pid = addPoultice(profile);

    const auto r = selva::items::healAction(profile.inventory, pid);
    REQUIRE(r.fired);
    REQUIRE(r.rejection_reason.empty());
    REQUIRE(r.success_message.rfind("Used ", 0) == 0);
    // Caller (useItem orchestrator) does the consume; healAction only
    // applies the HP delta. We assert HP rose.
    REQUIRE(player.hp.current > 50);
    REQUIRE(player.hp.current <= 100);

    selva::gameplay::actors().clear();
}

TEST_CASE("healAction rejects unknown item id", "[items][heal]")
{
    selva::tests::ActiveProfileScope scope{"PILGRIM"};
    registerPoulticeDef();
    seedPlayer(100, 50);

    const auto r = selva::items::healAction(scope.profile().inventory, 99999);
    REQUIRE_FALSE(r.fired);
    REQUIRE(r.rejection_reason == "Item not found.");

    selva::gameplay::actors().clear();
}
