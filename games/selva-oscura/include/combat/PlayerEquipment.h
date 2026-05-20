#pragma once

#include <nlohmann/json.hpp>

#include <string>

namespace selva::combat
{
struct Weapon;
struct WeaponRegistry;

// Grip mode is a *runtime* choice, not a property of the weapon. Any
// weapon can be held one-handed or two-handed; the WeaponClass defines a
// distinct animset and the Weapon defines distinct stats for each. The
// player toggles between them in-game.
//
// Two-handing forces the off-hand to drop / stash — implemented in code
// when grip is set to TwoHanded, not declared in JSON.
enum class Grip
{
    OneHanded,
    TwoHanded,
};

// Either hand. Used for "which hand fired this attack" plumbing.
enum class HandSide
{
    Right,
    Left,
};

// Live equipment state. Pointers into a WeaponRegistry; the registry
// outlives the equipment. nullptr in either slot means that hand is
// empty (later: unarmed punch animset). At the moment the player has no
// inventory — equipment is set once at spawn from loadout.json and held
// for the run.
struct PlayerEquipment
{
    const Weapon* right = nullptr;
    const Weapon* left = nullptr;
    Grip grip = Grip::OneHanded;
};

// loadout.json schema (in config/loadout.json):
//   { "right_hand": "longsword", "left_hand": "iron_buckler", "grip": "one_handed" }
// Either hand may be null. `grip` is "one_handed" or "two_handed".
struct Loadout
{
    std::string right_hand;          // weapon id, or empty for unarmed
    std::string left_hand;           // weapon id, or empty for unarmed
    std::string grip = "one_handed"; // "one_handed" | "two_handed"
};
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(Loadout, right_hand, left_hand, grip);

// Read loadout.json and resolve weapon ids against the registry.
// Returns a populated PlayerEquipment; missing/unknown ids leave the
// corresponding slot null (warning logged). The "grip" field falls back
// to OneHanded if unrecognized.
PlayerEquipment loadEquipment(const std::string& loadout_path, const WeaponRegistry& weapons);

} // namespace selva::combat
