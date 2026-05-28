#include "ecs/Items.h"
#include "ecs/RpgComponents.h"
#include "ops/InventoryOps.h"
using namespace engine::ecs;
namespace InventoryOps = engine::ops::inventory;

#include <catch2/catch_test_macros.hpp>

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

static ItemRegistry makeRegistry()
{
    ItemRegistry reg;

    ItemDef shiv;
    shiv.config_path = "config/items/weapons/shiv.json";
    shiv.name = "Shiv";
    shiv.category = ItemCategory::Weapon;
    shiv.base_damage = 12.0f;
    shiv.weight = 1.0f;
    shiv.str_scaling = 0.5f;
    shiv.dex_scaling = 1.0f;
    shiv.str_requirement = 3;
    shiv.dex_requirement = 5;
    shiv.stackable = false;
    reg.defs[shiv.config_path] = shiv;

    ItemDef sword;
    sword.config_path = "config/items/weapons/iron_sword.json";
    sword.name = "Iron Sword";
    sword.category = ItemCategory::Weapon;
    sword.base_damage = 20.0f;
    sword.weight = 3.0f;
    sword.str_scaling = 1.2f;
    sword.dex_scaling = 0.3f;
    sword.two_handed = true;
    sword.stackable = false;
    reg.defs[sword.config_path] = sword;

    ItemDef shield;
    shield.config_path = "config/items/armor/wooden_shield.json";
    shield.name = "Wooden Shield";
    shield.category = ItemCategory::Armor;
    shield.max_guard = 60.0f;
    shield.armor_slot = ArmorSlot::Chest;
    shield.stackable = false;
    reg.defs[shield.config_path] = shield;

    ItemDef chest;
    chest.config_path = "config/items/armor/leather_chest.json";
    chest.name = "Leather Vest";
    chest.category = ItemCategory::Armor;
    chest.armor_slot = ArmorSlot::Chest;
    chest.defense_bonus = 5.0f;
    chest.stackable = false;
    reg.defs[chest.config_path] = chest;

    ItemDef scrap;
    scrap.config_path = "config/items/materials/scrap_metal.json";
    scrap.name = "Scrap Metal";
    scrap.category = ItemCategory::Material;
    scrap.stackable = true;
    scrap.max_stack = 99;
    reg.defs[scrap.config_path] = scrap;

    return reg;
}

static ItemInstance makeItem(const std::string& path, int qty = 1)
{
    ItemInstance item;
    item.config_path = path;
    item.quantity = qty;
    return item;
}

// ---------------------------------------------------------------------------
// ItemRegistry tests
// ---------------------------------------------------------------------------

TEST_CASE("ItemRegistry find returns pointer for known item", "[inventory]")
{
    auto reg = makeRegistry();
    const ItemDef* def = reg.find("config/items/weapons/shiv.json");
    REQUIRE(def != nullptr);
    REQUIRE(def->name == "Shiv");
    REQUIRE(def->category == ItemCategory::Weapon);
}

TEST_CASE("ItemRegistry find returns nullptr for unknown item", "[inventory]")
{
    auto reg = makeRegistry();
    REQUIRE(reg.find("config/items/weapons/nonexistent.json") == nullptr);
}

// ---------------------------------------------------------------------------
// addItem tests
// ---------------------------------------------------------------------------

TEST_CASE("addItem to empty inventory", "[inventory]")
{
    auto reg = makeRegistry();
    Inventory inv;
    inv.max_slots = 5;

    REQUIRE(InventoryOps::addItem(inv, makeItem("config/items/weapons/shiv.json"), reg));
    REQUIRE(inv.items.size() == 1);
    REQUIRE(inv.items[0].config_path == "config/items/weapons/shiv.json");
}

TEST_CASE("addItem stackable merges with existing stack", "[inventory]")
{
    auto reg = makeRegistry();
    Inventory inv;
    inv.max_slots = 5;

    InventoryOps::addItem(inv, makeItem("config/items/materials/scrap_metal.json", 3), reg);
    InventoryOps::addItem(inv, makeItem("config/items/materials/scrap_metal.json", 2), reg);

    REQUIRE(inv.items.size() == 1);
    REQUIRE(inv.items[0].quantity == 5);
}

TEST_CASE("addItem non-stackable creates separate entries", "[inventory]")
{
    auto reg = makeRegistry();
    Inventory inv;
    inv.max_slots = 5;

    InventoryOps::addItem(inv, makeItem("config/items/weapons/shiv.json"), reg);
    InventoryOps::addItem(inv, makeItem("config/items/weapons/shiv.json"), reg);

    REQUIRE(inv.items.size() == 2);
}

