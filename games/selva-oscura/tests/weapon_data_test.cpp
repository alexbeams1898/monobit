#include "combat/PlayerEquipment.h"
#include "combat/Weapon.h"
#include "combat/WeaponClass.h"

#include <catch2/catch_test_macros.hpp>

// Working directory for these tests is build/bin/ (set by CMake on the
// test target), which is where the per-build sync target deposits a
// copy of games/selva-oscura/config/. So relative paths like
// "config/weapon_classes" resolve to the synced copies.

using selva::combat::Grip;
using selva::combat::loadEquipment;
using selva::combat::WeaponClassRegistry;
using selva::combat::WeaponRegistry;

// Catch2 TEST_CASE macro expansion produces a generated function whose
// cognitive complexity exceeds the project threshold; the actual test
// body is straightforward.
// NOLINTNEXTLINE(readability-function-cognitive-complexity)
TEST_CASE("WeaponClassRegistry loads classes from JSON dir", "[combat][weaponclass][load]")
{
    WeaponClassRegistry classes;
    const int n = classes.loadDirectory("config/weapon_classes");
    REQUIRE(n >= 1);

    const auto* sword = classes.get("sword");
    REQUIRE(sword != nullptr);
    REQUIRE(sword->id == "sword");
    // Each slot is a list of techniques; each technique is a chain of
    // attacks. Sword carries one technique per slot today.
    REQUIRE_FALSE(sword->one_handed.light.empty());
    REQUIRE_FALSE(sword->one_handed.light[0].attacks.empty());
    REQUIRE_FALSE(sword->one_handed.light[0].attacks[0].clip.empty());
    REQUIRE_FALSE(sword->two_handed.heavy.empty());
    REQUIRE_FALSE(sword->two_handed.heavy[0].attacks.empty());
    REQUIRE_FALSE(sword->two_handed.heavy[0].attacks[0].clip.empty());
    // sword.json uses the auto-detect default for cancel_open_seconds
    // (negative sentinel = scan at clip-load). Per-attack overrides
    // would be positive clip-local seconds; absent here.
    for (const auto& a : sword->one_handed.light[0].attacks)
    {
        REQUIRE(a.cancel_open_seconds < 0.0f);
    }
    // Mixamo bone names should round-trip through JSON unchanged.
    REQUIRE(sword->attach.bone_right == "mixamorig:RightHand");
    REQUIRE(sword->attach.bone_left == "mixamorig:LeftHand");
}

TEST_CASE("Unarmed exposes both techniques with expected_button per attack",
          "[combat][weaponclass][unarmed]")
{
    WeaponClassRegistry classes;
    classes.loadDirectory("config/weapon_classes");
    const auto* fists = classes.get("unarmed");
    REQUIRE(fists != nullptr);
    REQUIRE(fists->one_handed.light.size() == 2);
    const auto& tech_a = fists->one_handed.light[0];
    const auto& tech_b = fists->one_handed.light[1];
    REQUIRE(tech_a.id == "jab_hook_combo");
    REQUIRE(tech_b.id == "jab_jab_hook");
    REQUIRE(tech_a.attacks.size() == 3);
    REQUIRE(tech_b.attacks.size() == 3);
    // Chain A (LMB-RMB-LMB) plays jab -> hook -> combo.
    REQUIRE(tech_a.attacks[0].clip == "jab");
    REQUIRE(tech_a.attacks[1].clip == "hook");
    REQUIRE(tech_a.attacks[2].clip == "combo");
    REQUIRE(tech_a.attacks[0].expected_button == "LMB");
    REQUIRE(tech_a.attacks[1].expected_button == "RMB");
    REQUIRE(tech_a.attacks[2].expected_button == "LMB");
    // Chain B (LMB-LMB-RMB) plays jab -> jab -> hook.
    REQUIRE(tech_b.attacks[0].clip == "jab");
    REQUIRE(tech_b.attacks[1].clip == "jab");
    REQUIRE(tech_b.attacks[2].clip == "hook");
    REQUIRE(tech_b.attacks[0].expected_button == "LMB");
    REQUIRE(tech_b.attacks[1].expected_button == "LMB");
    REQUIRE(tech_b.attacks[2].expected_button == "RMB");
}

TEST_CASE("WeaponRegistry resolves class pointers", "[combat][weapon][load]")
{
    WeaponClassRegistry classes;
    classes.loadDirectory("config/weapon_classes");

    WeaponRegistry weapons;
    const int n = weapons.loadDirectory("config/weapons", classes);
    REQUIRE(n >= 1);

    const auto* longsword = weapons.get("longsword");
    REQUIRE(longsword != nullptr);
    REQUIRE(longsword->id == "longsword");
    REQUIRE(longsword->class_id == "sword");
    // Class pointer should resolve since both files are present.
    REQUIRE(longsword->cls != nullptr);
    REQUIRE(longsword->cls->id == "sword");
    // Stats sanity: two-handed should hit harder than one-handed for swords.
    REQUIRE(longsword->stats.two_handed.base_damage > longsword->stats.one_handed.base_damage);
}

TEST_CASE("loadEquipment reads loadout grip", "[combat][equipment][load]")
{
    WeaponClassRegistry classes;
    classes.loadDirectory("config/weapon_classes");
    WeaponRegistry weapons;
    weapons.loadDirectory("config/weapons", classes);

    const auto eq = loadEquipment("config/loadout.json", weapons);
    // The current loadout ships unarmed (empty strings in both hands)
    // while sword/shield work is parked. Empty-string ids resolve to
    // nullptr; the runtime treats two-null hands as the synthesized
    // fists weapon. Grip is the only field worth asserting here until
    // a non-empty placeholder loadout returns.
    REQUIRE(eq.grip == Grip::OneHanded);
}

TEST_CASE("Unknown weapon id in loadout leaves slot null without crashing",
          "[combat][equipment][robustness]")
{
    WeaponClassRegistry classes;
    classes.loadDirectory("config/weapon_classes");
    WeaponRegistry weapons;
    weapons.loadDirectory("config/weapons", classes);

    // Look up an id that doesn't exist — registry should hand back null,
    // not throw and not return a stale pointer.
    REQUIRE(weapons.get("definitely_not_a_real_weapon") == nullptr);
}
