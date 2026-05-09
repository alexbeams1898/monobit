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

TEST_CASE("WeaponClassRegistry loads classes from JSON dir", "[combat][weaponclass][load]")
{
    WeaponClassRegistry classes;
    const int n = classes.loadDirectory("config/weapon_classes");
    REQUIRE(n >= 1);

    const auto* sword = classes.get("sword");
    REQUIRE(sword != nullptr);
    REQUIRE(sword->id == "sword");
    // Each slot is now a chain (vector). At minimum one entry per slot
    // for any weapon that supports that attack kind.
    REQUIRE_FALSE(sword->one_handed.light.empty());
    REQUIRE_FALSE(sword->one_handed.light[0].clip.empty());
    REQUIRE_FALSE(sword->two_handed.heavy.empty());
    REQUIRE_FALSE(sword->two_handed.heavy[0].clip.empty());
    // sword.json uses the auto-detect default for cancel_open_seconds
    // (negative sentinel = scan at clip-load). Per-attack overrides
    // would be positive clip-local seconds; absent here.
    for (const auto& a : sword->one_handed.light)
    {
        REQUIRE(a.cancel_open_seconds < 0.0f);
    }
    // Mixamo bone names should round-trip through JSON unchanged.
    REQUIRE(sword->attach.bone_right == "mixamorig:RightHand");
    REQUIRE(sword->attach.bone_left == "mixamorig:LeftHand");
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

TEST_CASE("loadEquipment reads loadout and resolves both hands", "[combat][equipment][load]")
{
    WeaponClassRegistry classes;
    classes.loadDirectory("config/weapon_classes");
    WeaponRegistry weapons;
    weapons.loadDirectory("config/weapons", classes);

    const auto eq = loadEquipment("config/loadout.json", weapons);
    // Either hand may be empty in principle; the placeholder loadout
    // ships with both filled, so we assert that for now.
    REQUIRE(eq.right != nullptr);
    REQUIRE(eq.left != nullptr);
    REQUIRE(eq.right->id == "longsword");
    REQUIRE(eq.left->id == "iron_buckler");
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
