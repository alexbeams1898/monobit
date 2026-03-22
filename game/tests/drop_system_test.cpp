#include "ecs/Components.h"
#include "ecs/EntityManager.h"
#include "ecs/GameComponents.h"
#include "ecs/GameConfig.h"
#include "systems/DeathSystem.h"
#include "systems/PickupSystem.h"
#include "test_helpers.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

static ItemRegistry makeItemRegistry()
{
    ItemRegistry reg;

    ItemDef bone;
    bone.config_path = "config/items/materials/bone_shard.json";
    bone.name = "Bone Shard";
    bone.category = ItemCategory::Material;
    bone.stackable = true;
    bone.max_stack = 99;
    reg.defs[bone.config_path] = bone;

    ItemDef bill;
    bill.config_path = "config/items/money/5_bill.json";
    bill.name = "$5 Bill";
    bill.category = ItemCategory::Money;
    bill.rarity = Rarity::Uncommon;
    bill.value = 5;
    bill.stackable = true;
    bill.max_stack = 99;
    reg.defs[bill.config_path] = bill;

    return reg;
}

// Create a minimal enemy entity with Loot + drops and a death trigger.
static entt::entity spawnEnemy(EntityManager& em, const std::vector<DropEntry>& drops, int lck = 0)
{
    auto e = em.create();
    em.registry().emplace<Transform>(e, Transform{100.0f, 100.0f});
    em.registry().emplace<Tag>(e, Tag{"skeleton"});
    em.registry().emplace<Health>(e, Health{0, 50}); // already dead

    Stats stats;
    stats.lck = lck;
    em.registry().emplace<Stats>(e, stats);

    Loot loot;
    loot.xp_drop = 20;
    loot.drops = drops;
    em.registry().emplace<Loot>(e, loot);

    Dead dead;
    dead.timer = 0.0f; // ready to process
    em.registry().emplace<Dead>(e, dead);

    return e;
}

static entt::entity spawnPlayer(EntityManager& em)
{
    auto p = em.create();
    em.registry().emplace<Transform>(p, Transform{100.0f, 100.0f});
    em.registry().emplace<PlayerActions>(p);
    em.registry().emplace<Experience>(p);
    em.registry().emplace<Health>(p, Health{100, 100});
    em.registry().emplace<Inventory>(p);
    em.registry().emplace<Wallet>(p);
    return p;
}

// ---------------------------------------------------------------------------
// Drop spawn tests
// ---------------------------------------------------------------------------

TEST_CASE("Enemy death spawns item pickup", "[drops]")
{
    EntityManager em;
    emplaceGameConfigs(em);
    em.registry().ctx().get<ItemRegistry>() = makeItemRegistry();

    spawnPlayer(em);

    DropEntry drop;
    drop.config_path = "config/items/materials/bone_shard.json";
    drop.base_chance = 1.0f; // guaranteed
    drop.min_qty = 1;
    drop.max_qty = 1;
    spawnEnemy(em, {drop});

    DeathSystem::update(em, 0.0);

    // Enemy should be destroyed, a Pickup entity should exist.
    int pickupCount = 0;
    for (auto [entity, pickup] : em.registry().view<Pickup>().each())
    {
        if (!pickup.item.empty())
        {
            REQUIRE(pickup.item.config_path == "config/items/materials/bone_shard.json");
            REQUIRE(pickup.item.quantity >= 1);
            ++pickupCount;
        }
    }
    REQUIRE(pickupCount == 1);
}

TEST_CASE("Zero drop chance spawns no item pickup", "[drops]")
{
    EntityManager em;
    emplaceGameConfigs(em);
    em.registry().ctx().get<ItemRegistry>() = makeItemRegistry();

    spawnPlayer(em);

    DropEntry drop;
    drop.config_path = "config/items/materials/bone_shard.json";
    drop.base_chance = 0.0f; // never
    drop.min_qty = 1;
    drop.max_qty = 1;
    spawnEnemy(em, {drop});

    DeathSystem::update(em, 0.0);

    int itemPickups = 0;
    for (auto [entity, pickup] : em.registry().view<Pickup>().each())
    {
        if (!pickup.item.empty())
            ++itemPickups;
    }
    REQUIRE(itemPickups == 0);
}

