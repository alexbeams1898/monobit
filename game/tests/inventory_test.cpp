#include "InventoryOps.h"
#include "ecs/GameComponents.h"
#include "ecs/GameConfig.h"

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

// ---------------------------------------------------------------------------
// removeItem tests
// ---------------------------------------------------------------------------

TEST_CASE("removeItem valid index", "[inventory]")
{
    auto reg = makeRegistry();
    Inventory inv;
    inv.max_slots = 5;

    InventoryOps::addItem(inv, makeItem("config/items/weapons/shiv.json"), reg);
    InventoryOps::addItem(inv, makeItem("config/items/weapons/iron_sword.json"), reg);

    REQUIRE(InventoryOps::removeItem(inv, 0));
    REQUIRE(inv.items.size() == 1);
    REQUIRE(inv.items[0].config_path == "config/items/weapons/iron_sword.json");
}

TEST_CASE("removeItem invalid index returns false", "[inventory]")
{
    Inventory inv;
    REQUIRE_FALSE(InventoryOps::removeItem(inv, 0));
    REQUIRE_FALSE(InventoryOps::removeItem(inv, -1));
}

// ---------------------------------------------------------------------------
// equipItem tests
// ---------------------------------------------------------------------------

TEST_CASE("equipItem weapon goes to main_hand", "[inventory]")
{
    auto reg = makeRegistry();
    Inventory inv;
    inv.max_slots = 5;
    Equipment equip;

    InventoryOps::addItem(inv, makeItem("config/items/weapons/shiv.json"), reg);
    REQUIRE(InventoryOps::equipItem(inv, equip, 0, reg));

    REQUIRE(inv.items.empty());
    REQUIRE(equip.main_hand.config_path == "config/items/weapons/shiv.json");
}

TEST_CASE("equipItem swap returns old item to inventory", "[inventory]")
{
    auto reg = makeRegistry();
    Inventory inv;
    inv.max_slots = 5;
    Equipment equip;

    InventoryOps::addItem(inv, makeItem("config/items/weapons/shiv.json"), reg);
    InventoryOps::equipItem(inv, equip, 0, reg);

    InventoryOps::addItem(inv, makeItem("config/items/weapons/iron_sword.json"), reg);
    REQUIRE(InventoryOps::equipItem(inv, equip, 0, reg));

    REQUIRE(equip.main_hand.config_path == "config/items/weapons/iron_sword.json");
    // Old shiv should be back in inventory.
    REQUIRE(inv.items.size() == 1);
    REQUIRE(inv.items[0].config_path == "config/items/weapons/shiv.json");
}

TEST_CASE("equipItem shield goes to off_hand", "[inventory]")
{
    auto reg = makeRegistry();
    Inventory inv;
    inv.max_slots = 5;
    Equipment equip;

    InventoryOps::addItem(inv, makeItem("config/items/armor/wooden_shield.json"), reg);
    REQUIRE(InventoryOps::equipItem(inv, equip, 0, reg));

    REQUIRE(equip.off_hand.config_path == "config/items/armor/wooden_shield.json");
}

TEST_CASE("equipItem armor goes to correct slot", "[inventory]")
{
    auto reg = makeRegistry();
    Inventory inv;
    inv.max_slots = 5;
    Equipment equip;

    InventoryOps::addItem(inv, makeItem("config/items/armor/leather_chest.json"), reg);
    REQUIRE(InventoryOps::equipItem(inv, equip, 0, reg));

    REQUIRE(equip.chest.config_path == "config/items/armor/leather_chest.json");
}

TEST_CASE("equipItem two-handed weapon clears off_hand", "[inventory]")
{
    auto reg = makeRegistry();
    Inventory inv;
    inv.max_slots = 5;
    Equipment equip;

    // Equip shield first.
    InventoryOps::addItem(inv, makeItem("config/items/armor/wooden_shield.json"), reg);
    InventoryOps::equipItem(inv, equip, 0, reg);
    REQUIRE_FALSE(equip.off_hand.empty());

    // Equip two-handed sword.
    InventoryOps::addItem(inv, makeItem("config/items/weapons/iron_sword.json"), reg);
    REQUIRE(InventoryOps::equipItem(inv, equip, 0, reg));

    REQUIRE(equip.main_hand.config_path == "config/items/weapons/iron_sword.json");
    REQUIRE(equip.off_hand.empty());
    REQUIRE(equip.two_handing);
    // Shield should be back in inventory.
    bool foundShield = false;
    for (const auto& it : inv.items)
        if (it.config_path == "config/items/armor/wooden_shield.json")
            foundShield = true;
    REQUIRE(foundShield);
}

TEST_CASE("equipItem rejects non-equippable items", "[inventory]")
{
    auto reg = makeRegistry();
    Inventory inv;
    inv.max_slots = 5;
    Equipment equip;

    InventoryOps::addItem(inv, makeItem("config/items/materials/scrap_metal.json"), reg);
    REQUIRE_FALSE(InventoryOps::equipItem(inv, equip, 0, reg));
    REQUIRE(inv.items.size() == 1);
}

// ---------------------------------------------------------------------------
// unequipSlot tests
// ---------------------------------------------------------------------------

TEST_CASE("unequipSlot moves item to inventory", "[inventory]")
{
    auto reg = makeRegistry();
    Inventory inv;
    inv.max_slots = 5;
    Equipment equip;

    InventoryOps::addItem(inv, makeItem("config/items/weapons/shiv.json"), reg);
    InventoryOps::equipItem(inv, equip, 0, reg);
    REQUIRE(inv.items.empty());

    REQUIRE(InventoryOps::unequipSlot(inv, equip, EquipSlot::MainHand));
    REQUIRE(equip.main_hand.empty());
    REQUIRE(inv.items.size() == 1);
    REQUIRE(inv.items[0].config_path == "config/items/weapons/shiv.json");
}

TEST_CASE("unequipSlot fails when inventory full", "[inventory]")
{
    auto reg = makeRegistry();
    Inventory inv;
    inv.max_slots = 1;
    Equipment equip;

    InventoryOps::addItem(inv, makeItem("config/items/weapons/shiv.json"), reg);
    InventoryOps::equipItem(inv, equip, 0, reg);

    // Fill inventory.
    InventoryOps::addItem(inv, makeItem("config/items/weapons/iron_sword.json"), reg);
    REQUIRE(static_cast<int>(inv.items.size()) >= inv.max_slots);

    REQUIRE_FALSE(InventoryOps::unequipSlot(inv, equip, EquipSlot::MainHand));
    REQUIRE_FALSE(equip.main_hand.empty());
}

TEST_CASE("unequipSlot on empty slot returns false", "[inventory]")
{
    Inventory inv;
    inv.max_slots = 5;
    Equipment equip;

    REQUIRE_FALSE(InventoryOps::unequipSlot(inv, equip, EquipSlot::MainHand));
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
