#include "ecs/Components.h"
#include "ecs/EntityManager.h"
#include "ecs/GameComponents.h"
#include "ecs/GameConfig.h"
#include "ops/InventoryOps.h"
#include "systems/EquipmentSystem.h"
#include "test_helpers.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

static void registerShiv(EntityManager& em)
{
    auto& reg = em.registry().ctx().get<ItemRegistry>();
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
    reg.defs[shiv.config_path] = shiv;
}

static void registerPistol(EntityManager& em)
{
    auto& reg = em.registry().ctx().get<ItemRegistry>();
    ItemDef pistol;
    pistol.config_path = "config/items/weapons/colt_45.json";
    pistol.name = "Colt .45";
    pistol.category = ItemCategory::Weapon;
    pistol.base_damage = 16.0f;
    pistol.weight = 1.0f;
    pistol.ranged = true;
    pistol.visual_weapon = "colt_45";
    pistol.attack_anim = "shoot";
    pistol.shoot_frames = {0, 1, 2, 3, 10, 3, 2, 1};
    reg.defs[pistol.config_path] = pistol;
}

static void registerAk(EntityManager& em)
{
    auto& reg = em.registry().ctx().get<ItemRegistry>();
    ItemDef ak;
    ak.config_path = "config/items/weapons/ak_47.json";
    ak.name = "AK-47";
    ak.category = ItemCategory::Weapon;
    ak.base_damage = 12.0f;
    ak.weight = 1.3f;
    ak.ranged = true;
    ak.visual_weapon = "ak_47";
    ak.two_handed = true;
    ak.grip_x = 12.0f;
    ak.grip_y = 23.0f;
    ak.fore_grip_x = 20.0f;
    ak.fore_grip_y = 10.0f;
    reg.defs[ak.config_path] = ak;
}

static void registerShield(EntityManager& em)
{
    auto& reg = em.registry().ctx().get<ItemRegistry>();
    ItemDef shield;
    shield.config_path = "config/items/armor/wooden_shield.json";
    shield.name = "Wooden Shield";
    shield.category = ItemCategory::Armor;
    shield.max_guard = 60.0f;
    reg.defs[shield.config_path] = shield;
}

// Helper: add a weapon item to entity inventory, equip it to right hand, and
// set synced_right_hand so EquipmentSystem detects the change.
static void equipWeaponToRightHand(EntityManager& em, entt::entity entity,
                                   const std::string& config_path)
{
    auto& inv = em.registry().get<Inventory>(entity);
    inv.items.push_back({config_path});
    const int idx = static_cast<int>(inv.items.size()) - 1;
    auto& equip = em.registry().get<Equipment>(entity);
    equip.right_hand = idx;
}

// Helper: add an item to entity inventory and equip it to left hand.
static void equipItemToLeftHand(EntityManager& em, entt::entity entity,
                                const std::string& config_path)
{
    auto& inv = em.registry().get<Inventory>(entity);
    inv.items.push_back({config_path});
    const int idx = static_cast<int>(inv.items.size()) - 1;
    auto& equip = em.registry().get<Equipment>(entity);
    equip.left_hand = idx;
}

// ---------------------------------------------------------------------------
// EquipmentSystem tests
// ---------------------------------------------------------------------------

TEST_CASE("EquipmentSystem: empty equipment gives fist defaults", "[equipment]")
{
    EntityManager em;
    emplaceGameConfigs(em);

    auto entity = em.create();
    em.registry().emplace<Equipment>(entity);
    em.registry().emplace<Inventory>(entity);
    em.registry().emplace<Weapon>(entity);

    // synced_right_hand starts as "__unsynced__" which differs from the
    // equippedPath ("" since right_hand is -1), so the first update triggers sync.

    EquipmentSystem::update(em);

    const auto& w = em.registry().get<Weapon>(entity);
    const auto& f = em.registry().ctx().get<FormulaConfig>();
    REQUIRE(w.name == "Unarmed");
    REQUIRE(w.base_damage == Catch::Approx(f.fist.base_damage));
    REQUIRE(w.str_scaling == Catch::Approx(f.fist.str_scaling));
    REQUIRE(w.dex_scaling == Catch::Approx(f.fist.dex_scaling));
    REQUIRE(w.weight == Catch::Approx(f.fist.weight));
}