TEST_CASE("Item pickup collected into player inventory", "[drops]")
{
    EntityManager em;
    emplaceGameConfigs(em);
    em.registry().ctx().get<ItemRegistry>() = makeItemRegistry();

    auto player = spawnPlayer(em);

    // Manually create a pickup.
    auto pe = em.create();
    em.registry().emplace<Transform>(pe, Transform{100.0f, 100.0f}); // same pos as player
    Pickup p;
    p.item.config_path = "config/items/materials/bone_shard.json";
    p.item.quantity = 2;
    em.registry().emplace<Pickup>(pe, std::move(p));

    em.registry().get<PlayerActions>(player).interact = true;
    PickupSystem::update(em);

    auto& inv = em.registry().get<Inventory>(player);
    REQUIRE(inv.items.size() == 1);
    REQUIRE(inv.items[0].config_path == "config/items/materials/bone_shard.json");
    REQUIRE(inv.items[0].quantity == 2);

    // Pickup entity should be destroyed.
    REQUIRE_FALSE(em.registry().valid(pe));
}

TEST_CASE("Item pickups stack in inventory", "[drops]")
{
    EntityManager em;
    emplaceGameConfigs(em);
    em.registry().ctx().get<ItemRegistry>() = makeItemRegistry();

    auto player = spawnPlayer(em);

    // Two separate pickups of the same item.
    for (int i = 0; i < 2; ++i)
    {
        auto pe = em.create();
        em.registry().emplace<Transform>(pe, Transform{100.0f, 100.0f});
        Pickup p;
        p.item.config_path = "config/items/materials/bone_shard.json";
        p.item.quantity = 3;
        em.registry().emplace<Pickup>(pe, std::move(p));
    }

    em.registry().get<PlayerActions>(player).interact = true;
    PickupSystem::update(em);
    PickupSystem::update(em);

    auto& inv = em.registry().get<Inventory>(player);
    REQUIRE(inv.items.size() == 1);
    REQUIRE(inv.items[0].quantity == 6);
}

TEST_CASE("Inventory-full prevents item pickup collection", "[drops]")
{
    EntityManager em;
    emplaceGameConfigs(em);
    em.registry().ctx().get<ItemRegistry>() = makeItemRegistry();

    auto player = spawnPlayer(em);
    auto& inv = em.registry().get<Inventory>(player);
    inv.max_slots = 0; // no space

    auto pe = em.create();
    em.registry().emplace<Transform>(pe, Transform{100.0f, 100.0f});
    Pickup p;
    p.item.config_path = "config/items/materials/bone_shard.json";
    p.item.quantity = 1;
    em.registry().emplace<Pickup>(pe, std::move(p));

    em.registry().get<PlayerActions>(player).interact = true;
    PickupSystem::update(em);

    // Pickup should still exist.
    REQUIRE(em.registry().valid(pe));
    REQUIRE(inv.items.empty());
}

TEST_CASE("XP pickup still works alongside item pickups", "[drops]")
{
    EntityManager em;
    emplaceGameConfigs(em);
    em.registry().ctx().get<ItemRegistry>() = makeItemRegistry();

    auto player = spawnPlayer(em);

    auto pe = em.create();
    em.registry().emplace<Transform>(pe, Transform{100.0f, 100.0f});
    Pickup p;
    p.xp_value = 42;
    em.registry().emplace<Pickup>(pe, std::move(p));

    PickupSystem::update(em);

    auto& exp = em.registry().get<Experience>(player);
    REQUIRE(exp.current_xp == 42);
    REQUIRE_FALSE(em.registry().valid(pe));
}

