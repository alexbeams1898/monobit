#include "ecs/Components.h"
#include "ecs/GameComponents.h"
#include "ecs/GameConfig.h"
#include "ops/InventoryOps.h"
#include "systems/WeaponXPSystem.h"
#include "test_helpers.h"

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

using Catch::Matchers::WithinAbs;

TEST_CASE("WeaponXPSystem: enemy power rating uses config weights", "[weapon_xp]")
{
    EntityManager em;
    emplaceGameConfigs(em);
    auto& f = em.registry().ctx().get<FormulaConfig>();
    f.weapon_xp.power_level_weight = 2.0f;
    f.weapon_xp.power_hp_weight = 0.5f;
    f.weapon_xp.power_dmg_weight = 1.0f;
    f.weapon_xp.power_stat_weight = 0.0f;

    // power = 2*5 + 0.5*100 + 1*10 + 0*0 = 10 + 50 + 10 = 70
    float power = WeaponXPSystem::computeEnemyPower(5, 100, 10.0f, 0, f);
    REQUIRE_THAT(power, WithinAbs(70.0f, 0.01f));
}

TEST_CASE("WeaponXPSystem: grantXP adds XP to player weapon", "[weapon_xp]")
{
    EntityManager em;
    emplaceGameConfigs(em);

    auto player = em.create();
    em.registry().emplace<PlayerActions>(player);
    em.registry().emplace<Weapon>(player);

    WeaponXPSystem::grantXP(em, 50.0f, 1.0f, EquipSlot::RightHand);

    const auto& w = em.registry().get<Weapon>(player);
    REQUIRE_THAT(w.wxp_current, WithinAbs(50.0f, 0.01f));
}

TEST_CASE("WeaponXPSystem: level-up increases weapon stats", "[weapon_xp]")
{
    EntityManager em;
    emplaceGameConfigs(em);

    auto player = em.create();
    em.registry().emplace<PlayerActions>(player);

    Weapon w;
    w.name = "Test Blade";
    w.base_damage = 10.0f;
    w.str_scaling = 0.5f;
    w.dex_scaling = 0.5f;
    w.wxp_current = 200.0f; // enough to level up
    w.wxp_to_next = 50.0f;
    em.registry().emplace<Weapon>(player, w);

    // Put weapon in inventory and equip to right hand.
    auto& inv = em.registry().emplace<Inventory>(player);
    inv.items.push_back({"config/items/weapons/test.json", QualityTier::Common});
    Equipment equip;
    equip.right_hand = 0;
    em.registry().emplace<Equipment>(player, equip);

    // Need ItemRegistry and WeaponTierRegistry in ctx (already emplaced by emplaceGameConfigs).
    WeaponXPSystem::update(em);

    const auto& postW = em.registry().get<Weapon>(player);
    REQUIRE(postW.wxp_level > 1);
    REQUIRE(postW.base_damage > 10.0f);
}

TEST_CASE("WeaponXPSystem: quality affects growth factor", "[weapon_xp]")
{
    EntityManager em;
    emplaceGameConfigs(em);

    // Set up player with a Crude quality weapon.
    auto playerA = em.create();
    em.registry().emplace<PlayerActions>(playerA);
    Weapon wA;
    wA.name = "Crude";
    wA.base_damage = 10.0f;
    wA.str_scaling = 0.5f;
    wA.dex_scaling = 0.5f;
    wA.wxp_current = 200.0f;
    wA.wxp_to_next = 50.0f;
    em.registry().emplace<Weapon>(playerA, wA);
    auto& invA = em.registry().emplace<Inventory>(playerA);
    invA.items.push_back({"test_a", QualityTier::Crude});
    Equipment eqA;
    eqA.right_hand = 0;
    em.registry().emplace<Equipment>(playerA, eqA);

    WeaponXPSystem::update(em);
    const float dmgCrude = em.registry().get<Weapon>(playerA).base_damage;

    // Reset for Masterwork quality.
    auto& postA = em.registry().get<Weapon>(playerA);
    postA.wxp_current = 200.0f;
    postA.wxp_to_next = 50.0f;
    postA.wxp_level = 1;
    postA.base_damage = 10.0f;
    em.registry().get<Inventory>(playerA).items[0].quality = QualityTier::Masterwork;

    WeaponXPSystem::update(em);
    const float dmgMasterwork = em.registry().get<Weapon>(playerA).base_damage;

    // Masterwork has higher quality factor -> higher growth per level.
    REQUIRE(dmgMasterwork > dmgCrude);
}
