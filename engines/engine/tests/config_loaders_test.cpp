// JSON-loader contract tests for engine::ecs::loadItemRegistry and
// engine::ecs::loadRecipeRegistry. Each test writes a fixture JSON
// directory to a tmp path, runs the loader, asserts the parsed
// ItemDef / RecipeDef / category fields match expectations.

#include "ecs/ConfigLoaders.h"
#include "ecs/Items.h"

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <string>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

using Catch::Approx;

namespace
{

namespace fs = std::filesystem;

const fs::path kTmpRoot = "tmp/engine-config-loaders-tests";

std::string writeFile(const std::string& subdir, const std::string& name, const std::string& body)
{
    const fs::path dir = kTmpRoot / subdir;
    std::error_code ec;
    fs::create_directories(dir, ec);
    REQUIRE(!ec);
    const std::string path = (dir / name).generic_string();
    std::ofstream out(path);
    REQUIRE(out.is_open());
    out << body;
    out.close();
    return path;
}

void clearDir(const std::string& subdir)
{
    std::error_code ec;
    fs::remove_all(kTmpRoot / subdir, ec);
}

} // namespace

TEST_CASE("ItemDef loads new 7-axis scaling + requirement fields", "[engine][config][item]")
{
    clearDir("item7axis");
    const std::string body = R"({
        "name": "Test axes",
        "category": "weapon",
        "rarity": "common",
        "base_damage": 10.0,
        "weight": 1.5,
        "str_scaling": 0.5,
        "dex_scaling": 0.6,
        "end_scaling": 0.7,
        "lck_scaling": 0.1,
        "per_scaling": 0.2,
        "cog_scaling": 0.3,
        "int_scaling": 0.4,
        "str_requirement": 5,
        "dex_requirement": 6,
        "end_requirement": 7,
        "lck_requirement": 1,
        "per_requirement": 2,
        "cog_requirement": 3,
        "int_requirement": 4
    })";
    writeFile("item7axis", "test_weapon.json", body);

    engine::ecs::ItemRegistry reg;
    const int n = engine::ecs::loadItemRegistry(reg, (kTmpRoot / "item7axis").generic_string());
    REQUIRE(n == 1);
    REQUIRE(reg.defs.size() == 1);

    const auto& def = reg.defs.begin()->second;
    REQUIRE(def.name == "Test axes");
    REQUIRE(def.category == engine::ecs::ItemCategory::Weapon);
    REQUIRE(def.base_damage == Approx(10.0f));
    REQUIRE(def.str_scaling == Approx(0.5f));
    REQUIRE(def.dex_scaling == Approx(0.6f));
    REQUIRE(def.end_scaling == Approx(0.7f));
    REQUIRE(def.lck_scaling == Approx(0.1f));
    REQUIRE(def.per_scaling == Approx(0.2f));
    REQUIRE(def.cog_scaling == Approx(0.3f));
    REQUIRE(def.int_scaling == Approx(0.4f));
    REQUIRE(def.str_requirement == 5);
    REQUIRE(def.dex_requirement == 6);
    REQUIRE(def.end_requirement == 7);
    REQUIRE(def.lck_requirement == 1);
    REQUIRE(def.per_requirement == 2);
    REQUIRE(def.cog_requirement == 3);
    REQUIRE(def.int_requirement == 4);
}

TEST_CASE("ItemDef fields default to zero when absent from JSON", "[engine][config][item]")
{
    clearDir("itemmin");
    const std::string body = R"({
        "name": "Minimal",
        "category": "material",
        "rarity": "common"
    })";
    writeFile("itemmin", "minimal.json", body);

    engine::ecs::ItemRegistry reg;
    REQUIRE(engine::ecs::loadItemRegistry(reg, (kTmpRoot / "itemmin").generic_string()) == 1);
    const auto& def = reg.defs.begin()->second;
    REQUIRE(def.end_scaling == Approx(0.0f));
    REQUIRE(def.lck_scaling == Approx(0.0f));
    REQUIRE(def.per_scaling == Approx(0.0f));
    REQUIRE(def.cog_scaling == Approx(0.0f));
    REQUIRE(def.int_scaling == Approx(0.0f));
    REQUIRE(def.end_requirement == 0);
    REQUIRE(def.per_requirement == 0);
    REQUIRE(def.weapon_class_id.empty());
    REQUIRE(def.visual_weapon.empty());
    REQUIRE(def.world_mesh.empty());
    REQUIRE(def.grip_offset_x == Approx(0.0f));
    REQUIRE(def.grip_offset_y == Approx(0.0f));
    REQUIRE(def.grip_offset_z == Approx(0.0f));
    REQUIRE(def.grip_rot_deg_x == Approx(0.0f));
    REQUIRE(def.grip_rot_deg_y == Approx(0.0f));
    REQUIRE(def.grip_rot_deg_z == Approx(0.0f));
    REQUIRE(def.grip_scale == Approx(1.0f));
}