TEST_CASE("Money pickup adds to Wallet", "[drops]")
{
    EntityManager em;
    emplaceGameConfigs(em);
    em.registry().ctx().get<ItemRegistry>() = makeItemRegistry();

    auto player = spawnPlayer(em);

    auto pe = em.create();
    em.registry().emplace<Transform>(pe, Transform{100.0f, 100.0f});
    Pickup p;
    p.item.config_path = "config/items/money/5_bill.json";
    p.item.quantity = 1;
    em.registry().emplace<Pickup>(pe, std::move(p));

    em.registry().get<PlayerActions>(player).interact = true;
    PickupSystem::update(em);

    REQUIRE(em.registry().get<Wallet>(player).money == 5);
    // Money should NOT go to inventory.
    REQUIRE(em.registry().get<Inventory>(player).items.empty());
    REQUIRE_FALSE(em.registry().valid(pe));
}

TEST_CASE("Dropped items have a quality tier", "[drops]")
{
    EntityManager em;
    emplaceGameConfigs(em);
    em.registry().ctx().get<ItemRegistry>() = makeItemRegistry();

    spawnPlayer(em);

    DropEntry drop;
    drop.config_path = "config/items/materials/bone_shard.json";
    drop.base_chance = 1.0f;
    drop.min_qty = 1;
    drop.max_qty = 1;
    spawnEnemy(em, {drop});

    DeathSystem::update(em, 0.0);

    bool found = false;
    for (auto [entity, pickup] : em.registry().view<Pickup>().each())
    {
        if (!pickup.item.empty())
        {
            // Quality should be a valid enum value (0-4).
            REQUIRE(static_cast<int>(pickup.item.quality) >= 0);
            REQUIRE(static_cast<int>(pickup.item.quality) <= 4);
            found = true;
        }
    }
    REQUIRE(found);
}

TEST_CASE("High essence enemy drops higher quality items", "[drops]")
{
    EntityManager em;
    emplaceGameConfigs(em);
    em.registry().ctx().get<ItemRegistry>() = makeItemRegistry();

    spawnPlayer(em);

    DropEntry drop;
    drop.config_path = "config/items/materials/bone_shard.json";
    drop.base_chance = 1.0f;
    drop.min_qty = 1;
    drop.max_qty = 1;

    // High LCK guarantees Masterwork: lck=32 * quality_scale=3.0 = +96 > 95 threshold.
    // Essence adds a small bonus on top (total=200 * 0.05 = +10).
    auto e = spawnEnemy(em, {drop}, /*lck=*/32);
    Essence ess;
    ess.str = 50;
    ess.dex = 50;
    ess.end = 50;
    ess.lck = 50;
    em.registry().emplace<Essence>(e, ess);

    DeathSystem::update(em, 0.0);

    for (auto [entity, pickup] : em.registry().view<Pickup>().each())
    {
        if (!pickup.item.empty())
            REQUIRE(pickup.item.quality == QualityTier::Masterwork);
    }
}

TEST_CASE("Money drops via unified drop table", "[drops]")
{
    EntityManager em;
    emplaceGameConfigs(em);
    em.registry().ctx().get<ItemRegistry>() = makeItemRegistry();

    auto player = spawnPlayer(em);

    // Guaranteed $5 bill drop.
    DropEntry drop;
    drop.config_path = "config/items/money/5_bill.json";
    drop.base_chance = 1.0f;
    drop.min_qty = 1;
    drop.max_qty = 1;
    spawnEnemy(em, {drop});

    DeathSystem::update(em, 0.0);

    // Should spawn a Pickup with the money item.
    bool found = false;
    for (auto [entity, pickup] : em.registry().view<Pickup>().each())
    {
        if (!pickup.item.empty() && pickup.item.config_path == "config/items/money/5_bill.json")
            found = true;
    }
    REQUIRE(found);

    // Collect it.
    em.registry().get<PlayerActions>(player).interact = true;
    PickupSystem::update(em);
    REQUIRE(em.registry().get<Wallet>(player).money == 5);
}
