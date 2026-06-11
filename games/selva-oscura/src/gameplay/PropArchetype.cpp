#include "gameplay/PropArchetype.h"

#include "combat/CombatLog.h"

#include <nlohmann/json.hpp>

#include <cstdio>
#include <fstream>

namespace selva::gameplay
{

PropCategory parsePropCategory(const std::string& s)
{
    if (s == "tree")
        return PropCategory::Tree;
    if (s == "light")
        return PropCategory::Light;
    if (s == "unknown" || s.empty())
        return PropCategory::Unknown;
    selva::combat::combatLog("[prop-archetype] unknown category '{}'; defaulting to Unknown", s);
    return PropCategory::Unknown;
}

PropCollisionKind parsePropCollisionKind(const std::string& s)
{
    if (s == "none" || s.empty())
        return PropCollisionKind::None;
    if (s == "cylinder")
        return PropCollisionKind::Cylinder;
    if (s == "aabb")
        return PropCollisionKind::Aabb;
    selva::combat::combatLog("[prop-archetype] unknown collision_kind '{}'; defaulting to None", s);
    return PropCollisionKind::None;
}

namespace
{

// Parse the optional light_source block. Sets has_light_source true
// only when the JSON contains the key; absent => default-empty +
// flag stays false.
void loadLightSource(const nlohmann::json& j, PropArchetype& a)
{
    if (!j.contains("light_source") || !j["light_source"].is_object())
        return;
    const auto& ls = j["light_source"];
    if (ls.contains("color") && ls["color"].is_array() && ls["color"].size() == 3)
    {
        a.light_source.color[0] = ls["color"][0].get<float>();
        a.light_source.color[1] = ls["color"][1].get<float>();
        a.light_source.color[2] = ls["color"][2].get<float>();
    }
    a.light_source.intensity = ls.value("intensity", 1.0f);
    a.light_source.range_meters = ls.value("range_meters", 0.0f);
    a.light_source.flicker_amp = ls.value("flicker_amp", 0.0f);
    a.light_source.flicker_freq = ls.value("flicker_freq", 0.0f);
    a.light_source.kind = ls.value("kind", std::string{});
    a.has_light_source = true;
}

void loadHarvestable(const nlohmann::json& j, PropArchetype& a)
{
    if (!j.contains("harvestable") || !j["harvestable"].is_object())
        return;
    const auto& h = j["harvestable"];
    a.harvestable.resource_id = h.value("resource_id", std::string{});
    a.harvestable.yield_amount = h.value("yield_amount", 0);
    a.harvestable.cooldown_seconds = h.value("cooldown_seconds", 0.0f);
    a.harvestable.depletes = h.value("depletes", false);
    a.has_harvestable = true;
}

void loadVisualFields(const nlohmann::json& j, PropArchetype& a)
{
    a.mesh_path = j.value("mesh_path", std::string{});
    if (j.contains("mesh_node_filter") && j["mesh_node_filter"].is_array())
    {
        for (const auto& n : j["mesh_node_filter"])
            if (n.is_string())
                a.mesh_node_filter.push_back(n.get<std::string>());
    }
    if (j.contains("texture_overrides") && j["texture_overrides"].is_object())
    {
        for (auto it = j["texture_overrides"].begin(); it != j["texture_overrides"].end(); ++it)
        {
            if (it.value().is_string())
                a.texture_overrides.emplace(it.key(), it.value().get<std::string>());
        }
    }
    if (j.contains("scale_jitter_range") && j["scale_jitter_range"].is_array() &&
        j["scale_jitter_range"].size() == 2)
    {
        a.scale_jitter_range[0] = j["scale_jitter_range"][0].get<float>();
        a.scale_jitter_range[1] = j["scale_jitter_range"][1].get<float>();
    }
    a.alpha_cutoff = j.value("alpha_cutoff", 0.0f);
    if (j.contains("foliage_tint") && j["foliage_tint"].is_array() && j["foliage_tint"].size() == 3)
    {
        a.foliage_tint[0] = j["foliage_tint"][0].get<float>();
        a.foliage_tint[1] = j["foliage_tint"][1].get<float>();
        a.foliage_tint[2] = j["foliage_tint"][2].get<float>();
    }
}

void loadCollisionFields(const nlohmann::json& j, PropArchetype& a)
{
    a.collision_kind = parsePropCollisionKind(j.value("collision_kind", std::string{}));
    a.cylinder_radius = j.value("cylinder_radius", 0.0f);
    a.cylinder_half_height = j.value("cylinder_half_height", 0.0f);
    if (j.contains("collision_aabb_half_extents") && j["collision_aabb_half_extents"].is_array() &&
        j["collision_aabb_half_extents"].size() == 3)
    {
        a.collision_aabb_half_extents[0] = j["collision_aabb_half_extents"][0].get<float>();
        a.collision_aabb_half_extents[1] = j["collision_aabb_half_extents"][1].get<float>();
        a.collision_aabb_half_extents[2] = j["collision_aabb_half_extents"][2].get<float>();
    }
}

void loadInteractableFields(const nlohmann::json& j, PropArchetype& a)
{
    a.interactable_kind = j.value("interactable_kind", std::string{});
    a.interact_range_meters = j.value("interact_range_meters", 0.0f);
    a.examine_text = j.value("examine_text", std::string{});
    a.examine_text_key = j.value("examine_text_key", std::string{});
    if (j.contains("examine_text_keys") && j["examine_text_keys"].is_array())
    {
        for (const auto& k : j["examine_text_keys"])
            if (k.is_string())
                a.examine_text_keys.push_back(k.get<std::string>());
    }
    a.examine_label_key = j.value("examine_label_key", std::string{});
    a.display_name = j.value("display_name", std::string{});
    a.display_name_key = j.value("display_name_key", std::string{});
    a.interactable_requires_flag = j.value("interactable_requires_flag", std::string{});
    a.insight_trigger_subject = j.value("insight_trigger_subject", std::string{});
}

} // namespace

void from_json(const nlohmann::json& j, PropArchetype& a)
{
    j.at("id").get_to(a.id);
    a.category = parsePropCategory(j.value("category", std::string{}));
    loadVisualFields(j, a);
    loadCollisionFields(j, a);
    loadInteractableFields(j, a);
    loadLightSource(j, a);
    loadHarvestable(j, a);
}

void PropArchetypeRegistry::loadDirectory(const std::filesystem::path& dir)
{
    if (!std::filesystem::exists(dir) || !std::filesystem::is_directory(dir))
    {
        std::fprintf(stderr, "[prop-archetype] directory not found: %s\n", dir.string().c_str());
        return;
    }
    // Pass 1: raw JSON load. Defer deserialize so the inheritance
    // chain can splice parent fields BEFORE the struct gets built --
    // identical pattern to EnemyArchetypeRegistry::loadDirectory.
    std::unordered_map<std::string, nlohmann::json> raw_by_id;
    std::unordered_map<std::string, std::filesystem::path> path_by_id;
    for (const auto& entry : std::filesystem::directory_iterator(dir))
    {
        if (!entry.is_regular_file() || entry.path().extension() != ".json")
            continue;
        std::ifstream in(entry.path());
        if (!in)
        {
            selva::combat::combatLog("[prop-archetype] cannot open {}", entry.path().string());
            continue;
        }
        try
        {
            nlohmann::json j;
            in >> j;
            const std::string id = j.value("id", std::string{});
            if (id.empty())
            {
                selva::combat::combatLog("[prop-archetype] {} has empty id; skipping",
                                         entry.path().string());
                continue;
            }
            raw_by_id[id] = std::move(j);
            path_by_id[id] = entry.path();
        }
        catch (const std::exception& e)
        {
            selva::combat::combatLog("[prop-archetype] parse error in {}: {}",
                                     entry.path().string(), e.what());
        }
    }
    // Pass 2: deserialize. Same merge_patch + inherits chain as
    // EnemyArchetypeRegistry. Depth-limit guards against cycles.
    constexpr int kMaxInheritDepth = 8;
    for (auto& [id, raw] : raw_by_id)
    {
        try
        {
            nlohmann::json merged = raw;
            std::string cursor = raw.value("inherits", std::string{});
            int depth = 0;
            while (!cursor.empty())
            {
                if (++depth > kMaxInheritDepth)
                {
                    selva::combat::combatLog(
                        "[prop-archetype] '{}' inherits chain exceeded depth {}; aborting merge",
                        id, kMaxInheritDepth);
                    break;
                }
                auto parent_it = raw_by_id.find(cursor);
                if (parent_it == raw_by_id.end())
                {
                    selva::combat::combatLog(
                        "[prop-archetype] '{}' inherits unknown parent '{}'; skipping merge", id,
                        cursor);
                    break;
                }
                nlohmann::json next = parent_it->second;
                next.merge_patch(merged);
                merged = std::move(next);
                cursor = parent_it->second.value("inherits", std::string{});
            }
            merged.erase("inherits");
            merged["id"] = id;
            PropArchetype arch = merged.get<PropArchetype>();
            by_id[id] = std::move(arch);
            selva::combat::combatLog("[prop-archetype] loaded '{}' from {}", id,
                                     path_by_id[id].string());
        }
        catch (const std::exception& e)
        {
            selva::combat::combatLog("[prop-archetype] deserialize error in '{}': {}", id,
                                     e.what());
        }
    }
    std::fprintf(stderr, "[prop-archetype] total archetypes loaded: %zu\n", by_id.size());
}

const PropArchetype* PropArchetypeRegistry::get(const std::string& id) const
{
    auto it = by_id.find(id);
    return (it != by_id.end()) ? &it->second : nullptr;
}

PropArchetypeRegistry& propArchetypes()
{
    static PropArchetypeRegistry s_registry;
    return s_registry;
}

} // namespace selva::gameplay