TEST_CASE("EquipmentSystem: equip weapon updates Weapon component", "[equipment]")
{
    EntityManager em;
    emplaceGameConfigs(em);
    registerShiv(em);

    auto entity = em.create();
    em.registry().emplace<Equipment>(entity);
    em.registry().emplace<Inventory>(entity);
    em.registry().emplace<Weapon>(entity);

    equipWeaponToRightHand(em, entity, "config/items/weapons/shiv.json");

    EquipmentSystem::update(em);

    const auto& w = em.registry().get<Weapon>(entity);
    REQUIRE(w.name == "Shiv");
    REQUIRE(w.base_damage == Catch::Approx(12.0f));
    REQUIRE(w.str_scaling == Catch::Approx(0.5f));
    REQUIRE(w.dex_scaling == Catch::Approx(1.0f));
    REQUIRE(w.str_requirement == 3);
    REQUIRE(w.dex_requirement == 5);
    REQUIRE(w.weight == Catch::Approx(1.0f));
}

TEST_CASE("EquipmentSystem: unequip weapon reverts to fist", "[equipment]")
{
    EntityManager em;
    emplaceGameConfigs(em);
    registerShiv(em);

    auto entity = em.create();
    em.registry().emplace<Equipment>(entity);
    em.registry().emplace<Inventory>(entity);
    em.registry().emplace<Weapon>(entity);

    // Equip shiv.
    equipWeaponToRightHand(em, entity, "config/items/weapons/shiv.json");
    EquipmentSystem::update(em);
    REQUIRE(em.registry().get<Weapon>(entity).name == "Shiv");

    // Unequip.
    auto& equip = em.registry().get<Equipment>(entity);
    equip.right_hand = -1;
    EquipmentSystem::update(em);

    const auto& w = em.registry().get<Weapon>(entity);
    REQUIRE(w.name == "Unarmed");
    REQUIRE(w.base_damage ==
            Catch::Approx(em.registry().ctx().get<FormulaConfig>().fist.base_damage));
}

TEST_CASE("EquipmentSystem: no change when equipment unchanged", "[equipment]")
{
    EntityManager em;
    emplaceGameConfigs(em);
    registerShiv(em);

    auto entity = em.create();
    em.registry().emplace<Equipment>(entity);
    em.registry().emplace<Inventory>(entity);
    em.registry().emplace<Weapon>(entity);

    equipWeaponToRightHand(em, entity, "config/items/weapons/shiv.json");
    EquipmentSystem::update(em);

    // Modify weapon manually to detect if EquipmentSystem overwrites it.
    em.registry().get<Weapon>(entity).base_damage = 999.0f;

    EquipmentSystem::update(em);

    // Should NOT have overwritten because synced_right_hand matches.
    REQUIRE(em.registry().get<Weapon>(entity).base_damage == Catch::Approx(999.0f));
}

TEST_CASE("EquipmentSystem: equip shield emplaces Shield component", "[equipment]")
{
    EntityManager em;
    emplaceGameConfigs(em);
    registerShield(em);

    auto entity = em.create();
    em.registry().emplace<Equipment>(entity);
    em.registry().emplace<Inventory>(entity);
    em.registry().emplace<Weapon>(entity);

    equipItemToLeftHand(em, entity, "config/items/armor/wooden_shield.json");
    EquipmentSystem::update(em);

    REQUIRE(em.registry().all_of<Shield>(entity));
    REQUIRE(em.registry().get<Shield>(entity).max_guard == Catch::Approx(60.0f));
}

TEST_CASE("EquipmentSystem: unequip shield removes Shield component", "[equipment]")
{
    EntityManager em;
    emplaceGameConfigs(em);
    registerShield(em);

    auto entity = em.create();
    em.registry().emplace<Equipment>(entity);
    em.registry().emplace<Inventory>(entity);
    em.registry().emplace<Weapon>(entity);

    equipItemToLeftHand(em, entity, "config/items/armor/wooden_shield.json");
    EquipmentSystem::update(em);
    REQUIRE(em.registry().all_of<Shield>(entity));

    auto& equip = em.registry().get<Equipment>(entity);
    equip.left_hand = -1;
    EquipmentSystem::update(em);
    REQUIRE_FALSE(em.registry().all_of<Shield>(entity));
}

