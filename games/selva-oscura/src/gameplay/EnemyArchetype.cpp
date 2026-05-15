#include "gameplay/EnemyArchetype.h"

#include "combat/CombatLog.h"

#include <nlohmann/json.hpp>

#include <fstream>

namespace selva::gameplay
{

namespace
{

Awareness parseAwareness(const std::string& s)
{
    if (s == "Unaware")
        return Awareness::Unaware;
    if (s == "Suspicious")
        return Awareness::Suspicious;
    if (s == "Alerted")
        return Awareness::Alerted;
    return Awareness::Combat; // default; also matches "Combat"
}

const char* awarenessString(Awareness a)
{
    switch (a)
    {
    case Awareness::Unaware:
        return "Unaware";
    case Awareness::Suspicious:
        return "Suspicious";
    case Awareness::Alerted:
        return "Alerted";
    case Awareness::Combat:
        break;
    }
    return "Combat";
}

} // namespace

void to_json(nlohmann::json& j, const EnemyAction& a)
{
    j = {{"id", a.id},
         {"clip", a.clip},
         {"range_min", a.range_min},
         {"range_max", a.range_max},
         {"cooldown_seconds", a.cooldown_seconds},
         {"weight", a.weight},
         {"raw_damage", a.raw_damage},
         {"poise_damage", a.poise_damage},
         {"blend_in_seconds", a.blend_in_seconds},
         {"blend_out_seconds", a.blend_out_seconds},
         {"freeze_last", a.freeze_last},
         {"min_awareness", awarenessString(a.min_awareness)},
         {"hitbox_joint", a.hitbox_joint},
         {"hitbox_radius", a.hitbox_radius},
         {"hitbox_tip_offset_z", a.hitbox_tip_offset_z}};
}

void from_json(const nlohmann::json& j, EnemyAction& a)
{
    j.at("id").get_to(a.id);
    j.at("clip").get_to(a.clip);
    a.range_min = j.value("range_min", 0.0f);
    a.range_max = j.value("range_max", 0.0f);
    a.cooldown_seconds = j.value("cooldown_seconds", 0.0f);
    a.weight = j.value("weight", 1.0f);
    a.raw_damage = j.value("raw_damage", 0);
    a.poise_damage = j.value("poise_damage", 0);
    a.blend_in_seconds = j.value("blend_in_seconds", 0.10f);
    a.blend_out_seconds = j.value("blend_out_seconds", 0.20f);
    a.freeze_last = j.value("freeze_last", false);
    a.min_awareness = parseAwareness(j.value("min_awareness", std::string("Combat")));
    a.hitbox_joint = j.value("hitbox_joint", std::string{});
    a.hitbox_radius = j.value("hitbox_radius", 0.18f);
    a.hitbox_tip_offset_z = j.value("hitbox_tip_offset_z", 0.0f);
}

void to_json(nlohmann::json& j, const EnemyArchetype& a)
{
    j = nlohmann::json{{"id", a.id}, {"actions", a.actions}, {"tree", a.tree_id}};
    if (a.vision_fov_degrees.has_value())
        j["vision_fov_degrees"] = *a.vision_fov_degrees;
    if (a.vision_range_meters.has_value())
        j["vision_range_meters"] = *a.vision_range_meters;
}

void from_json(const nlohmann::json& j, EnemyArchetype& a)
{
    j.at("id").get_to(a.id);
    a.actions = j.value("actions", std::vector<EnemyAction>{});
    a.tree_id = j.value("tree", std::string("humanoid_basic"));
    if (j.contains("vision_fov_degrees"))
        a.vision_fov_degrees = j.at("vision_fov_degrees").get<float>();
    if (j.contains("vision_range_meters"))
        a.vision_range_meters = j.at("vision_range_meters").get<float>();
}

void EnemyArchetypeRegistry::loadDirectory(const std::filesystem::path& dir)
{
    if (!std::filesystem::exists(dir) || !std::filesystem::is_directory(dir))
    {
        selva::combat::combatLog("[archetype] directory not found: %s\n", dir.string().c_str());
        return;
    }
    for (const auto& entry : std::filesystem::directory_iterator(dir))
    {
        if (!entry.is_regular_file())
            continue;
        if (entry.path().extension() != ".json")
            continue;
        std::ifstream in(entry.path());
        if (!in)
        {
            selva::combat::combatLog("[archetype] cannot open %s\n", entry.path().string().c_str());
            continue;
        }
        try
        {
            nlohmann::json j;
            in >> j;
            EnemyArchetype arch = j.get<EnemyArchetype>();
            if (arch.id.empty())
            {
                selva::combat::combatLog("[archetype] %s has empty id; skipping\n",
                                         entry.path().string().c_str());
                continue;
            }
            const std::string id = arch.id;
            by_id_[id] = std::move(arch);
            selva::combat::combatLog("[archetype] loaded '%s' (%zu actions) from %s\n", id.c_str(),
                                     by_id_[id].actions.size(), entry.path().string().c_str());
        }
        catch (const std::exception& e)
        {
            selva::combat::combatLog("[archetype] parse error in %s: %s\n",
                                     entry.path().string().c_str(), e.what());
        }
    }
}

const EnemyArchetype* EnemyArchetypeRegistry::get(const std::string& id) const
{
    const auto it = by_id_.find(id);
    return (it == by_id_.end()) ? nullptr : &it->second;
}

EnemyArchetypeRegistry& archetypes()
{
    static EnemyArchetypeRegistry instance;
    return instance;
}

} // namespace selva::gameplay
