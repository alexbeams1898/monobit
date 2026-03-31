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
    inv.items.push_back({"mat_a", QualityTier::Common, 100.0f, 3});
    inv.items.push_back({"mat_b", QualityTier::Common, 100.0f, 5});
    inv.items.push_back({"mat_a", QualityTier::Common, 100.0f, 2});

    REQUIRE(InventoryOps::consumeItems(inv, "mat_a", 4));
    REQUIRE(InventoryOps::countItem(inv, "mat_a") == 1);

    // Consuming more than available fails.
    REQUIRE_FALSE(InventoryOps::consumeItems(inv, "mat_a", 10));
}

TEST_CASE("InventoryOps::canEvolve checks level and materials", "[evolution]")
{
    Inventory inv;
    Equipment equip;
    WeaponXP wxp;
    wxp.level = 3;

    equip.main_hand.config_path = "config/items/weapons/shiv.json";

    EvolutionPath path;
    path.target_node = "dagger";
    path.min_level = 5;
    path.material_config_path = "config/items/materials/bone_shard.json";
    path.material_qty = 1;

    // Level too low.
    REQUIRE_FALSE(InventoryOps::canEvolve(inv, equip, wxp, path));

    // Level high enough but no materials.
    wxp.level = 5;
    REQUIRE_FALSE(InventoryOps::canEvolve(inv, equip, wxp, path));

    // Add materials.
    inv.items.push_back({"config/items/materials/bone_shard.json", QualityTier::Common, 100.0f, 1});
    REQUIRE(InventoryOps::canEvolve(inv, equip, wxp, path));
}

TEST_CASE("InventoryOps::canEvolve flat upgrade needs no materials", "[evolution]")
{
    const Inventory inv;
    Equipment equip;
    WeaponXP wxp;
    wxp.level = 5;
    equip.main_hand.config_path = "config/items/weapons/shiv.json";

    EvolutionPath path;
    path.target_node = "dagger";
    path.min_level = 5;
    // No material required (flat upgrade).

    REQUIRE(InventoryOps::canEvolve(inv, equip, wxp, path));
}

TEST_CASE("InventoryOps::evolveWeapon replaces weapon and resets XP", "[evolution]")
{
    Inventory inv;
    inv.items.push_back({"config/items/materials/bone_shard.json", QualityTier::Common, 100.0f, 2});

    Equipment equip;
    equip.main_hand.config_path = "config/items/weapons/shiv.json";
    equip.synced_main_hand = "config/items/weapons/shiv.json";

    WeaponXP wxp;
    wxp.level = 7;
    wxp.current_xp = 150.0f;
    wxp.xp_to_next = 200.0f;

    EvolutionPath path;
    path.target_node = "dagger";
    path.min_level = 5;
    path.material_config_path = "config/items/materials/bone_shard.json";
    path.material_qty = 1;

    const ItemRegistry registry;
    const float carry_factor = 0.15f;

    REQUIRE(InventoryOps::evolveWeapon(inv, equip, wxp, path, "config/items/weapons/dagger.json",
                                       registry, carry_factor));

    // Weapon replaced.
    REQUIRE(equip.main_hand.config_path == "config/items/weapons/dagger.json");

    // synced_main_hand cleared to force EquipmentSystem re-sync.
    REQUIRE(equip.synced_main_hand.empty());

    // Carry-forward bonus computed: old_bonus(0) + old_level(7) * carry_factor(0.15).
    REQUIRE_THAT(equip.main_hand.evolution_bonus, WithinAbs(1.05f, 0.01f));

    // Newly discovered flag set.
    REQUIRE(equip.main_hand.newly_discovered);

    // WeaponXP reset.
    REQUIRE(wxp.level == 1);
    REQUIRE_THAT(wxp.current_xp, WithinAbs(0.0f, 0.01f));

    // Material consumed (had 2, used 1).
    REQUIRE(InventoryOps::countItem(inv, "config/items/materials/bone_shard.json") == 1);
}

TEST_CASE("InventoryOps::evolveWeapon accumulates carry-forward bonus", "[evolution]")
{
    Inventory inv;
    Equipment equip;
    equip.main_hand.config_path = "config/items/weapons/shiv.json";
    equip.main_hand.evolution_bonus = 2.0f; // from a prior evolution

    WeaponXP wxp;
    wxp.level = 10;

    EvolutionPath path;
    path.target_node = "dagger";
    path.min_level = 5;
    // Flat upgrade, no materials.

    const ItemRegistry registry;
    const float carry_factor = 0.15f;

    REQUIRE(InventoryOps::evolveWeapon(inv, equip, wxp, path, "config/items/weapons/dagger.json",
                                       registry, carry_factor));

    // bonus = old_bonus(2.0) + old_level(10) * carry_factor(0.15) = 2.0 + 1.5 = 3.5
    REQUIRE_THAT(equip.main_hand.evolution_bonus, WithinAbs(3.5f, 0.01f));
}