TEST_CASE("EquipmentSystem: entity without Equipment is unaffected", "[equipment]")
{
    EntityManager em;
    emplaceGameConfigs(em);

    // Enemy with direct Weapon, no Equipment.
    auto enemy = em.create();
    Weapon w;
    w.name = "Bone Club";
    w.base_damage = 15.0f;
    em.registry().emplace<Weapon>(enemy, w);

    EquipmentSystem::update(em);

    // Weapon should be unchanged.
    REQUIRE(em.registry().get<Weapon>(enemy).name == "Bone Club");
    REQUIRE(em.registry().get<Weapon>(enemy).base_damage == Catch::Approx(15.0f));
}

TEST_CASE("EquipmentSystem: Body natural weapon used when unarmed", "[equipment]")
{
    EntityManager em;
    emplaceGameConfigs(em);

    auto entity = em.create();
    Body body;
    body.base_defense = 2;
    body.unarmed_damage = 3.0f;
    body.unarmed_str_scaling = 0.25f;
    body.unarmed_dex_scaling = 0.0f;
    em.registry().emplace<Body>(entity, body);
    em.registry().emplace<Equipment>(entity);
    em.registry().emplace<Inventory>(entity);
    em.registry().emplace<Weapon>(entity);

    // synced_right_hand defaults to "__unsynced__" which triggers sync on first update.

    EquipmentSystem::update(em);

    const auto& w = em.registry().get<Weapon>(entity);
    REQUIRE(w.name == "Unarmed");
    REQUIRE(w.base_damage == Catch::Approx(3.0f));
    REQUIRE(w.str_scaling == Catch::Approx(0.25f));
    REQUIRE(w.dex_scaling == Catch::Approx(0.0f));
}

TEST_CASE("EquipmentSystem: no Body falls back to FormulaConfig fist", "[equipment]")
{
    EntityManager em;
    emplaceGameConfigs(em);

    auto entity = em.create();
    em.registry().emplace<Equipment>(entity);
    em.registry().emplace<Inventory>(entity);
    em.registry().emplace<Weapon>(entity);

    // synced_right_hand defaults to "__unsynced__" which triggers sync on first update.

    EquipmentSystem::update(em);

    const auto& w = em.registry().get<Weapon>(entity);
    const auto& f = em.registry().ctx().get<FormulaConfig>();
    REQUIRE(w.name == "Unarmed");
    REQUIRE(w.base_damage == Catch::Approx(f.fist.base_damage));
    REQUIRE(w.str_scaling == Catch::Approx(f.fist.str_scaling));
    REQUIRE(w.dex_scaling == Catch::Approx(f.fist.dex_scaling));
}

// ---------------------------------------------------------------------------
// Weapon visual field tests
// ---------------------------------------------------------------------------

TEST_CASE("EquipmentSystem: equip copies visual weapon fields from ItemDef", "[equipment]")
{
    EntityManager em;
    emplaceGameConfigs(em);
    registerPistol(em);

    auto entity = em.create();
    em.registry().emplace<Equipment>(entity);
    em.registry().emplace<Inventory>(entity);
    em.registry().emplace<Weapon>(entity);

    equipWeaponToRightHand(em, entity, "config/items/weapons/colt_45.json");
    EquipmentSystem::update(em);

    const auto& w = em.registry().get<Weapon>(entity);
    REQUIRE(w.visual_weapon == "colt_45");
    REQUIRE(w.attack_anim == "shoot");
    REQUIRE(w.shoot_frames.size() == 8);
    REQUIRE(w.shoot_frames[4] == 10);
}