TEST_CASE("ItemDef loads grip_offset / grip_rot / grip_scale fields",
          "[engine][config][item][grip]")
{
    clearDir("itemgrip");
    const std::string body = R"({
        "name": "Grippy",
        "category": "weapon",
        "rarity": "common",
        "visual_weapon": "assets/weapons/test/test.gltf",
        "weapon_class_id": "sword",
        "grip_offset_x": -0.065,
        "grip_offset_y": 0.030,
        "grip_offset_z": 0.015,
        "grip_rot_deg_x": 12.5,
        "grip_rot_deg_y": -6.2,
        "grip_rot_deg_z": -67.4,
        "grip_scale": 1.25
    })";
    writeFile("itemgrip", "grippy.json", body);

    engine::ecs::ItemRegistry reg;
    REQUIRE(engine::ecs::loadItemRegistry(reg, (kTmpRoot / "itemgrip").generic_string()) == 1);
    const auto& def = reg.defs.begin()->second;
    REQUIRE(def.visual_weapon == "assets/weapons/test/test.gltf");
    REQUIRE(def.weapon_class_id == "sword");
    REQUIRE(def.grip_offset_x == Approx(-0.065f));
    REQUIRE(def.grip_offset_y == Approx(0.030f));
    REQUIRE(def.grip_offset_z == Approx(0.015f));
    REQUIRE(def.grip_rot_deg_x == Approx(12.5f));
    REQUIRE(def.grip_rot_deg_y == Approx(-6.2f));
    REQUIRE(def.grip_rot_deg_z == Approx(-67.4f));
    REQUIRE(def.grip_scale == Approx(1.25f));
}

TEST_CASE("ItemDef loads world_mesh field for pickup rendering",
          "[engine][config][item][world_mesh]")
{
    clearDir("itemworldmesh");
    const std::string body = R"({
        "name": "Bark scrap",
        "category": "material",
        "rarity": "common",
        "stackable": true,
        "max_stack": 99,
        "world_mesh": "assets/world/materials/bark_scrap/bark_scrap.glb"
    })";
    writeFile("itemworldmesh", "bark_scrap.json", body);

    engine::ecs::ItemRegistry reg;
    REQUIRE(engine::ecs::loadItemRegistry(reg, (kTmpRoot / "itemworldmesh").generic_string()) == 1);
    const auto& def = reg.defs.begin()->second;
    REQUIRE(def.world_mesh == "assets/world/materials/bark_scrap/bark_scrap.glb");
    REQUIRE(def.category == engine::ecs::ItemCategory::Material);
    REQUIRE(def.stackable);
    REQUIRE(def.max_stack == 99);
}

