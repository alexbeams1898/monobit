// Tests for the inventory -> combat bridge resolver:
// selva::combat::resolveHandWeaponFor. Covers the four branches it
// has to handle correctly so attack chains / animations see the
// right weapon class when the player equips through the inventory UI.

#include "combat/CombatData.h"
#include "ecs/Items.h"
#include "ops/InventoryOps.h"

#include <catch2/catch_test_macros.hpp>

namespace
{

engine::ecs::ItemRegistry makeItemReg()
{
    engine::ecs::ItemRegistry reg;

    engine::ecs::ItemDef sword;
    sword.config_path = "config/items/weapons/iron_sword.json";
    sword.name = "Iron Sword";
    sword.category = engine::ecs::ItemCategory::Weapon;
    sword.weapon_class_id = "sword";
    reg.defs[sword.config_path] = sword;

    engine::ecs::ItemDef material;
    material.config_path = "config/items/materials/scrap.json";
    material.name = "Scrap";
    material.category = engine::ecs::ItemCategory::Material;
    // weapon_class_id intentionally empty -- materials aren't weapons.
    reg.defs[material.config_path] = material;

    engine::ecs::ItemDef weird;
    weird.config_path = "config/items/weapons/orphan.json";
    weird.name = "Orphan";
    weird.category = engine::ecs::ItemCategory::Weapon;
    weird.weapon_class_id = "nonexistent_class";
    reg.defs[weird.config_path] = weird;

    return reg;
}

selva::combat::WeaponClassRegistry makeClassReg()
{
    selva::combat::WeaponClassRegistry reg;
    selva::combat::WeaponClass sword;
    sword.id = "sword";
    reg.by_id["sword"] = sword;
    return reg;
}

} // namespace

TEST_CASE("resolveHandWeaponFor returns nullptr for invalid id", "[combat][bridge]")
{
    const auto items = makeItemReg();
    const auto classes = makeClassReg();
    engine::ecs::Inventory inv;
    const auto* w = selva::combat::resolveHandWeaponFor(
        engine::ecs::kInvalidItemInstanceId, inv, items, classes);
    REQUIRE(w == nullptr);
}

TEST_CASE("resolveHandWeaponFor returns nullptr when item not in inventory",
          "[combat][bridge]")
{
    const auto items = makeItemReg();
    const auto classes = makeClassReg();
    engine::ecs::Inventory inv;
    // Pass an id that doesn't exist in the inventory bucket map.
    const auto* w = selva::combat::resolveHandWeaponFor(
        engine::ecs::ItemInstanceId{42}, inv, items, classes);
    REQUIRE(w == nullptr);
}

TEST_CASE("resolveHandWeaponFor returns nullptr when item has empty weapon_class_id",
          "[combat][bridge]")
{
    auto items = makeItemReg();
    const auto classes = makeClassReg();
    engine::ecs::Inventory inv;

    engine::ecs::ItemInstance inst;
    inst.config_path = "config/items/materials/scrap.json";
    inst.quantity = 1;
    const auto id = engine::ops::inventory::addItem(inv, inst, items);
    REQUIRE(id != engine::ecs::kInvalidItemInstanceId);

    const auto* w = selva::combat::resolveHandWeaponFor(id, inv, items, classes);
    REQUIRE(w == nullptr);
}

TEST_CASE("resolveHandWeaponFor returns nullptr when weapon_class_id is not in class registry",
          "[combat][bridge]")
{
    auto items = makeItemReg();
    const auto classes = makeClassReg();
    engine::ecs::Inventory inv;

    engine::ecs::ItemInstance inst;
    inst.config_path = "config/items/weapons/orphan.json";
    inst.quantity = 1;
    const auto id = engine::ops::inventory::addItem(inv, inst, items);
    REQUIRE(id != engine::ecs::kInvalidItemInstanceId);

    // Orphan has weapon_class_id="nonexistent_class" but the class
    // registry only has "sword".
    const auto* w = selva::combat::resolveHandWeaponFor(id, inv, items, classes);
    REQUIRE(w == nullptr);
}

TEST_CASE("resolveHandWeaponFor returns a Weapon with the correct class for a known weapon",
          "[combat][bridge]")
{
    auto items = makeItemReg();
    const auto classes = makeClassReg();
    engine::ecs::Inventory inv;

    engine::ecs::ItemInstance inst;
    inst.config_path = "config/items/weapons/iron_sword.json";
    inst.quantity = 1;
    const auto id = engine::ops::inventory::addItem(inv, inst, items);
    REQUIRE(id != engine::ecs::kInvalidItemInstanceId);

    const auto* w = selva::combat::resolveHandWeaponFor(id, inv, items, classes);
    REQUIRE(w != nullptr);
    REQUIRE(w->class_id == "sword");
    REQUIRE(w->cls != nullptr);
    REQUIRE(w->cls->id == "sword");
    REQUIRE(w->name == "Iron Sword");
    REQUIRE(w->id == "config/items/weapons/iron_sword.json");
}

TEST_CASE("resolveHandWeaponFor returns stable pointer for same config_path across calls",
          "[combat][bridge]")
{
    // Synthesized weapons are cached by config_path. Two resolve calls
    // for the same id should produce the same pointer so downstream
    // systems can keep a Weapon* across frames.
    auto items = makeItemReg();
    const auto classes = makeClassReg();
    engine::ecs::Inventory inv;

    engine::ecs::ItemInstance inst;
    inst.config_path = "config/items/weapons/iron_sword.json";
    inst.quantity = 1;
    const auto id = engine::ops::inventory::addItem(inv, inst, items);

    const auto* w1 = selva::combat::resolveHandWeaponFor(id, inv, items, classes);
    const auto* w2 = selva::combat::resolveHandWeaponFor(id, inv, items, classes);
    REQUIRE(w1 != nullptr);
    REQUIRE(w1 == w2);
}