TEST_CASE("EquipmentSystem: fist clears visual weapon fields", "[equipment]")
{
    EntityManager em;
    emplaceGameConfigs(em);
    registerPistol(em);

    auto entity = em.create();
    em.registry().emplace<Equipment>(entity);
    em.registry().emplace<Inventory>(entity);
    em.registry().emplace<Weapon>(entity);

    // Equip pistol first.
    equipWeaponToRightHand(em, entity, "config/items/weapons/colt_45.json");
    EquipmentSystem::update(em);
    REQUIRE(em.registry().get<Weapon>(entity).visual_weapon == "colt_45");

    // Unequip -> fist.
    auto& equip = em.registry().get<Equipment>(entity);
    equip.right_hand = -1;
    EquipmentSystem::update(em);

    const auto& w = em.registry().get<Weapon>(entity);
    REQUIRE(w.visual_weapon.empty());
    REQUIRE(w.attack_anim.empty());
    REQUIRE(w.shoot_frames.empty());
}

TEST_CASE("EquipmentSystem: shiv has no visual weapon fields", "[equipment]")
{
    EntityManager em;
    emplaceGameConfigs(em);
    registerShiv(em);

    auto entity = em.create();
    em.registry().emplace<Equipment>(entity);
    em.registry().emplace<Inventory>(entity);
    em.registry().emplace<Weapon>(entity);

    equipWeaponToRightHand(em, entity, "config/items/weapons/shiv.json");
    EquipmentSystem::update(em);

    const auto& w = em.registry().get<Weapon>(entity);
    REQUIRE(w.visual_weapon.empty());
    REQUIRE(w.attack_anim.empty());
    REQUIRE(w.shoot_frames.empty());
}

TEST_CASE("EquipmentSystem: two-handed weapon flows flag and starts inactive",
          "[equipment][two_handed]")
{
    EntityManager em;
    emplaceGameConfigs(em);
    registerAk(em);

    auto entity = em.create();
    em.registry().emplace<Equipment>(entity);
    em.registry().emplace<Inventory>(entity);
    em.registry().emplace<Weapon>(entity);

    equipWeaponToRightHand(em, entity, "config/items/weapons/ak_47.json");
    EquipmentSystem::update(em);

    const auto& w = em.registry().get<Weapon>(entity);
    REQUIRE(w.two_handed);
    // Fresh equip always starts one-handed; player presses Left Alt to toggle.
    REQUIRE_FALSE(w.two_handed_active);
    REQUIRE(w.fore_grip_x == Catch::Approx(20.0f));
    REQUIRE(w.fore_grip_y == Catch::Approx(10.0f));
}

TEST_CASE("EquipmentSystem: pistol is not two-handed-capable",
          "[equipment][two_handed]")
{
    EntityManager em;
    emplaceGameConfigs(em);
    registerPistol(em);

    auto entity = em.create();
    em.registry().emplace<Equipment>(entity);
    em.registry().emplace<Inventory>(entity);
    em.registry().emplace<Weapon>(entity);

    equipWeaponToRightHand(em, entity, "config/items/weapons/colt_45.json");
    EquipmentSystem::update(em);

    const auto& w = em.registry().get<Weapon>(entity);
    REQUIRE_FALSE(w.two_handed);
    REQUIRE_FALSE(w.two_handed_active);
}

TEST_CASE("EquipmentSystem: swapping to fists clears two-handed state",
          "[equipment][two_handed]")
{
    EntityManager em;
    emplaceGameConfigs(em);
    registerAk(em);

    auto entity = em.create();
    em.registry().emplace<Equipment>(entity);
    em.registry().emplace<Inventory>(entity);
    em.registry().emplace<Weapon>(entity);

    // Equip AK and manually set active, simulating a player toggle.
    equipWeaponToRightHand(em, entity, "config/items/weapons/ak_47.json");
    EquipmentSystem::update(em);
    em.registry().get<Weapon>(entity).two_handed_active = true;

    // Unequip back to fists.
    auto& equip = em.registry().get<Equipment>(entity);
    equip.right_hand = -1;
    EquipmentSystem::update(em);

    const auto& w = em.registry().get<Weapon>(entity);
    REQUIRE_FALSE(w.two_handed);
    REQUIRE_FALSE(w.two_handed_active);
}
