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

// Look up the single bucket vector by string key (matches categoryKey
// in InventoryOps.cpp). Returns nullptr if no items in that bucket.
static const std::vector<ItemInstance>* bucket(const Inventory& inv, const std::string& key)
{
    const auto it = inv.by_category.find(key);
    return (it != inv.by_category.end()) ? &it->second : nullptr;
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

    const auto id = InventoryOps::addItem(inv, makeItem("config/items/weapons/shiv.json"), reg);
    REQUIRE(id != kInvalidItemInstanceId);

    const auto* weapons = bucket(inv, "weapons");
    REQUIRE(weapons != nullptr);
    REQUIRE(weapons->size() == 1);
    REQUIRE((*weapons)[0].config_path == "config/items/weapons/shiv.json");
    REQUIRE((*weapons)[0].id == id);
}

TEST_CASE("addItem stackable merges with existing stack", "[inventory]")
{
    auto reg = makeRegistry();
    Inventory inv;

    InventoryOps::addItem(inv, makeItem("config/items/materials/scrap_metal.json", 3), reg);
    InventoryOps::addItem(inv, makeItem("config/items/materials/scrap_metal.json", 2), reg);

    const auto* mats = bucket(inv, "materials");
    REQUIRE(mats != nullptr);
    REQUIRE(mats->size() == 1);
    REQUIRE((*mats)[0].quantity == 5);
}

TEST_CASE("addItem non-stackable creates separate entries", "[inventory]")
{
    auto reg = makeRegistry();
    Inventory inv;

    InventoryOps::addItem(inv, makeItem("config/items/weapons/shiv.json"), reg);
    InventoryOps::addItem(inv, makeItem("config/items/weapons/shiv.json"), reg);

    const auto* weapons = bucket(inv, "weapons");
    REQUIRE(weapons != nullptr);
    REQUIRE(weapons->size() == 2);
    REQUIRE((*weapons)[0].id != (*weapons)[1].id);
}

TEST_CASE("addItem unknown config_path returns invalid id", "[inventory]")
{
    auto reg = makeRegistry();
    Inventory inv;

    const auto id = InventoryOps::addItem(inv, makeItem("config/items/weapons/ghost.json"), reg);
    REQUIRE(id == kInvalidItemInstanceId);
    REQUIRE(inv.by_category.empty());
}

TEST_CASE("addItem stackable remainder gets correct quantity", "[inventory]")
{
    auto reg = makeRegistry();
    Inventory inv;

    // Fill a stack almost to max (99).
    InventoryOps::addItem(inv, makeItem("config/items/materials/scrap_metal.json", 95), reg);
    const auto* mats = bucket(inv, "materials");
    REQUIRE(mats != nullptr);
    REQUIRE(mats->size() == 1);
    REQUIRE((*mats)[0].quantity == 95);

    // Add 10 more -- 4 merge into existing, 6 overflow to new stack.
    REQUIRE(InventoryOps::addItem(inv, makeItem("config/items/materials/scrap_metal.json", 10),
                                  reg) != kInvalidItemInstanceId);
    REQUIRE(mats->size() == 2);
    REQUIRE((*mats)[0].quantity == 99);
    REQUIRE((*mats)[1].quantity == 6);
}

TEST_CASE("addItem allocates unique stable ids", "[inventory]")
{
    auto reg = makeRegistry();
    Inventory inv;

    const auto id_a = InventoryOps::addItem(inv, makeItem("config/items/weapons/shiv.json"), reg);
    const auto id_b =
        InventoryOps::addItem(inv, makeItem("config/items/weapons/iron_sword.json"), reg);
    REQUIRE(id_a != kInvalidItemInstanceId);
    REQUIRE(id_b != kInvalidItemInstanceId);
    REQUIRE(id_a != id_b);
}

// ---------------------------------------------------------------------------
// findById tests
// ---------------------------------------------------------------------------

