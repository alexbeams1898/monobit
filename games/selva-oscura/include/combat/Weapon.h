#pragma once

#include <nlohmann/json.hpp>
#include <string>
#include <unordered_map>

namespace selva::combat
{
struct WeaponClass;
struct WeaponClassRegistry;

// Per-grip stats. Each grip mode of a weapon has its own damage curve:
// two-handing usually swings harder for more stamina. Multipliers (light_mul,
// heavy_mul) compose with base_damage so a designer can rebalance the
// "weight" between light and heavy without touching base_damage.
//
// Stamina + poise live here as zero-default fields today: the schema slot
// is reserved so combat math code added later doesn't trigger a JSON
// migration on every existing weapon file.
struct WeaponGripStats
{
    float base_damage = 10.0f;
    float light_mul = 1.0f;
    float heavy_mul = 1.6f;
    float stamina_light = 0.0f;
    float stamina_heavy = 0.0f;
    float poise_damage = 0.0f;
};
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(WeaponGripStats, base_damage, light_mul, heavy_mul,
                                                stamina_light, stamina_heavy, poise_damage);

// Both grip-mode stat blocks. Every weapon defines both: weapons that
// "shouldn't" be wielded in one hand still get an entry — designer fills
// in heavier stamina / weaker damage to express the awkward grip in
// numbers, instead of branching on null in code.
struct WeaponStats
{
    WeaponGripStats one_handed = {};
    WeaponGripStats two_handed = {};
};
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(WeaponStats, one_handed, two_handed);

// An individual weapon. References its class (sword, dagger, spear, ...)
// by id; the class pointer is resolved at registry-load time. The mesh
// path points at a glTF file used to render the weapon as an attachment
// to the wielder's hand bone (rendering not wired yet — schema reserves
// the field).
struct Weapon
{
    std::string id = {};
    std::string name = {};
    std::string class_id = {};
    std::string mesh = {};
    WeaponStats stats = {};

    // Resolved at load time, not serialized. Pointer into the
    // WeaponClassRegistry passed to WeaponRegistry::loadDirectory(); valid
    // for the lifetime of that registry.
    const WeaponClass* cls = nullptr;
};

// JSON IO is hand-written rather than macro-generated because we want the
// JSON key "class" (a C++ keyword) to map onto class_id, and we don't
// want `cls` (the resolved pointer) to appear in serialized output.
void to_json(nlohmann::json& j, const Weapon& w);
void from_json(const nlohmann::json& j, Weapon& w);

// Registry of all Weapons, keyed by `id`. Populated at startup by scanning
// config/weapons/*.json. Each weapon's `cls` pointer is resolved against
// the supplied WeaponClassRegistry; weapons with an unknown class_id are
// loaded with cls == nullptr and a warning is logged (still listed, so
// missing-class errors are visible rather than silent drops).
struct WeaponRegistry
{
    std::unordered_map<std::string, Weapon> by_id;

    int loadDirectory(const std::string& dir, const WeaponClassRegistry& classes);

    const Weapon* get(const std::string& id) const;
};

} // namespace selva::combat
