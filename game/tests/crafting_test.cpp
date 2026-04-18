#include "ecs/GameComponents.h"
#include "ecs/GameConfig.h"
#include "ops/CraftingOps.h"

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

static ItemInstance makeItem(const std::string& path, int qty = 1)
{
    ItemInstance item;
    item.config_path = path;
    item.quantity = qty;
    return item;
}

// ---------------------------------------------------------------------------
// canCraft tests
// ---------------------------------------------------------------------------

TEST_CASE("canCraft true when ingredients sufficient", "[crafting]")
{
    auto reg = makeItemRegistry();
    Inventory inv;
    inv.max_slots = 20;
    inv.items.push_back(makeItem("config/items/materials/bone_shard.json", 5));

    REQUIRE(CraftingOps::canCraft(inv, boneClubRecipe(), reg));
}

TEST_CASE("canCraft false when ingredients insufficient", "[crafting]")
{
    auto reg = makeItemRegistry();
    Inventory inv;
    inv.max_slots = 20;
    inv.items.push_back(makeItem("config/items/materials/bone_shard.json", 2));

    REQUIRE_FALSE(CraftingOps::canCraft(inv, boneClubRecipe(), reg));
}

TEST_CASE("canCraft counts across multiple stacks", "[crafting]")
{
    auto reg = makeItemRegistry();
    Inventory inv;
    inv.max_slots = 20;
    inv.items.push_back(makeItem("config/items/materials/bone_shard.json", 2));
    inv.items.push_back(makeItem("config/items/materials/bone_shard.json", 2));

    // 2 + 2 = 4 >= 3 required
    REQUIRE(CraftingOps::canCraft(inv, boneClubRecipe(), reg));
}

// ---------------------------------------------------------------------------
// craft tests
// ---------------------------------------------------------------------------

static const ItemInstance* findInInventory(const Inventory& inv, const std::string& path)
{
    for (const auto& item : inv.items)
        if (item.config_path == path)
            return &item;
    return nullptr;
}

TEST_CASE("craft consumes ingredients and produces output", "[crafting]")
{
    auto reg = makeItemRegistry();
    Inventory inv;
    Equipment equip;
    inv.max_slots = 20;
    inv.items.push_back(makeItem("config/items/materials/bone_shard.json", 5));

    REQUIRE(CraftingOps::craft(inv, equip, boneClubRecipe(), reg));

    // 5 - 3 = 2 bone shards remaining + 1 bone club.
    REQUIRE(inv.items.size() == 2);

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
    Equipment equip;
    inv.max_slots = 20;
    inv.items.push_back(makeItem("config/items/materials/bone_shard.json", 2));
    inv.items.push_back(makeItem("config/items/materials/bone_shard.json", 2));

    REQUIRE(CraftingOps::craft(inv, equip, boneClubRecipe(), reg));

    // 2 + 2 = 4, consumed 3, leaves 1 shard + 1 club.
    int shardQty = 0;
    bool foundClub = false;
    for (const auto& item : inv.items)
    {
        if (item.config_path == "config/items/materials/bone_shard.json")
            shardQty += item.quantity;
        if (item.config_path == "config/items/weapons/bone_club.json")
            foundClub = true;
    }
    REQUIRE(shardQty == 1);
    REQUIRE(foundClub);
}

TEST_CASE("craft fails when inventory full for output", "[crafting]")
{
    auto reg = makeItemRegistry();
    Inventory inv;
    Equipment equip;
    inv.max_slots = 1;
    inv.items.push_back(makeItem("config/items/materials/bone_shard.json", 5));

    // Ingredients consumed first, then addItem fails because slot is still occupied
    // by the shard remainder. craft should return false.
    // After consuming 3, shard has qty=2 (still 1 slot). Adding club needs a 2nd slot.
    REQUIRE_FALSE(CraftingOps::craft(inv, equip, boneClubRecipe(), reg));
}

// ---------------------------------------------------------------------------
// findCraftable tests
// ---------------------------------------------------------------------------

TEST_CASE("findCraftable returns recipe when craftable", "[crafting]")
{
    auto reg = makeItemRegistry();
    Inventory inv;
    inv.max_slots = 20;
    inv.items.push_back(makeItem("config/items/materials/bone_shard.json", 5));

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
    inv.max_slots = 20;

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
    Equipment equip;
    inv.max_slots = 20;

    // 3 Fine bone shards -> avg quality = Fine (2).
    ItemInstance shard;
    shard.config_path = "config/items/materials/bone_shard.json";
    shard.quantity = 3;
    shard.quality = QualityTier::Fine;
    inv.items.push_back(shard);

    REQUIRE(CraftingOps::craft(inv, equip, boneClubRecipe(), reg));

    // Find the crafted weapon.
    bool found = false;
    for (const auto& item : inv.items)
    {
        if (item.config_path == "config/items/weapons/bone_club.json")
        {
            REQUIRE(item.quality == QualityTier::Fine);
            found = true;
        }
    }
    REQUIRE(found);
}

TEST_CASE("Crafted output quality rounds mixed inputs", "[crafting]")
{
    auto reg = makeItemRegistry();
    Inventory inv;
    Equipment equip;
    inv.max_slots = 20;

    // Crude(0) + Common(1) + Fine(2) = sum 3, avg 1 = Common.
    ItemInstance s1;
    s1.config_path = "config/items/materials/bone_shard.json";
    s1.quantity = 1;
    s1.quality = QualityTier::Crude;
    inv.items.push_back(s1);

    ItemInstance s2;
    s2.config_path = "config/items/materials/bone_shard.json";
    s2.quantity = 1;
    s2.quality = QualityTier::Common;
    inv.items.push_back(s2);

    ItemInstance s3;
    s3.config_path = "config/items/materials/bone_shard.json";
    s3.quantity = 1;
    s3.quality = QualityTier::Fine;
    inv.items.push_back(s3);

    REQUIRE(CraftingOps::craft(inv, equip, boneClubRecipe(), reg));

    bool found = false;
    for (const auto& item : inv.items)
    {
        if (item.config_path == "config/items/weapons/bone_club.json")
        {
            REQUIRE(item.quality == QualityTier::Common);
            found = true;
        }
    }
    REQUIRE(found);
}