TEST_CASE("findById resolves across category buckets", "[inventory]")
{
    auto reg = makeRegistry();
    Inventory inv;

    const auto weapon_id =
        InventoryOps::addItem(inv, makeItem("config/items/weapons/shiv.json"), reg);
    const auto mat_id =
        InventoryOps::addItem(inv, makeItem("config/items/materials/scrap_metal.json", 5), reg);

    const auto* weapon = InventoryOps::findById(inv, weapon_id);
    REQUIRE(weapon != nullptr);
    REQUIRE(weapon->config_path == "config/items/weapons/shiv.json");

    const auto* mat = InventoryOps::findById(inv, mat_id);
    REQUIRE(mat != nullptr);
    REQUIRE(mat->config_path == "config/items/materials/scrap_metal.json");
    REQUIRE(mat->quantity == 5);
}

TEST_CASE("findById returns nullptr for invalid id", "[inventory]")
{
    Inventory inv;
    REQUIRE(InventoryOps::findById(inv, kInvalidItemInstanceId) == nullptr);
    REQUIRE(InventoryOps::findById(inv, 12345) == nullptr);
}

// ---------------------------------------------------------------------------
// removeItem tests
// ---------------------------------------------------------------------------

TEST_CASE("removeItem by id removes from the right bucket", "[inventory]")
{
    auto reg = makeRegistry();
    Inventory inv;
    Equipment equip;

    const auto shiv_id =
        InventoryOps::addItem(inv, makeItem("config/items/weapons/shiv.json"), reg);
    InventoryOps::addItem(inv, makeItem("config/items/weapons/iron_sword.json"), reg);

    REQUIRE(InventoryOps::removeItem(inv, equip, shiv_id));

    const auto* weapons = bucket(inv, "weapons");
    REQUIRE(weapons != nullptr);
    REQUIRE(weapons->size() == 1);
    REQUIRE((*weapons)[0].config_path == "config/items/weapons/iron_sword.json");
}

TEST_CASE("removeItem invalid id returns false", "[inventory]")
{
    Inventory inv;
    Equipment equip;
    REQUIRE_FALSE(InventoryOps::removeItem(inv, equip, kInvalidItemInstanceId));
    REQUIRE_FALSE(InventoryOps::removeItem(inv, equip, 999));
}

TEST_CASE("removeItem clears equipment ref to the removed item", "[inventory]")
{
    auto reg = makeRegistry();
    Inventory inv;
    Equipment equip;

    const auto id = InventoryOps::addItem(inv, makeItem("config/items/weapons/shiv.json"), reg);
    InventoryOps::equipItemToSlot(inv, equip, id, EquipSlot::RightHand);
    REQUIRE(equip.right_hand == id);

    InventoryOps::removeItem(inv, equip, id);
    REQUIRE(equip.right_hand == kInvalidItemInstanceId);
}

// ---------------------------------------------------------------------------
// equipItemToSlot tests
// ---------------------------------------------------------------------------

TEST_CASE("equipItemToSlot weapon goes to right_hand", "[inventory]")
{
    auto reg = makeRegistry();
    Inventory inv;
    Equipment equip;

    const auto id = InventoryOps::addItem(inv, makeItem("config/items/weapons/shiv.json"), reg);
    REQUIRE(InventoryOps::equipItemToSlot(inv, equip, id, EquipSlot::RightHand));

    REQUIRE(equip.right_hand == id);
    REQUIRE(InventoryOps::equippedPath(inv, equip, EquipSlot::RightHand) ==
            "config/items/weapons/shiv.json");
}

TEST_CASE("equipItemToSlot swap unequips old item", "[inventory]")
{
    auto reg = makeRegistry();
    Inventory inv;
    Equipment equip;

    const auto shiv_id =
        InventoryOps::addItem(inv, makeItem("config/items/weapons/shiv.json"), reg);
    const auto sword_id =
        InventoryOps::addItem(inv, makeItem("config/items/weapons/iron_sword.json"), reg);
    InventoryOps::equipItemToSlot(inv, equip, shiv_id, EquipSlot::RightHand);

    // Equip a different item in the same slot.
    REQUIRE(InventoryOps::equipItemToSlot(inv, equip, sword_id, EquipSlot::RightHand));

    REQUIRE(equip.right_hand == sword_id);
    REQUIRE(InventoryOps::equippedPath(inv, equip, EquipSlot::RightHand) ==
            "config/items/weapons/iron_sword.json");
    // Old shiv is still in inventory, just no longer equipped.
    REQUIRE(InventoryOps::findById(inv, shiv_id) != nullptr);
}

