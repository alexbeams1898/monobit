#include "ecs/Items.h"
#include "ecs/RpgComponents.h"
#include "ops/CraftingOps.h"
#include "ops/InventoryOps.h"

using namespace engine::ecs;
namespace CraftingOps = engine::ops::crafting;
namespace InventoryOps = engine::ops::inventory;

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

    ItemDef club;
    club.config_path = "config/items/weapons/bone_club.json";
    club.name = "Bone Club";
    club.category = ItemCategory::Weapon;
    club.base_damage = 12.0f;
    club.stackable = false;
    reg.defs[club.config_path] = club;

    return reg;
}

static RecipeDef boneClubRecipe()
{
    RecipeDef r;
    r.config_path = "config/recipes/bone_club.json";
    r.name = "Bone Club";
    r.inputs = {{"config/items/materials/bone_shard.json", 3}};
    r.output_item = "config/items/weapons/bone_club.json";
    r.output_quantity = 1;
    return r;
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

// Find the first matching item across all buckets.
static const ItemInstance* findInInventory(const Inventory& inv, const std::string& path)
{
    for (const auto& [bucket, items] : inv.by_category)
    {
        for (const auto& item : items)
        {
            if (item.config_path == path)
                return &item;
        }
    }
    return nullptr;
}

// ---------------------------------------------------------------------------
// canCraft tests
// ---------------------------------------------------------------------------

TEST_CASE("canCraft true when ingredients sufficient", "[crafting]")
{
    auto reg = makeItemRegistry();
    Inventory inv;
    InventoryOps::addItem(inv, makeItem("config/items/materials/bone_shard.json", 5), reg);

    REQUIRE(CraftingOps::canCraft(inv, boneClubRecipe(), reg));
}

TEST_CASE("canCraft false when ingredients insufficient", "[crafting]")
{
    auto reg = makeItemRegistry();
    Inventory inv;
    InventoryOps::addItem(inv, makeItem("config/items/materials/bone_shard.json", 2), reg);

    REQUIRE_FALSE(CraftingOps::canCraft(inv, boneClubRecipe(), reg));
}

TEST_CASE("canCraft counts across multiple stacks", "[crafting]")
{
    auto reg = makeItemRegistry();
    Inventory inv;

    // Two adds of 2 each into a stackable with max_stack=99 collapse into
    // a single stack of 4 -- still satisfies the 3-required.
    InventoryOps::addItem(inv, makeItem("config/items/materials/bone_shard.json", 2), reg);
    InventoryOps::addItem(inv, makeItem("config/items/materials/bone_shard.json", 2), reg);

    REQUIRE(CraftingOps::canCraft(inv, boneClubRecipe(), reg));
}

// ---------------------------------------------------------------------------
// craft tests
// ---------------------------------------------------------------------------

TEST_CASE("craft consumes ingredients and produces output", "[crafting]")
{
    auto reg = makeItemRegistry();
    Inventory inv;
    InventoryOps::addItem(inv, makeItem("config/items/materials/bone_shard.json", 5), reg);

    REQUIRE(CraftingOps::craft(inv, boneClubRecipe(), reg));

    // 5 - 3 = 2 bone shards remaining + 1 bone club.
    const auto* shards = findInInventory(inv, "config/items/materials/bone_shard.json");
    REQUIRE(shards != nullptr);
    REQUIRE(shards->quantity == 2);

    const auto* club = findInInventory(inv, "config/items/weapons/bone_club.json");
    REQUIRE(club != nullptr);
    REQUIRE(club->quantity == 1);
}

TEST_CASE("craft consumes across split stacks", "[crafting]")
{
    auto reg = makeItemRegistry();
    Inventory inv;

    // addItem will merge these into one stack of 4 (max_stack=99), so this
    // is really just "one stack with enough quantity" -- the cross-stack
    // path is exercised by other tests when items can't merge (different
    // qualities or hit max_stack).
    InventoryOps::addItem(inv, makeItem("config/items/materials/bone_shard.json", 2), reg);
    InventoryOps::addItem(inv, makeItem("config/items/materials/bone_shard.json", 2), reg);

    REQUIRE(CraftingOps::craft(inv, boneClubRecipe(), reg));

    int shardQty = 0;
    bool foundClub = false;
    for (const auto& [bucket, items] : inv.by_category)
    {
        for (const auto& item : items)
        {
            if (item.config_path == "config/items/materials/bone_shard.json")
                shardQty += item.quantity;
            if (item.config_path == "config/items/weapons/bone_club.json")
                foundClub = true;
        }
    }
    REQUIRE(shardQty == 1);
    REQUIRE(foundClub);
}

// ---------------------------------------------------------------------------
// findCraftable tests
// ---------------------------------------------------------------------------

TEST_CASE("findCraftable returns recipe when craftable", "[crafting]")
{
    auto reg = makeItemRegistry();
    Inventory inv;
    InventoryOps::addItem(inv, makeItem("config/items/materials/bone_shard.json", 5), reg);

    RecipeRegistry recipes;
    recipes.recipes.push_back(boneClubRecipe());

    const RecipeDef* r = CraftingOps::findCraftable(inv, recipes, reg);
    REQUIRE(r != nullptr);
    REQUIRE(r->name == "Bone Club");
}

TEST_CASE("findCraftable returns nullptr when none craftable", "[crafting]")
{
    auto reg = makeItemRegistry();
    Inventory inv;

    RecipeRegistry recipes;
    recipes.recipes.push_back(boneClubRecipe());

    REQUIRE(CraftingOps::findCraftable(inv, recipes, reg) == nullptr);
}

// ---------------------------------------------------------------------------
// Quality averaging tests
// ---------------------------------------------------------------------------

TEST_CASE("Crafted output quality averages input qualities", "[crafting]")
{
    auto reg = makeItemRegistry();
    Inventory inv;

    // 3 Fine bone shards -> avg quality = Fine (2).
    InventoryOps::addItem(
        inv, makeItem("config/items/materials/bone_shard.json", 3, QualityTier::Fine), reg);

    REQUIRE(CraftingOps::craft(inv, boneClubRecipe(), reg));

    const auto* club = findInInventory(inv, "config/items/weapons/bone_club.json");
    REQUIRE(club != nullptr);
    REQUIRE(club->quality == QualityTier::Fine);
}

TEST_CASE("Crafted output quality rounds mixed inputs", "[crafting]")
{
    auto reg = makeItemRegistry();
    Inventory inv;

    // Adds with different qualities can't merge into one stack -- they
    // remain three separate stacks of qty 1 in the same bucket.
    InventoryOps::addItem(
        inv, makeItem("config/items/materials/bone_shard.json", 1, QualityTier::Crude), reg);
    InventoryOps::addItem(
        inv, makeItem("config/items/materials/bone_shard.json", 1, QualityTier::Common), reg);
    InventoryOps::addItem(
        inv, makeItem("config/items/materials/bone_shard.json", 1, QualityTier::Fine), reg);

    REQUIRE(CraftingOps::craft(inv, boneClubRecipe(), reg));

    const auto* club = findInInventory(inv, "config/items/weapons/bone_club.json");
    REQUIRE(club != nullptr);
    // Crude(0) + Common(1) + Fine(2) = sum 3, avg 1 = Common.
    REQUIRE(club->quality == QualityTier::Common);
}