TEST_CASE("ItemCategory parses Incantation and Invocation strings",
          "[engine][config][item][category]")
{
    clearDir("itemcat");
    writeFile("itemcat", "incant.json",
              R"({"name":"Spell","category":"incantation","rarity":"common"})");
    writeFile("itemcat", "invoke.json",
              R"({"name":"Pray","category":"invocation","rarity":"common"})");
    writeFile("itemcat", "weapon.json",
              R"({"name":"Blade","category":"weapon","rarity":"common"})");
    writeFile("itemcat", "armor.json", R"({"name":"Vest","category":"armor","rarity":"common"})");
    writeFile("itemcat", "consum.json",
              R"({"name":"Potion","category":"consumable","rarity":"common"})");
    writeFile("itemcat", "material.json",
              R"({"name":"Bone","category":"material","rarity":"common"})");
    writeFile("itemcat", "key.json", R"({"name":"Key","category":"key_item","rarity":"common"})");
    writeFile("itemcat", "money.json", R"({"name":"Gold","category":"money","rarity":"common"})");
    writeFile("itemcat", "acc.json", R"({"name":"Ring","category":"accessory","rarity":"common"})");

    engine::ecs::ItemRegistry reg;
    REQUIRE(engine::ecs::loadItemRegistry(reg, (kTmpRoot / "itemcat").generic_string()) == 9);

    auto byName = [&](const char* name) -> const engine::ecs::ItemDef*
    {
        for (const auto& [_, d] : reg.defs)
            if (d.name == name)
                return &d;
        return nullptr;
    };

    REQUIRE(byName("Spell")->category == engine::ecs::ItemCategory::Incantation);
    REQUIRE(byName("Pray")->category == engine::ecs::ItemCategory::Invocation);
    REQUIRE(byName("Blade")->category == engine::ecs::ItemCategory::Weapon);
    REQUIRE(byName("Vest")->category == engine::ecs::ItemCategory::Armor);
    REQUIRE(byName("Potion")->category == engine::ecs::ItemCategory::Consumable);
    REQUIRE(byName("Bone")->category == engine::ecs::ItemCategory::Material);
    REQUIRE(byName("Key")->category == engine::ecs::ItemCategory::KeyItem);
    REQUIRE(byName("Gold")->category == engine::ecs::ItemCategory::Money);
    REQUIRE(byName("Ring")->category == engine::ecs::ItemCategory::Accessory);
}

TEST_CASE("RecipeDef loads sangue_cost and substrate fields", "[engine][config][recipe]")
{
    clearDir("recipefull");
    const std::string body = R"({
        "name": "Distill thing",
        "inputs": [
            { "item": "config/items/materials/foo.json", "quantity": 3 }
        ],
        "output": "config/items/materials/bar.json",
        "output_quantity": 1,
        "sangue_cost": 5,
        "substrate": "hell"
    })";
    writeFile("recipefull", "distill.json", body);

    engine::ecs::RecipeRegistry reg;
    const int n = engine::ecs::loadRecipeRegistry(reg, (kTmpRoot / "recipefull").generic_string());
    REQUIRE(n == 1);
    REQUIRE(reg.recipes.size() == 1);

    const auto& r = reg.recipes[0];
    REQUIRE(r.name == "Distill thing");
    REQUIRE(r.inputs.size() == 1);
    REQUIRE(r.inputs[0].config_path == "config/items/materials/foo.json");
    REQUIRE(r.inputs[0].quantity == 3);
    REQUIRE(r.output_item == "config/items/materials/bar.json");
    REQUIRE(r.output_quantity == 1);
    REQUIRE(r.sangue_cost == 5);
    REQUIRE(r.substrate == "hell");
}

TEST_CASE("RecipeDef loads unlocks_recipe + unlock_after for mastery chain",
          "[engine][config][recipe][unlock]")
{
    clearDir("recipeunlock");
    const std::string body = R"({
        "name": "Poultice",
        "inputs": [
            { "item": "config/items/materials/bark_scrap.json", "quantity": 2 }
        ],
        "output": "config/items/consumables/poultice.json",
        "output_quantity": 1,
        "unlocks_recipe": "config/recipes/craft_salve.json",
        "unlock_after": 10
    })";
    writeFile("recipeunlock", "craft_poultice.json", body);

    engine::ecs::RecipeRegistry reg;
    REQUIRE(engine::ecs::loadRecipeRegistry(
                reg, (kTmpRoot / "recipeunlock").generic_string()) == 1);
    const auto& r = reg.recipes[0];
    REQUIRE(r.unlocks_recipe == "config/recipes/craft_salve.json");
    REQUIRE(r.unlock_after == 10);
}

TEST_CASE("RecipeDef sangue_cost defaults to 0 and substrate defaults to empty",
          "[engine][config][recipe]")
{
    clearDir("recipemin");
    const std::string body = R"({
        "name": "Plain recipe",
        "inputs": [],
        "output": "config/items/materials/bar.json"
    })";
    writeFile("recipemin", "plain.json", body);

    engine::ecs::RecipeRegistry reg;
    REQUIRE(engine::ecs::loadRecipeRegistry(reg, (kTmpRoot / "recipemin").generic_string()) == 1);
    REQUIRE(reg.recipes[0].sangue_cost == 0);
    REQUIRE(reg.recipes[0].substrate.empty());
    REQUIRE(reg.recipes[0].output_quantity == 1);
    REQUIRE(reg.recipes[0].unlocks_recipe.empty());
    REQUIRE(reg.recipes[0].unlock_after == 0);
}