TEST_CASE("equipItemToSlot shield to left hand", "[inventory]")
{
    auto reg = makeRegistry();
    Inventory inv;
    Equipment equip;

    const auto id =
        InventoryOps::addItem(inv, makeItem("config/items/armor/wooden_shield.json"), reg);
    REQUIRE(InventoryOps::equipItemToSlot(inv, equip, id, EquipSlot::LeftHand));

    REQUIRE(InventoryOps::equippedPath(inv, equip, EquipSlot::LeftHand) ==
            "config/items/armor/wooden_shield.json");
    REQUIRE(InventoryOps::slotEmpty(equip, EquipSlot::RightHand));
}

TEST_CASE("equipItemToSlot armor goes to chest slot", "[inventory]")
{
    auto reg = makeRegistry();
    Inventory inv;
    Equipment equip;

    const auto id =
        InventoryOps::addItem(inv, makeItem("config/items/armor/leather_chest.json"), reg);
    REQUIRE(InventoryOps::equipItemToSlot(inv, equip, id, EquipSlot::Chest));

    REQUIRE(InventoryOps::equippedPath(inv, equip, EquipSlot::Chest) ==
            "config/items/armor/leather_chest.json");
}

TEST_CASE("equipItemToSlot two-handed weapon does not clear left_hand", "[inventory]")
{
    // With the runtime two-hand toggle, equipping a two-handed-capable
    // weapon leaves the left hand occupied. Suppression happens at render /
    // behavior time when the player toggles two_handed_active on.
    auto reg = makeRegistry();
    Inventory inv;
    Equipment equip;

    const auto shield_id =
        InventoryOps::addItem(inv, makeItem("config/items/armor/wooden_shield.json"), reg);
    const auto sword_id =
        InventoryOps::addItem(inv, makeItem("config/items/weapons/iron_sword.json"), reg);
    InventoryOps::equipItemToSlot(inv, equip, shield_id, EquipSlot::LeftHand);
    REQUIRE_FALSE(InventoryOps::slotEmpty(equip, EquipSlot::LeftHand));

    REQUIRE(InventoryOps::equipItemToSlot(inv, equip, sword_id, EquipSlot::RightHand));

    REQUIRE(InventoryOps::equippedPath(inv, equip, EquipSlot::RightHand) ==
            "config/items/weapons/iron_sword.json");
    REQUIRE_FALSE(InventoryOps::slotEmpty(equip, EquipSlot::LeftHand));
}

TEST_CASE("equipItemToSlot rejects unknown id", "[inventory]")
{
    Inventory inv;
    Equipment equip;
    REQUIRE_FALSE(InventoryOps::equipItemToSlot(inv, equip, 42, EquipSlot::RightHand));
    REQUIRE(InventoryOps::slotEmpty(equip, EquipSlot::RightHand));
}

// ---------------------------------------------------------------------------
// unequipSlot tests
// ---------------------------------------------------------------------------

TEST_CASE("unequipSlot clears the slot", "[inventory]")
{
    auto reg = makeRegistry();
    Inventory inv;
    Equipment equip;

    const auto id = InventoryOps::addItem(inv, makeItem("config/items/weapons/shiv.json"), reg);
    InventoryOps::equipItemToSlot(inv, equip, id, EquipSlot::RightHand);
    REQUIRE_FALSE(InventoryOps::slotEmpty(equip, EquipSlot::RightHand));

    InventoryOps::unequipSlot(equip, EquipSlot::RightHand);
    REQUIRE(InventoryOps::slotEmpty(equip, EquipSlot::RightHand));
    // Item stays in inventory.
    REQUIRE(InventoryOps::findById(inv, id) != nullptr);
}