TEST_CASE("addItem fails when inventory full", "[inventory]")
{
    auto reg = makeRegistry();
    Inventory inv;
    inv.max_slots = 1;

    REQUIRE(InventoryOps::addItem(inv, makeItem("config/items/weapons/shiv.json"), reg));
    REQUIRE_FALSE(
        InventoryOps::addItem(inv, makeItem("config/items/weapons/iron_sword.json"), reg));
    REQUIRE(inv.items.size() == 1);
}

TEST_CASE("addItem stackable remainder gets correct quantity", "[inventory]")
{
    auto reg = makeRegistry();
    Inventory inv;
    inv.max_slots = 5;

    // Fill a stack almost to max (99).
    InventoryOps::addItem(inv, makeItem("config/items/materials/scrap_metal.json", 95), reg);
    REQUIRE(inv.items.size() == 1);
    REQUIRE(inv.items[0].quantity == 95);

    // Add 10 more -- 4 merge into existing, 6 overflow to new slot.
    REQUIRE(
        InventoryOps::addItem(inv, makeItem("config/items/materials/scrap_metal.json", 10), reg));
    REQUIRE(inv.items.size() == 2);
    REQUIRE(inv.items[0].quantity == 99);
    REQUIRE(inv.items[1].quantity == 6);
}

TEST_CASE("addItem stackable overflow with full inventory", "[inventory]")
{
    auto reg = makeRegistry();
    Inventory inv;
    inv.max_slots = 1;

    // Fill one stack to max.
    InventoryOps::addItem(inv, makeItem("config/items/materials/scrap_metal.json", 99), reg);

    // Try to add more -- no room for overflow slot.
    REQUIRE_FALSE(
        InventoryOps::addItem(inv, makeItem("config/items/materials/scrap_metal.json", 5), reg));
    // Original stack unchanged.
    REQUIRE(inv.items[0].quantity == 99);
}

// ---------------------------------------------------------------------------
// removeItem tests
// ---------------------------------------------------------------------------

TEST_CASE("removeItem valid index", "[inventory]")
{
    auto reg = makeRegistry();
    Inventory inv;
    inv.max_slots = 5;
    Equipment equip;

    InventoryOps::addItem(inv, makeItem("config/items/weapons/shiv.json"), reg);
    InventoryOps::addItem(inv, makeItem("config/items/weapons/iron_sword.json"), reg);

    REQUIRE(InventoryOps::removeItem(inv, equip, 0));
    REQUIRE(inv.items.size() == 1);
    REQUIRE(inv.items[0].config_path == "config/items/weapons/iron_sword.json");
}

TEST_CASE("removeItem invalid index returns false", "[inventory]")
{
    Inventory inv;
    Equipment equip;
    REQUIRE_FALSE(InventoryOps::removeItem(inv, equip, 0));
    REQUIRE_FALSE(InventoryOps::removeItem(inv, equip, -1));
}

// ---------------------------------------------------------------------------
// equipItemToSlot tests
// ---------------------------------------------------------------------------

TEST_CASE("equipItemToSlot weapon goes to right_hand", "[inventory]")
{
    auto reg = makeRegistry();
    Inventory inv;
    inv.max_slots = 5;
    Equipment equip;

    InventoryOps::addItem(inv, makeItem("config/items/weapons/shiv.json"), reg);
    REQUIRE(InventoryOps::equipItemToSlot(equip, 0, EquipSlot::RightHand));

    // Item stays in inventory; equip slot points at it.
    REQUIRE(inv.items.size() == 1);
    REQUIRE(equip.right_hand == 0);
    REQUIRE(InventoryOps::equippedPath(inv, equip, EquipSlot::RightHand) ==
            "config/items/weapons/shiv.json");
}

TEST_CASE("equipItemToSlot swap unequips old item", "[inventory]")
{
    auto reg = makeRegistry();
    Inventory inv;
    inv.max_slots = 5;
    Equipment equip;

    InventoryOps::addItem(inv, makeItem("config/items/weapons/shiv.json"), reg);
    InventoryOps::addItem(inv, makeItem("config/items/weapons/iron_sword.json"), reg);
    InventoryOps::equipItemToSlot(equip, 0, EquipSlot::RightHand);

    // Equip a different item in the same slot.
    REQUIRE(InventoryOps::equipItemToSlot(equip, 1, EquipSlot::RightHand));

    REQUIRE(equip.right_hand == 1);
    REQUIRE(InventoryOps::equippedPath(inv, equip, EquipSlot::RightHand) ==
            "config/items/weapons/iron_sword.json");
    // Old shiv is still in inventory at index 0, just no longer equipped.
    REQUIRE(inv.items[0].config_path == "config/items/weapons/shiv.json");
}

