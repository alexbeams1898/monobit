#include "ecs/Components.h"
#include "ecs/GameComponents.h"
#include "ecs/GameConfig.h"
#include "systems/EquipmentSystem.h"
#include "test_helpers.h"

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

using Catch::Matchers::WithinAbs;

TEST_CASE("EquipmentSystem: ArmorStats aggregates defense from armor slots", "[armor]")
{
    EntityManager em;
    emplaceGameConfigs(em);

    // Register item defs for armor pieces.
    auto& items = em.registry().ctx().get<ItemRegistry>();

    ItemDef helm;
    helm.config_path = "helm";
    helm.category = ItemCategory::Armor;
    helm.armor_slot = ArmorSlot::Head;
    helm.defense_bonus = 3.0f;
    helm.poise_bonus = 2.0f;
    helm.weight = 1.0f;
    items.defs["helm"] = helm;

    ItemDef chest;
    chest.config_path = "chest";
    chest.category = ItemCategory::Armor;
    chest.armor_slot = ArmorSlot::Chest;
    chest.defense_bonus = 8.0f;
    chest.poise_bonus = 5.0f;
    chest.weight = 4.0f;
    items.defs["chest"] = chest;

    auto player = em.create();
    em.registry().emplace<PlayerActions>(player);
    em.registry().emplace<Inventory>(player);
    em.registry().emplace<Stats>(player, Stats{5, 5, 5, 5});

    Equipment equip;
    equip.head.config_path = "helm";
    equip.chest.config_path = "chest";
    em.registry().emplace<Equipment>(player, equip);

    EquipmentSystem::update(em);

    const auto& armor = em.registry().get<ArmorStats>(player);
    REQUIRE_THAT(armor.total_defense, WithinAbs(11.0f, 0.01f));    // 3 + 8
    REQUIRE_THAT(armor.total_poise_bonus, WithinAbs(7.0f, 0.01f)); // 2 + 5
    REQUIRE_THAT(armor.total_weight, WithinAbs(5.0f, 0.01f));      // 1 + 4
}

TEST_CASE("EquipmentSystem: equip load tier computed from weight and stats", "[armor]")
{
    EntityManager em;
    emplaceGameConfigs(em);
    auto& f = em.registry().ctx().get<FormulaConfig>();
    f.equip_load.base_capacity = 40.0f;
    f.equip_load.str_scale = 3.0f;
    f.equip_load.end_scale = 1.5f;
    f.equip_load.light_threshold = 0.3f;
    f.equip_load.medium_threshold = 0.7f;
    f.equip_load.heavy_threshold = 1.0f;

    auto& items = em.registry().ctx().get<ItemRegistry>();
    ItemDef heavyArmor;
    heavyArmor.config_path = "heavy";
    heavyArmor.category = ItemCategory::Armor;
    heavyArmor.armor_slot = ArmorSlot::Chest;
    heavyArmor.defense_bonus = 10.0f;
    heavyArmor.weight = 35.0f; // very heavy
    items.defs["heavy"] = heavyArmor;

    auto player = em.create();
    em.registry().emplace<PlayerActions>(player);
    em.registry().emplace<Inventory>(player);
    // STR 1, END 1 -> capacity = 40 + 3*1 + 1.5*1 = 44.5
    em.registry().emplace<Stats>(player, Stats{1, 1, 1, 1});

    Equipment equip;
    equip.chest.config_path = "heavy";
    em.registry().emplace<Equipment>(player, equip);

    EquipmentSystem::update(em);

    const auto& armor = em.registry().get<ArmorStats>(player);
    // 35 / 44.5 = 0.787 -> medium threshold (0.3 < 0.787 <= 0.7 is false, 0.7 < 0.787 <= 1.0)
    // = heavy tier (2)
    REQUIRE(armor.load_tier == 2);
}

TEST_CASE("EquipmentSystem: light load with strong stats", "[armor]")
{
    EntityManager em;
    emplaceGameConfigs(em);
    auto& f = em.registry().ctx().get<FormulaConfig>();
    f.equip_load.base_capacity = 40.0f;
    f.equip_load.str_scale = 3.0f;
    f.equip_load.end_scale = 1.5f;

    auto& items = em.registry().ctx().get<ItemRegistry>();
    ItemDef lightArmor;
    lightArmor.config_path = "light";
    lightArmor.category = ItemCategory::Armor;
    lightArmor.armor_slot = ArmorSlot::Chest;
    lightArmor.defense_bonus = 2.0f;
    lightArmor.weight = 5.0f;
    items.defs["light"] = lightArmor;

    auto player = em.create();
    em.registry().emplace<PlayerActions>(player);
    em.registry().emplace<Inventory>(player);
    // STR 10, END 10 -> capacity = 40 + 30 + 15 = 85
    em.registry().emplace<Stats>(player, Stats{10, 10, 10, 10});

    Equipment equip;
    equip.chest.config_path = "light";
    em.registry().emplace<Equipment>(player, equip);

    EquipmentSystem::update(em);

    const auto& armor = em.registry().get<ArmorStats>(player);
    // 5 / 85 = 0.059 -> light tier (0)
    REQUIRE(armor.load_tier == 0);
}

TEST_CASE("EquipmentSystem: poise max set from armor bonus", "[armor]")
{
    EntityManager em;
    emplaceGameConfigs(em);

    auto& items = em.registry().ctx().get<ItemRegistry>();
    ItemDef helm;
    helm.config_path = "helm";
    helm.category = ItemCategory::Armor;
    helm.armor_slot = ArmorSlot::Head;
    helm.defense_bonus = 2.0f;
    helm.poise_bonus = 10.0f;
    helm.weight = 1.0f;
    items.defs["helm"] = helm;

    auto player = em.create();
    em.registry().emplace<PlayerActions>(player);
    em.registry().emplace<Inventory>(player);
    em.registry().emplace<Stats>(player, Stats{0, 0, 0, 0});
    em.registry().emplace<Poise>(player, Poise{0.0f, 0.0f, 0.0f});

    Equipment equip;
    equip.head.config_path = "helm";
    em.registry().emplace<Equipment>(player, equip);

    EquipmentSystem::update(em);

    const auto& poise = em.registry().get<Poise>(player);
    REQUIRE_THAT(poise.max, WithinAbs(10.0f, 0.01f));
}