TEST_CASE("unequipSlot on empty slot is a no-op", "[inventory]")
{
    Equipment equip;
    InventoryOps::unequipSlot(equip, EquipSlot::RightHand);
    REQUIRE(InventoryOps::slotEmpty(equip, EquipSlot::RightHand));
}

// ---------------------------------------------------------------------------
// addWithId / save-load round-trip semantics
// ---------------------------------------------------------------------------

TEST_CASE("addWithId preserves the saved id verbatim", "[inventory]")
{
    auto reg = makeRegistry();
    Inventory inv;

    ItemInstance saved;
    saved.id = 42;
    saved.config_path = "config/items/weapons/shiv.json";
    saved.quantity = 1;
    InventoryOps::addWithId(inv, saved, reg);

    const auto* found = InventoryOps::findById(inv, 42);
    REQUIRE(found != nullptr);
    REQUIRE(found->id == 42);
    REQUIRE(found->config_path == "config/items/weapons/shiv.json");
}

TEST_CASE("addWithId advances next_id past loaded ids", "[inventory]")
{
    auto reg = makeRegistry();
    Inventory inv;

    ItemInstance saved;
    saved.id = 100;
    saved.config_path = "config/items/weapons/shiv.json";
    saved.quantity = 1;
    InventoryOps::addWithId(inv, saved, reg);

    // A fresh add must allocate an id strictly greater than the loaded one.
    const auto next_id =
        InventoryOps::addItem(inv, makeItem("config/items/weapons/iron_sword.json"), reg);
    REQUIRE(next_id > 100);
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

// ---------------------------------------------------------------------------
// equipItemToSlot single-slot uniqueness
// ---------------------------------------------------------------------------

TEST_CASE("equipItemToSlot moves the same id between slots (single-slot uniqueness)", "[inventory]")
{
    // Equipping the same item to a second slot must clear the first
    // slot. The item is one physical thing -- it cannot occupy both
    // hands simultaneously.
    auto reg = makeRegistry();
    Inventory inv;
    Equipment equip;

    const auto shiv_id =
        InventoryOps::addItem(inv, makeItem("config/items/weapons/shiv.json"), reg);
    REQUIRE(InventoryOps::equipItemToSlot(inv, equip, shiv_id, EquipSlot::RightHand));
    REQUIRE_FALSE(InventoryOps::slotEmpty(equip, EquipSlot::RightHand));

    // Equip the SAME id to the left hand. Right hand must be cleared.
    REQUIRE(InventoryOps::equipItemToSlot(inv, equip, shiv_id, EquipSlot::LeftHand));
    REQUIRE_FALSE(InventoryOps::slotEmpty(equip, EquipSlot::LeftHand));
    REQUIRE(InventoryOps::slotEmpty(equip, EquipSlot::RightHand));
}

// ---------------------------------------------------------------------------
// consumeItems partial-consumption contract
// ---------------------------------------------------------------------------

TEST_CASE("consumeItems partial consumption commits and returns false", "[inventory]")
{
    // Per the locked contract: when inventory has SOME but not all of
    // the requested quantity, consumeItems consumes everything
    // available, leaves inventory empty, and returns false. Callers
    // that need atomic consume-or-nothing must precheck with countItem.
    auto reg = makeRegistry();
    Inventory inv;
    Equipment equip;

    InventoryOps::addItem(inv, makeItem("config/items/materials/scrap_metal.json", 3), reg);
    REQUIRE(InventoryOps::countItem(inv, "config/items/materials/scrap_metal.json") == 3);

    // Request 5; only 3 available.
    REQUIRE_FALSE(
        InventoryOps::consumeItems(inv, equip, "config/items/materials/scrap_metal.json", 5));
    // All 3 still consumed despite returning false.
    REQUIRE(InventoryOps::countItem(inv, "config/items/materials/scrap_metal.json") == 0);
}
