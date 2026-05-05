#include "ecs/GameComponents.h"
#include "ecs/GameConfig.h"
#include "ops/InventoryOps.h"
#include "test_helpers.h"

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

using Catch::Matchers::WithinAbs;

TEST_CASE("InventoryOps::countItem counts total quantity", "[evolution]")
{
    Inventory inv;
    inv.items.push_back({"mat_a", QualityTier::Common, 100.0f, 3});
    inv.items.push_back({"mat_b", QualityTier::Common, 100.0f, 1});
    inv.items.push_back({"mat_a", QualityTier::Common, 100.0f, 2});

    REQUIRE(InventoryOps::countItem(inv, "mat_a") == 5);
    REQUIRE(InventoryOps::countItem(inv, "mat_b") == 1);
    REQUIRE(InventoryOps::countItem(inv, "mat_c") == 0);
}

TEST_CASE("InventoryOps::consumeItems removes correct quantities", "[evolution]")
{
    Inventory inv;
    Equipment equip;
    inv.items.push_back({"mat_a", QualityTier::Common, 100.0f, 3});
    inv.items.push_back({"mat_b", QualityTier::Common, 100.0f, 5});
    inv.items.push_back({"mat_a", QualityTier::Common, 100.0f, 2});

    REQUIRE(InventoryOps::consumeItems(inv, equip, "mat_a", 4));
    REQUIRE(InventoryOps::countItem(inv, "mat_a") == 1);

    // Consuming more than available fails.
    REQUIRE_FALSE(InventoryOps::consumeItems(inv, equip, "mat_a", 10));
}

TEST_CASE("InventoryOps::canEvolve checks level and materials", "[evolution]")
{
    Inventory inv;
    Equipment equip;
    Weapon weapon;
    weapon.wxp_level = 3;

    inv.items.push_back({"config/items/weapons/shiv.json"});
    equip.right_hand = 0;

    EvolutionPath path;
    path.target_node = "dagger";
    path.min_level = 5;
    path.material_config_path = "config/items/materials/bone_shard.json";
    path.material_qty = 1;

    REQUIRE_FALSE(InventoryOps::canEvolve(inv, equip, weapon, path));

    weapon.wxp_level = 5;
    REQUIRE_FALSE(InventoryOps::canEvolve(inv, equip, weapon, path));

    inv.items.push_back({"config/items/materials/bone_shard.json", QualityTier::Common, 100.0f, 1});
    REQUIRE(InventoryOps::canEvolve(inv, equip, weapon, path));
}

TEST_CASE("InventoryOps::canEvolve flat upgrade needs no materials", "[evolution]")
{
    Inventory inv;
    Equipment equip;
    Weapon weapon;
    weapon.wxp_level = 5;

    inv.items.push_back({"config/items/weapons/shiv.json"});
    equip.right_hand = 0;

    EvolutionPath path;
    path.target_node = "dagger";
    path.min_level = 5;

    REQUIRE(InventoryOps::canEvolve(inv, equip, weapon, path));
}

TEST_CASE("InventoryOps::evolveWeapon replaces weapon and resets XP", "[evolution]")
{
    Inventory inv;
    Equipment equip;

    inv.items.push_back({"config/items/materials/bone_shard.json", QualityTier::Common, 100.0f, 2});
    inv.items.push_back({"config/items/weapons/shiv.json"});
    equip.right_hand = 1;
    equip.synced_right_hand = "config/items/weapons/shiv.json";

    Weapon weapon;
    weapon.wxp_level = 7;
    weapon.wxp_current = 150.0f;
    weapon.wxp_to_next = 200.0f;

    EvolutionPath path;
    path.target_node = "dagger";
    path.min_level = 5;
    path.material_config_path = "config/items/materials/bone_shard.json";
    path.material_qty = 1;

    const ItemRegistry registry;
    const float carry_factor = 0.15f;

    REQUIRE(InventoryOps::evolveWeapon(inv, equip, weapon, path, "config/items/weapons/dagger.json",
                                       registry, carry_factor));

    REQUIRE(InventoryOps::equippedPath(inv, equip, EquipSlot::RightHand) ==
            "config/items/weapons/dagger.json");
    REQUIRE(equip.synced_right_hand.empty());

    const auto* item = InventoryOps::equippedItem(inv, equip, EquipSlot::RightHand);
    REQUIRE(item != nullptr);
    REQUIRE_THAT(item->evolution_bonus, WithinAbs(1.05f, 0.01f));
    REQUIRE(item->newly_discovered);

    REQUIRE(weapon.wxp_level == 1);
    REQUIRE_THAT(weapon.wxp_current, WithinAbs(0.0f, 0.01f));

    REQUIRE(InventoryOps::countItem(inv, "config/items/materials/bone_shard.json") == 1);
}

TEST_CASE("InventoryOps::evolveWeapon accumulates carry-forward bonus", "[evolution]")
{
    Inventory inv;
    Equipment equip;

    ItemInstance shiv;
    shiv.config_path = "config/items/weapons/shiv.json";
    shiv.evolution_bonus = 2.0f;
    inv.items.push_back(shiv);
    equip.right_hand = 0;

    Weapon weapon;
    weapon.wxp_level = 10;

    EvolutionPath path;
    path.target_node = "dagger";
    path.min_level = 5;

    const ItemRegistry registry;
    const float carry_factor = 0.15f;

    REQUIRE(InventoryOps::evolveWeapon(inv, equip, weapon, path, "config/items/weapons/dagger.json",
                                       registry, carry_factor));

    const auto* item = InventoryOps::equippedItem(inv, equip, EquipSlot::RightHand);
    REQUIRE(item != nullptr);
    REQUIRE_THAT(item->evolution_bonus, WithinAbs(3.5f, 0.01f));
}
