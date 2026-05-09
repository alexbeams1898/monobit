#include "combat/PlayerEquipment.h"
#include "combat/Weapon.h"
#include "combat/WeaponClass.h"

#include <cstdio>
#include <filesystem>
#include <fstream>

namespace selva::combat
{

// ---------------------------------------------------------------------------
// JSON glue for types that NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT
// can't handle on its own (glm types, JSON keys that collide with C++
// keywords, fields that should not be serialized).
// ---------------------------------------------------------------------------

void to_json(nlohmann::json& j, const WeaponAttach& a)
{
    j = nlohmann::json{
        {"bone_right", a.bone_right},
        {"bone_left", a.bone_left},
        {"offset_translation",
         {a.offset_translation.x, a.offset_translation.y, a.offset_translation.z}},
        {"offset_rotation_euler_deg",
         {a.offset_rotation_euler_deg.x, a.offset_rotation_euler_deg.y,
          a.offset_rotation_euler_deg.z}},
    };
}

void from_json(const nlohmann::json& j, WeaponAttach& a)
{
    if (j.contains("bone_right"))
        j.at("bone_right").get_to(a.bone_right);
    if (j.contains("bone_left"))
        j.at("bone_left").get_to(a.bone_left);
    if (j.contains("offset_translation"))
    {
        const auto& t = j.at("offset_translation");
        a.offset_translation =
            glm::vec3(t.at(0).get<float>(), t.at(1).get<float>(), t.at(2).get<float>());
    }
    if (j.contains("offset_rotation_euler_deg"))
    {
        const auto& r = j.at("offset_rotation_euler_deg");
        a.offset_rotation_euler_deg =
            glm::vec3(r.at(0).get<float>(), r.at(1).get<float>(), r.at(2).get<float>());
    }
}

void to_json(nlohmann::json& j, const Weapon& w)
{
    j = nlohmann::json{
        {"id", w.id}, {"name", w.name}, {"class", w.class_id}, {"mesh", w.mesh}, {"stats", w.stats},
    };
}

void from_json(const nlohmann::json& j, Weapon& w)
{
    if (j.contains("id"))
        j.at("id").get_to(w.id);
    if (j.contains("name"))
        j.at("name").get_to(w.name);
    // JSON key is "class" (matches Souls/Mixamo terminology and reads naturally
    // in the file); C++ field is class_id since `class` is a keyword.
    if (j.contains("class"))
        j.at("class").get_to(w.class_id);
    if (j.contains("mesh"))
        j.at("mesh").get_to(w.mesh);
    if (j.contains("stats"))
        j.at("stats").get_to(w.stats);
}

// ---------------------------------------------------------------------------
// Generic directory loader. Walks one directory non-recursively; for each
// .json file, parses and forwards to the type's from_json. The visitor
// decides what to do with the parsed value. Centralized here so both
// registries share the same iteration + error reporting.
// ---------------------------------------------------------------------------

namespace
{
template <typename T, typename Visitor>
int loadJsonDirectory(const std::string& dir, const char* what, Visitor&& visit)
{
    namespace fs = std::filesystem;
    int loaded = 0;
    std::error_code ec;
    if (!fs::exists(dir, ec) || !fs::is_directory(dir, ec))
    {
        std::fprintf(stderr, "[combat] %s directory not found: %s\n", what, dir.c_str());
        return 0;
    }
    for (const auto& entry : fs::directory_iterator(dir, ec))
    {
        if (!entry.is_regular_file())
            continue;
        const auto& path = entry.path();
        if (path.extension() != ".json")
            continue;
        std::ifstream f(path);
        if (!f.is_open())
        {
            std::fprintf(stderr, "[combat] failed to open %s\n", path.string().c_str());
            continue;
        }
        try
        {
            const auto j = nlohmann::json::parse(f);
            T value;
            j.get_to(value);
            visit(std::move(value), path);
            ++loaded;
        }
        catch (const std::exception& ex)
        {
            std::fprintf(stderr, "[combat] failed to parse %s as %s: %s\n", path.string().c_str(),
                         what, ex.what());
        }
    }
    return loaded;
}
} // namespace

// ---------------------------------------------------------------------------
// WeaponClassRegistry
// ---------------------------------------------------------------------------

int WeaponClassRegistry::loadDirectory(const std::string& dir)
{
    return loadJsonDirectory<WeaponClass>(
        dir, "weapon_class",
        [&](WeaponClass cls, const std::filesystem::path& path)
        {
            if (cls.id.empty())
            {
                std::fprintf(stderr, "[combat] weapon_class %s missing 'id' field; skipping\n",
                             path.string().c_str());
                return;
            }
            by_id.emplace(cls.id, std::move(cls));
        });
}

const WeaponClass* WeaponClassRegistry::get(const std::string& id) const
{
    const auto it = by_id.find(id);
    return it == by_id.end() ? nullptr : &it->second;
}

// ---------------------------------------------------------------------------
// WeaponRegistry — also resolves each weapon's `cls` pointer against the
// supplied class registry. Weapons with an unknown class_id are loaded
// anyway (so the missing class is visible, not silently dropped) but
// their cls pointer stays null — combat code must handle that.
// ---------------------------------------------------------------------------

int WeaponRegistry::loadDirectory(const std::string& dir, const WeaponClassRegistry& classes)
{
    return loadJsonDirectory<Weapon>(
        dir, "weapon",
        [&](Weapon w, const std::filesystem::path& path)
        {
            if (w.id.empty())
            {
                std::fprintf(stderr, "[combat] weapon %s missing 'id' field; skipping\n",
                             path.string().c_str());
                return;
            }
            w.cls = classes.get(w.class_id);
            if (w.cls == nullptr)
            {
                std::fprintf(stderr, "[combat] weapon '%s' references unknown class '%s'\n",
                             w.id.c_str(), w.class_id.c_str());
            }
            by_id.emplace(w.id, std::move(w));
        });
}

const Weapon* WeaponRegistry::get(const std::string& id) const
{
    const auto it = by_id.find(id);
    return it == by_id.end() ? nullptr : &it->second;
}

// ---------------------------------------------------------------------------
// PlayerEquipment loader
// ---------------------------------------------------------------------------

PlayerEquipment loadEquipment(const std::string& loadout_path, const WeaponRegistry& weapons)
{
    PlayerEquipment eq;
    std::ifstream f(loadout_path);
    if (!f.is_open())
    {
        std::fprintf(stderr, "[combat] loadout file not found: %s (using empty hands)\n",
                     loadout_path.c_str());
        return eq;
    }
    Loadout lo;
    try
    {
        const auto j = nlohmann::json::parse(f);
        j.get_to(lo);
    }
    catch (const std::exception& ex)
    {
        std::fprintf(stderr, "[combat] failed to parse %s: %s\n", loadout_path.c_str(), ex.what());
        return eq;
    }

    if (!lo.right_hand.empty())
    {
        eq.right = weapons.get(lo.right_hand);
        if (eq.right == nullptr)
            std::fprintf(stderr, "[combat] loadout: unknown right_hand weapon '%s'\n",
                         lo.right_hand.c_str());
    }
    if (!lo.left_hand.empty())
    {
        eq.left = weapons.get(lo.left_hand);
        if (eq.left == nullptr)
            std::fprintf(stderr, "[combat] loadout: unknown left_hand weapon '%s'\n",
                         lo.left_hand.c_str());
    }

    if (lo.grip == "two_handed")
        eq.grip = Grip::TwoHanded;
    else
        eq.grip = Grip::OneHanded;

    return eq;
}

} // namespace selva::combat
