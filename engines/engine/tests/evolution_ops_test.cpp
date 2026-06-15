#include "ecs/Items.h"
#include "ecs/RpgComponents.h"
#include "ops/InventoryOps.h"

using namespace engine::ecs;
namespace InventoryOps = engine::ops::inventory;

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

using Catch::Matchers::WithinAbs;

// Helper: build a registry with weapons + materials needed by these tests.
static ItemRegistry makeRegistry()
{
    ItemRegistry reg;

    ItemDef shiv;
    shiv.config_path = "config/items/weapons/shiv.json";
    shiv.category = ItemCategory::Weapon;
    shiv.stackable = false;
    reg.defs[shiv.config_path] = shiv;

    ItemDef dagger;
    dagger.config_path = "config/items/weapons/dagger.json";
    dagger.category = ItemCategory::Weapon;
    dagger.stackable = false;
    reg.defs[dagger.config_path] = dagger;

    ItemDef bone;
    bone.config_path = "config/items/materials/bone_shard.json";
    bone.category = ItemCategory::Material;
    bone.stackable = true;
    bone.max_stack = 99;
    reg.defs[bone.config_path] = bone;

    ItemDef mat_a;
    mat_a.config_path = "mat_a";
    mat_a.category = ItemCategory::Material;
    mat_a.stackable = true;
    mat_a.max_stack = 99;
    reg.defs[mat_a.config_path] = mat_a;

    ItemDef mat_b;
    mat_b.config_path = "mat_b";
    mat_b.category = ItemCategory::Material;
    mat_b.stackable = true;
    mat_b.max_stack = 99;
    reg.defs[mat_b.config_path] = mat_b;

    return reg;
}

static ItemInstance makeItem(const std::string& path, int qty = 1,
                             QualityTier q = QualityTier::Common)
{
    ItemInstance item;
    item.config_path = path;
    item.quantity = qty;
    item.quality = q;
    return item;
}

TEST_CASE("InventoryOps::countItem counts total quantity across stacks", "[evolution]")
{
    auto reg = makeRegistry();
    Inventory inv;

    InventoryOps::addItem(inv, makeItem("mat_a", 3), reg);
    InventoryOps::addItem(inv, makeItem("mat_b", 1), reg);

    REQUIRE(InventoryOps::countItem(inv, "mat_a") == 3);
    REQUIRE(InventoryOps::countItem(inv, "mat_b") == 1);
    REQUIRE(InventoryOps::countItem(inv, "mat_c") == 0);
}

TEST_CASE("InventoryOps::consumeItems removes correct quantities", "[evolution]")
{
    auto reg = makeRegistry();
    Inventory inv;
    Equipment equip;

    InventoryOps::addItem(inv, makeItem("mat_a", 5), reg);
    InventoryOps::addItem(inv, makeItem("mat_b", 5), reg);

    REQUIRE(InventoryOps::consumeItems(inv, equip, "mat_a", 4));
    REQUIRE(InventoryOps::countItem(inv, "mat_a") == 1);

    // Consuming more than available fails.
    REQUIRE_FALSE(InventoryOps::consumeItems(inv, equip, "mat_a", 10));
}

TEST_CASE("InventoryOps::canEvolve checks level and materials", "[evolution]")
{
    auto reg = makeRegistry();
    Inventory inv;
    Equipment equip;
    Weapon weapon;
    weapon.wxp_level = 3;

    const auto shiv_id =
        InventoryOps::addItem(inv, makeItem("config/items/weapons/shiv.json"), reg);
    InventoryOps::equipItemToSlot(inv, equip, shiv_id, EquipSlot::RightHand);

    EvolutionPath path;
    path.target_node = "dagger";
    path.min_level = 5;
    path.material_config_path = "config/items/materials/bone_shard.json";
    path.material_qty = 1;

    REQUIRE_FALSE(InventoryOps::canEvolve(inv, equip, weapon, path));

    weapon.wxp_level = 5;
    REQUIRE_FALSE(InventoryOps::canEvolve(inv, equip, weapon, path));

    InventoryOps::addItem(inv, makeItem("config/items/materials/bone_shard.json", 1), reg);
    REQUIRE(InventoryOps::canEvolve(inv, equip, weapon, path));
}

TEST_CASE("InventoryOps::canEvolve flat upgrade needs no materials", "[evolution]")
{
    auto reg = makeRegistry();
    Inventory inv;
    Equipment equip;
    Weapon weapon;
    weapon.wxp_level = 5;

    const auto shiv_id =
        InventoryOps::addItem(inv, makeItem("config/items/weapons/shiv.json"), reg);
    InventoryOps::equipItemToSlot(inv, equip, shiv_id, EquipSlot::RightHand);

    EvolutionPath path;
    path.target_node = "dagger";
    path.min_level = 5;

    REQUIRE(InventoryOps::canEvolve(inv, equip, weapon, path));
}

TEST_CASE("InventoryOps::evolveWeapon replaces weapon and resets XP", "[evolution]")
{
    auto reg = makeRegistry();
    Inventory inv;
    Equipment equip;

    InventoryOps::addItem(inv, makeItem("config/items/materials/bone_shard.json", 2), reg);
    const auto shiv_id =
        InventoryOps::addItem(inv, makeItem("config/items/weapons/shiv.json"), reg);
    InventoryOps::equipItemToSlot(inv, equip, shiv_id, EquipSlot::RightHand);

    Weapon weapon;
    weapon.wxp_level = 7;
    weapon.wxp_current = 150.0f;
    weapon.wxp_to_next = 200.0f;

    EvolutionPath path;
    path.target_node = "dagger";
    path.min_level = 5;
    path.material_config_path = "config/items/materials/bone_shard.json";
    path.material_qty = 1;

    const float carry_factor = 0.15f;

    REQUIRE(InventoryOps::evolveWeapon(inv, equip, weapon, path, "config/items/weapons/dagger.json",
                                       reg, carry_factor));

    REQUIRE(InventoryOps::equippedPath(inv, equip, EquipSlot::RightHand) ==
            "config/items/weapons/dagger.json");

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
    auto reg = makeRegistry();
    Inventory inv;
    Equipment equip;

    ItemInstance shiv;
    shiv.config_path = "config/items/weapons/shiv.json";
    shiv.evolution_bonus = 2.0f;
    shiv.quantity = 1;
    const auto shiv_id = InventoryOps::addItem(inv, shiv, reg);
    // addItem ignored the input bonus when copying for stack-merge, but
    // for a non-stackable weapon it preserved the full instance verbatim
    // and assigned a fresh id. Re-fetch to set the bonus on the stored
    // copy without depending on which path addItem took.
    auto* stored = InventoryOps::findByIdMut(inv, shiv_id);
    REQUIRE(stored != nullptr);
    stored->evolution_bonus = 2.0f;
    InventoryOps::equipItemToSlot(inv, equip, shiv_id, EquipSlot::RightHand);

    Weapon weapon;
    weapon.wxp_level = 10;

    EvolutionPath path;
    path.target_node = "dagger";
    path.min_level = 5;

    const float carry_factor = 0.15f;

    REQUIRE(InventoryOps::evolveWeapon(inv, equip, weapon, path, "config/items/weapons/dagger.json",
                                       reg, carry_factor));

    const auto* item = InventoryOps::equippedItem(inv, equip, EquipSlot::RightHand);
    REQUIRE(item != nullptr);
    REQUIRE_THAT(item->evolution_bonus, WithinAbs(3.5f, 0.01f));
}
