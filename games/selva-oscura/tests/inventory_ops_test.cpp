#include "ecs/GameComponents.h"
#include "ecs/ItemConfig.h"
#include "ops/InventoryOps.h"

#include <catch2/catch_test_macros.hpp>

using selva::ArmorSlot;
using selva::Equipment;
using selva::EquipSlot;
using selva::Inventory;
using selva::ItemCategory;
using selva::ItemDef;
using selva::ItemInstance;
using selva::ItemRegistry;
using selva::QualityTier;

namespace ops = selva::InventoryOps;

namespace
{

ItemRegistry makeTestRegistry()
{
    ItemRegistry r;
    {
        ItemDef d;
        d.config_path = "sword";
        d.name = "Sword";
        d.category = ItemCategory::Weapon;
        d.stackable = false;
        d.max_stack = 1;
        r.defs[d.config_path] = d;
    }
    {
        ItemDef d;
        d.config_path = "herb";
        d.name = "Herb";
        d.category = ItemCategory::Consumable;
        d.stackable = true;
        d.max_stack = 10;
        r.defs[d.config_path] = d;
    }
    r.loaded = true;
    return r;
}

ItemInstance makeInstance(const std::string& path, int qty = 1)
{
    ItemInstance i;
    i.config_path = path;
    i.quantity = qty;
    return i;
}

} // namespace

TEST_CASE("InventoryOps: addItem appends non-stackable items", "[inventory][add]")
{
    Inventory inv;
    const ItemRegistry reg = makeTestRegistry();

    REQUIRE(ops::addItem(inv, makeInstance("sword"), reg));
    REQUIRE(ops::addItem(inv, makeInstance("sword"), reg));
    REQUIRE(inv.items.size() == 2);
}

TEST_CASE("InventoryOps: addItem stacks stackable items", "[inventory][add][stack]")
{
    Inventory inv;
    const ItemRegistry reg = makeTestRegistry();

    REQUIRE(ops::addItem(inv, makeInstance("herb", 5), reg));
    REQUIRE(ops::addItem(inv, makeInstance("herb", 3), reg));
    REQUIRE(inv.items.size() == 1);
    REQUIRE(inv.items[0].quantity == 8);
}

TEST_CASE("InventoryOps: addItem overflows stackable items to new entry",
          "[inventory][stack][overflow]")
{
    Inventory inv;
    const ItemRegistry reg = makeTestRegistry();

    REQUIRE(ops::addItem(inv, makeInstance("herb", 7), reg));
    REQUIRE(ops::addItem(inv, makeInstance("herb", 5), reg));
    // First stack tops out at 10; remaining 2 goes to a new entry.
    REQUIRE(inv.items.size() == 2);
    REQUIRE(inv.items[0].quantity == 10);
    REQUIRE(inv.items[1].quantity == 2);
}

TEST_CASE("InventoryOps: equipItemToSlot moves between hand slots", "[inventory][equip]")
{
    Inventory inv;
    Equipment eq;
    const ItemRegistry reg = makeTestRegistry();

    REQUIRE(ops::addItem(inv, makeInstance("sword"), reg));
    REQUIRE(ops::equipItemToSlot(eq, 0, EquipSlot::RightHand));
    REQUIRE(eq.right_hand == 0);
    REQUIRE(eq.left_hand == -1);

    // Move it to left hand - right hand should be cleared.
    REQUIRE(ops::equipItemToSlot(eq, 0, EquipSlot::LeftHand));
    REQUIRE(eq.right_hand == -1);
    REQUIRE(eq.left_hand == 0);
}

TEST_CASE("InventoryOps: removeItem reindexes equipment slots", "[inventory][remove][reindex]")
{
    Inventory inv;
    Equipment eq;
    const ItemRegistry reg = makeTestRegistry();

    REQUIRE(ops::addItem(inv, makeInstance("sword"), reg));
    REQUIRE(ops::addItem(inv, makeInstance("sword"), reg));
    REQUIRE(ops::addItem(inv, makeInstance("sword"), reg));
    REQUIRE(ops::equipItemToSlot(eq, 2, EquipSlot::RightHand));

    // Remove index 0; the equipped index 2 should shift down to 1.
    REQUIRE(ops::removeItem(inv, eq, 0));
    REQUIRE(eq.right_hand == 1);

    // Remove the equipped item itself; slot should become -1.
    REQUIRE(ops::removeItem(inv, eq, 1));
    REQUIRE(eq.right_hand == -1);
}

TEST_CASE("InventoryOps: countItem and consumeItems work on stacks", "[inventory][consume]")
{
    Inventory inv;
    Equipment eq;
    const ItemRegistry reg = makeTestRegistry();

    REQUIRE(ops::addItem(inv, makeInstance("herb", 8), reg));
    REQUIRE(ops::countItem(inv, "herb") == 8);

    REQUIRE(ops::consumeItems(inv, eq, "herb", 5));
    REQUIRE(ops::countItem(inv, "herb") == 3);

    REQUIRE(ops::consumeItems(inv, eq, "herb", 3));
    REQUIRE(ops::countItem(inv, "herb") == 0);
    REQUIRE(inv.items.empty());

    // Not enough remaining - returns false.
    REQUIRE(!ops::consumeItems(inv, eq, "herb", 1));
}

TEST_CASE("InventoryOps: addItem rejects when inventory full and not stackable",
          "[inventory][full]")
{
    Inventory inv;
    inv.max_slots = 2;
    const ItemRegistry reg = makeTestRegistry();

    REQUIRE(ops::addItem(inv, makeInstance("sword"), reg));
    REQUIRE(ops::addItem(inv, makeInstance("sword"), reg));
    // Third add should fail - bag is full.
    REQUIRE(!ops::addItem(inv, makeInstance("sword"), reg));
}

TEST_CASE("InventoryOps: itemRegistry singleton returns same instance", "[inventory][registry]")
{
    auto& r1 = selva::itemRegistry();
    auto& r2 = selva::itemRegistry();
    REQUIRE(&r1 == &r2);
}