TEST_CASE("equipItemToSlot shield to left hand", "[inventory]")
{
    auto reg = makeRegistry();
    Inventory inv;
    inv.max_slots = 5;
    Equipment equip;

    InventoryOps::addItem(inv, makeItem("config/items/armor/wooden_shield.json"), reg);
    REQUIRE(InventoryOps::equipItemToSlot(equip, 0, EquipSlot::LeftHand));

    REQUIRE(InventoryOps::equippedPath(inv, equip, EquipSlot::LeftHand) ==
            "config/items/armor/wooden_shield.json");
    REQUIRE(InventoryOps::slotEmpty(equip, EquipSlot::RightHand));
}

TEST_CASE("equipItemToSlot armor goes to chest slot", "[inventory]")
{
    auto reg = makeRegistry();
    Inventory inv;
    inv.max_slots = 5;
    Equipment equip;

    InventoryOps::addItem(inv, makeItem("config/items/armor/leather_chest.json"), reg);
    REQUIRE(InventoryOps::equipItemToSlot(equip, 0, EquipSlot::Chest));

    REQUIRE(InventoryOps::equippedPath(inv, equip, EquipSlot::Chest) ==
            "config/items/armor/leather_chest.json");
}

TEST_CASE("equipItemToSlot two-handed weapon does not clear left_hand", "[inventory]")
{
    // With the new runtime two-hand toggle, equipping a two-handed-capable
    // weapon leaves the left hand occupied. Suppression happens at render /
    // behavior time when the player toggles two_handed_active on.
    auto reg = makeRegistry();
    Inventory inv;
    inv.max_slots = 5;
    Equipment equip;

    InventoryOps::addItem(inv, makeItem("config/items/armor/wooden_shield.json"), reg);
    InventoryOps::addItem(inv, makeItem("config/items/weapons/iron_sword.json"), reg);
    InventoryOps::equipItemToSlot(equip, 0, EquipSlot::LeftHand);
    REQUIRE_FALSE(InventoryOps::slotEmpty(equip, EquipSlot::LeftHand));

    REQUIRE(InventoryOps::equipItemToSlot(equip, 1, EquipSlot::RightHand));

    REQUIRE(InventoryOps::equippedPath(inv, equip, EquipSlot::RightHand) ==
            "config/items/weapons/iron_sword.json");
    REQUIRE_FALSE(InventoryOps::slotEmpty(equip, EquipSlot::LeftHand));
}

// ---------------------------------------------------------------------------
// unequipSlot tests
// ---------------------------------------------------------------------------

TEST_CASE("unequipSlot clears the slot index", "[inventory]")
{
    auto reg = makeRegistry();
    Inventory inv;
    inv.max_slots = 5;
    Equipment equip;

    InventoryOps::addItem(inv, makeItem("config/items/weapons/shiv.json"), reg);
    InventoryOps::equipItemToSlot(equip, 0, EquipSlot::RightHand);
    REQUIRE_FALSE(InventoryOps::slotEmpty(equip, EquipSlot::RightHand));

    InventoryOps::unequipSlot(equip, EquipSlot::RightHand);
    REQUIRE(InventoryOps::slotEmpty(equip, EquipSlot::RightHand));
    // Item stays in inventory.
    REQUIRE(inv.items.size() == 1);
    REQUIRE(inv.items[0].config_path == "config/items/weapons/shiv.json");
}

TEST_CASE("unequipSlot on empty slot is a no-op", "[inventory]")
{
    Equipment equip;
    InventoryOps::unequipSlot(equip, EquipSlot::RightHand);
    REQUIRE(InventoryOps::slotEmpty(equip, EquipSlot::RightHand));
}

// ---------------------------------------------------------------------------
// ItemInstance::empty tests
// ---------------------------------------------------------------------------

TEST_CASE("ItemInstance empty when config_path is empty", "[inventory]")
{
    ItemInstance item;
    REQUIRE(item.empty());

    item.config_path = "something";
    REQUIRE_FALSE(item.empty());
}
