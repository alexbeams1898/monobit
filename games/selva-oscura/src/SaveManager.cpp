#include "SaveManager.h"

#include <nlohmann/json.hpp>

#include <SDL.h>

#include <algorithm>
#include <cstdio>
#include <filesystem>
#include <fstream>

using json = nlohmann::json;

namespace selva::SaveManager
{

// ---------------------------------------------------------------------------
// Save directory resolution
// ---------------------------------------------------------------------------

std::string getSaveDir()
{
    static std::string cached;
    if (!cached.empty())
        return cached;

    char* pref = SDL_GetPrefPath("SelvaOscura", "SelvaOscura");
    if (pref != nullptr)
    {
        cached = pref;
        SDL_free(pref);
    }
    else
    {
        cached = "saves/";
    }
    return cached;
}

std::string defaultSavePath()
{
    return getSaveDir() + "save.json";
}

// ---------------------------------------------------------------------------
// Public API
// ---------------------------------------------------------------------------

namespace
{
void loadFelledBosses(const json& c, PlayerProfile& p)
{
    if (!c.contains("felled_bosses") || !c["felled_bosses"].is_array())
        return;
    for (const auto& b : c["felled_bosses"])
        if (b.is_string())
            p.felled_bosses.push_back(b.get<std::string>());
}

void loadFlags(const json& c, PlayerProfile& p)
{
    if (!c.contains("flags") || !c["flags"].is_array())
        return;
    for (const auto& f : c["flags"])
        if (f.is_string())
            p.flags.push_back(f.get<std::string>());
}

// Load the player's unlocked insight node ids.
void loadInsightUnlockedNodes(const json& c, PlayerProfile& p)
{
    if (!c.contains("unlocked_insights") || !c["unlocked_insights"].is_array())
        return;
    for (const auto& n : c["unlocked_insights"])
        if (n.is_string())
            p.unlocked_insights.push_back(n.get<std::string>());
}

// Load a per-key uint32 map field (kill_counts, examine_counts).
void loadInsightCountMap(const json& c, const char* key,
                         std::unordered_map<std::string, std::uint32_t>& dst)
{
    if (!c.contains(key) || !c[key].is_object())
        return;
    for (auto it = c[key].begin(); it != c[key].end(); ++it)
        if (it.value().is_number_unsigned())
            dst[it.key()] = it.value().get<std::uint32_t>();
}

// Load the deprecated certain_conclusions list (back-compat).
void loadCertainConclusions(const json& c, PlayerProfile& p)
{
    if (!c.contains("certain_conclusions") || !c["certain_conclusions"].is_array())
        return;
    for (const auto& n : c["certain_conclusions"])
        if (n.is_string())
            p.certain_conclusions.push_back(n.get<std::string>());
}

// Load an array-of-strings JSON field into a string vector.
void loadStringArrayField(const json& n, const char* key, std::vector<std::string>& dst)
{
    if (!n.contains(key) || !n[key].is_array())
        return;
    for (const auto& s : n[key])
        if (s.is_string())
            dst.push_back(s.get<std::string>());
}

// Load a single workbench node object into a WorkbenchNode struct.
// Returns false if the node is missing its required `id` field.
bool loadWorkbenchOne(const json& n, PlayerProfile::WorkbenchNode& w)
{
    if (!n.is_object() || !n.contains("id") || !n["id"].is_string())
        return false;
    w.id = n["id"].get<std::string>();
    if (n.contains("x") && n["x"].is_number())
        w.x = n["x"].get<float>();
    if (n.contains("y") && n["y"].is_number())
        w.y = n["y"].get<float>();
    loadStringArrayField(n, "linked_observations", w.linked_observations);
    if (n.contains("reading_id") && n["reading_id"].is_string())
        w.reading_id = n["reading_id"].get<std::string>();
    loadStringArrayField(n, "linked_confirmers", w.linked_confirmers);
    return true;
}

// Load one workbench-node array into the destination vector.
void loadWorkbenchVec(const json& arr, std::vector<PlayerProfile::WorkbenchNode>& out)
{
    for (const auto& n : arr)
    {
        PlayerProfile::WorkbenchNode w;
        if (loadWorkbenchOne(n, w))
            out.push_back(std::move(w));
    }
}

// Load workbench observations + inferences (with back-compat for
// `workbench_conclusions`, the cognition-system-v1 prior name).
void loadWorkbenchNodes(const json& c, PlayerProfile& p)
{
    if (c.contains("workbench_observations") && c["workbench_observations"].is_array())
        loadWorkbenchVec(c["workbench_observations"], p.workbench_observations);
    if (c.contains("workbench_inferences") && c["workbench_inferences"].is_array())
        loadWorkbenchVec(c["workbench_inferences"], p.workbench_inferences);
    else if (c.contains("workbench_conclusions") && c["workbench_conclusions"].is_array())
        loadWorkbenchVec(c["workbench_conclusions"], p.workbench_inferences);
}

void loadInsights(const json& c, PlayerProfile& p)
{
    loadInsightUnlockedNodes(c, p);
    loadInsightCountMap(c, "kill_counts", p.kill_counts);
    loadCertainConclusions(c, p);
    loadInsightCountMap(c, "examine_counts", p.examine_counts);
    loadWorkbenchNodes(c, p);
    p.perception_growth = c.value("perception_growth", std::uint32_t{0});
    p.cognition_growth = c.value("cognition_growth", std::uint32_t{0});
    p.intelligence_growth = c.value("intelligence_growth", std::uint32_t{0});
}

void loadDoorStates(const json& c, PlayerProfile& p)
{
    if (!c.contains("door_states") || !c["door_states"].is_array())
        return;
    for (const auto& entry : c["door_states"])
    {
        if (entry.is_array() && entry.size() == 2 && entry[0].is_string() && entry[1].is_string())
        {
            p.door_states.emplace_back(entry[0].get<std::string>(), entry[1].get<std::string>());
        }
    }
}

void loadGatherState(const json& c, PlayerProfile& p)
{
    if (c.contains("active_gather_nodes") && c["active_gather_nodes"].is_array())
    {
        for (const auto& e : c["active_gather_nodes"])
        {
            if (!e.is_object() || !e.contains("node_config_path") ||
                !e["node_config_path"].is_string())
                continue;
            selva::gather::NodeState n;
            n.id = e.value("id", std::uint32_t{0});
            n.node_config_path = e["node_config_path"].get<std::string>();
            n.material_config_path = e.value("material_config_path", std::string{});
            n.pos_x = e.value("pos_x", 0.0f);
            n.pos_y = e.value("pos_y", 0.0f);
            n.pos_z = e.value("pos_z", 0.0f);
            n.quality = static_cast<engine::ecs::QualityTier>(
                e.value("quality", static_cast<int>(engine::ecs::QualityTier::Common)));
            n.yaw = e.value("yaw", 0.0f);
            p.active_gather_nodes.push_back(std::move(n));
        }
    }
    p.next_gather_node_id = c.value("next_gather_node_id", std::uint32_t{1});
    if (c.contains("gather_flows") && c["gather_flows"].is_array())
    {
        for (const auto& e : c["gather_flows"])
        {
            if (!e.is_object() || !e.contains("node_config_path") ||
                !e["node_config_path"].is_string())
                continue;
            selva::gather::FlowState f;
            f.node_config_path = e["node_config_path"].get<std::string>();
            f.last_spawn_wallclock = e.value("last_spawn_wallclock", 0.0f);
            f.initial_fill_done = e.value("initial_fill_done", false);
            p.gather_flows.push_back(std::move(f));
        }
    }
}

engine::ecs::ItemInstance loadInventoryEntry(const json& e)
{
    engine::ecs::ItemInstance item;
    item.id = e.value("id", std::uint64_t{0});
    item.config_path = e.value("config_path", std::string{});
    item.quality = static_cast<engine::ecs::QualityTier>(
        e.value("quality", static_cast<int>(engine::ecs::QualityTier::Common)));
    // Size: legacy saves (pre-v9) have no "size" key; default Normal
    // preserves their visible behavior exactly. Stored as int for
    // symmetry with quality.
    item.size = static_cast<engine::ecs::WeaponSize>(
        e.value("size", static_cast<int>(engine::ecs::WeaponSize::Normal)));
    item.durability = e.value("durability", 100.0f);
    item.quantity = e.value("quantity", 1);
    item.evolution_bonus = e.value("evolution_bonus", 0.0f);
    item.newly_discovered = e.value("newly_discovered", false);
    item.weapon_xp_level = e.value("weapon_xp_level", 1);
    item.weapon_xp_current = e.value("weapon_xp_current", 0.0f);
    return item;
}

void loadInventory(const json& c, PlayerProfile& p)
{
    if (!c.contains("inventory") || !c["inventory"].is_object())
        return;
    const auto& inv = c["inventory"];
    p.inventory.next_id = inv.value("next_id", std::uint64_t{1});
    if (!inv.contains("by_category") || !inv["by_category"].is_object())
        return;
    for (const auto& [cat_id, entries] : inv["by_category"].items())
    {
        if (!entries.is_array())
            continue;
        auto& vec = p.inventory.by_category[cat_id];
        for (const auto& e : entries)
        {
            if (!e.is_object() || !e.contains("config_path"))
                continue;
            vec.push_back(loadInventoryEntry(e));
        }
    }
}

void loadEquipment(const json& c, PlayerProfile& p)
{
    if (!c.contains("equipment") || !c["equipment"].is_object())
        return;
    const auto& e = c["equipment"];
    using Id = engine::ecs::ItemInstanceId;
    p.equipment.right_hand = e.value("right_hand", Id{0});
    p.equipment.left_hand = e.value("left_hand", Id{0});
    p.equipment.head = e.value("head", Id{0});
    p.equipment.chest = e.value("chest", Id{0});
    p.equipment.legs = e.value("legs", Id{0});
    p.equipment.feet = e.value("feet", Id{0});
    p.equipment.accessory_1 = e.value("accessory_1", Id{0});
    p.equipment.accessory_2 = e.value("accessory_2", Id{0});
}

void loadCompendium(const json& c, PlayerProfile& p)
{
    if (!c.contains("compendium") || !c["compendium"].is_array())
        return;
    for (const auto& entry : c["compendium"])
    {
        if (entry.is_string())
            p.compendium.discover(entry.get<std::string>());
    }
}

void loadUnreadNotices(const json& c, PlayerProfile& p)
{
    if (!c.contains("unread_notices") || !c["unread_notices"].is_array())
        return;
    for (const auto& entry : c["unread_notices"])
    {
        if (entry.is_string())
            p.unread_notices.insert(entry.get<std::string>());
    }
}

void loadNpcState(const json& c, PlayerProfile& p)
{
    if (!c.contains("npc_state") || !c["npc_state"].is_object())
        return;
    for (const auto& [npc_id, rec] : c["npc_state"].items())
    {
        if (!rec.is_object())
            continue;
        auto& state = p.npc_state[npc_id];
        state.times_talked = rec.value("times_talked", 0);
        if (rec.contains("topics_seen") && rec["topics_seen"].is_array())
            for (const auto& t : rec["topics_seen"])
                if (t.is_string())
                    state.topics_seen.insert(t.get<std::string>());
    }
}

PlayerProfile loadCharacter(const json& c)
{
    PlayerProfile p;
    p.name = c.value("name", std::string{});
    p.pos_x = c.value("pos_x", 0.0f);
    p.pos_y = c.value("pos_y", 0.0f);
    p.pos_z = c.value("pos_z", 0.0f);
    p.yaw = c.value("yaw", 0.0f);
    p.has_saved_pose = c.value("has_saved_pose", false);
    // Back-compat: legacy region ids (chapel_interior, acheron) all
    // collapsed into "surface" before the Scenes-to-Regions rename.
    p.current_region_id = c.value("current_region_id", std::string{"surface"});
    if (p.current_region_id == "chapel_interior" || p.current_region_id == "acheron")
        p.current_region_id = "surface";
    loadFelledBosses(c, p);
    loadFlags(c, p);
    loadInsights(c, p);
    loadDoorStates(c, p);
    loadGatherState(c, p);
    loadInsightCountMap(c, "craft_counts", p.craft_counts);
    loadStringArrayField(c, "known_recipes", p.known_recipes);
    loadStringArrayField(c, "quick_slot_assigned", p.quick_slot_assigned);
    p.quick_slot_primed_index = c.value("quick_slot_primed_index", -1);
    loadInventory(c, p);
    loadEquipment(c, p);
    loadCompendium(c, p);
    loadUnreadNotices(c, p);
    loadNpcState(c, p);
    // Sangue. Absent on legacy saves (pre-currency-system) and on
    // fresh characters; both cases default to 0. value<uint32_t>
    // tolerates the field being absent or numeric of a smaller type.
    p.sangue_lifetime = c.value("sangue_lifetime", std::uint32_t{0});
    p.sangue_vessel = c.value("sangue_vessel", std::uint32_t{0});
    p.sangue_riversato = c.value("sangue_riversato", std::uint32_t{0});
    if (p.sangue_lifetime > SANGUE_LIFETIME_CAP)
        p.sangue_lifetime = SANGUE_LIFETIME_CAP;
    if (p.sangue_vessel > SANGUE_LIFETIME_CAP)
        p.sangue_vessel = SANGUE_LIFETIME_CAP;
    if (p.sangue_riversato > SANGUE_LIFETIME_CAP)
        p.sangue_riversato = SANGUE_LIFETIME_CAP;
    p.player_class = parsePlayerClass(c.value("player_class", std::string{}));
    // Load appearance_path, with legacy v7 saves' shade_path as
    // fallback. Both keys are tolerated; migrate() runs afterward and
    // backfills the canonical default for any character still empty.
    p.appearance_path = c.value("appearance_path", c.value("shade_path", std::string{}));
    return p;
}

void loadSettings(const json& j, SaveData& data)
{
    if (!j.contains("settings") || !j["settings"].is_object())
        return;
    const auto& s = j["settings"];
    data.settings.bgm_volume = s.value("bgm_volume", data.settings.bgm_volume);
    data.settings.sfx_volume = s.value("sfx_volume", data.settings.sfx_volume);
    data.settings.fov_degrees_third_person =
        s.value("fov_degrees_third_person", data.settings.fov_degrees_third_person);
    data.settings.fov_degrees_first_person =
        s.value("fov_degrees_first_person", data.settings.fov_degrees_first_person);
    data.settings.show_interact_ring =
        s.value("show_interact_ring", data.settings.show_interact_ring);
    data.settings.auto_assign_consumables_to_quick_slot =
        s.value("auto_assign_consumables_to_quick_slot",
                data.settings.auto_assign_consumables_to_quick_slot);
}
} // namespace

SaveData load(const std::string& path)
{
    const std::string resolved = path.empty() ? defaultSavePath() : path;
    SaveData data;
    std::ifstream file(resolved);
    if (!file.is_open())
        return data;
    json j;
    try
    {
        file >> j;
    }
    catch (const std::exception& e)
    {
        std::fprintf(stderr, "[SaveManager] Error parsing %s: %s -- using defaults\n",
                     resolved.c_str(), e.what());
        return data;
    }
    data.schema_version = j.value("schema_version", 1);
    if (data.schema_version > SaveData::CURRENT_VERSION)
    {
        std::fprintf(stderr,
                     "[SaveManager] save file is newer than this build "
                     "(schema=%d > CURRENT_VERSION=%d); fields added in the newer "
                     "schema will be ignored, and migrate() will rewrite the "
                     "version to %d on next save. If you're playing an older build "
                     "of an in-development save, expect data loss.\n",
                     data.schema_version, SaveData::CURRENT_VERSION, SaveData::CURRENT_VERSION);
        std::fflush(stderr);
    }
    if (j.contains("characters") && j["characters"].is_array())
    {
        for (const auto& c : j["characters"])
        {
            // Per the unnamed-but-real character pattern: empty name is
            // a valid character (the in-progress unnamed Vagrant before
            // the Guide's naming dialog). Don't filter it out on load.
            data.characters.push_back(loadCharacter(c));
        }
    }
    loadSettings(j, data);
    if (j.value("has_last_played", false))
    {
        data.has_last_played = true;
        data.last_played_character = j.value("last_played_character", std::string{});
    }
    // migrate AFTER characters + settings are loaded -- previously
    // migrate ran before the character vector was populated, so any
    // backfill logic targeting characters was a no-op (the loop
    // iterated over an empty vector). Per
    // [[feedback_rebuild_from_authored_on_reset]] the migration is
    // the authoring source for upgrading old save shape; it must run
    // when the data it's migrating is actually present.
    migrate(data);
    std::fprintf(stderr, "[SaveManager] Loaded %zu characters from %s\n", data.characters.size(),
                 resolved.c_str());
    return data;
}

namespace
{
nlohmann::json saveDoorStates(const PlayerProfile& c)
{
    nlohmann::json arr = nlohmann::json::array();
    for (const auto& pair : c.door_states)
        arr.push_back({pair.first, pair.second});
    return arr;
}

nlohmann::json saveActiveGatherNodes(const PlayerProfile& c)
{
    nlohmann::json arr = nlohmann::json::array();
    for (const auto& n : c.active_gather_nodes)
    {
        arr.push_back({
            {"id", n.id},
            {"node_config_path", n.node_config_path},
            {"material_config_path", n.material_config_path},
            {"pos_x", n.pos_x},
            {"pos_y", n.pos_y},
            {"pos_z", n.pos_z},
            {"quality", static_cast<int>(n.quality)},
            {"yaw", n.yaw},
        });
    }
    return arr;
}

nlohmann::json saveGatherFlows(const PlayerProfile& c)
{
    nlohmann::json arr = nlohmann::json::array();
    for (const auto& f : c.gather_flows)
    {
        arr.push_back({
            {"node_config_path", f.node_config_path},
            {"last_spawn_wallclock", f.last_spawn_wallclock},
            {"initial_fill_done", f.initial_fill_done},
        });
    }
    return arr;
}

// Round-trip every ItemInstance field even when at its default --
// instance state isn't a side-channel like flags; it's the item's
// content. Skip-emit-when-default would create silent divergence on
// reload.
nlohmann::json saveInventoryEntry(const engine::ecs::ItemInstance& it)
{
    return nlohmann::json{
        {"id", it.id},
        {"config_path", it.config_path},
        {"quality", static_cast<int>(it.quality)},
        {"size", static_cast<int>(it.size)},
        {"durability", it.durability},
        {"quantity", it.quantity},
        {"evolution_bonus", it.evolution_bonus},
        {"newly_discovered", it.newly_discovered},
        {"weapon_xp_level", it.weapon_xp_level},
        {"weapon_xp_current", it.weapon_xp_current},
    };
}

nlohmann::json saveInventory(const PlayerProfile& c)
{
    nlohmann::json inv = nlohmann::json::object();
    inv["next_id"] = c.inventory.next_id;
    nlohmann::json by_cat = nlohmann::json::object();
    for (const auto& [cat_id, entries] : c.inventory.by_category)
    {
        if (entries.empty())
            continue;
        nlohmann::json arr = nlohmann::json::array();
        for (const auto& e : entries)
            arr.push_back(saveInventoryEntry(e));
        by_cat[cat_id] = std::move(arr);
    }
    inv["by_category"] = std::move(by_cat);
    return inv;
}

bool equipmentIsEmpty(const engine::ecs::Equipment& e)
{
    using engine::ecs::kInvalidItemInstanceId;
    return e.right_hand == kInvalidItemInstanceId && e.left_hand == kInvalidItemInstanceId &&
           e.head == kInvalidItemInstanceId && e.chest == kInvalidItemInstanceId &&
           e.legs == kInvalidItemInstanceId && e.feet == kInvalidItemInstanceId &&
           e.accessory_1 == kInvalidItemInstanceId && e.accessory_2 == kInvalidItemInstanceId;
}

nlohmann::json saveEquipment(const engine::ecs::Equipment& e)
{
    return nlohmann::json{
        {"right_hand", e.right_hand},
        {"left_hand", e.left_hand},
        {"head", e.head},
        {"chest", e.chest},
        {"legs", e.legs},
        {"feet", e.feet},
        {"accessory_1", e.accessory_1},
        {"accessory_2", e.accessory_2},
    };
}

nlohmann::json saveCompendium(const engine::ecs::Compendium& c)
{
    nlohmann::json arr = nlohmann::json::array();
    for (const auto& path : c.discovered)
        arr.push_back(path);
    return arr;
}

nlohmann::json saveUnreadNotices(const std::unordered_set<std::string>& notices)
{
    nlohmann::json arr = nlohmann::json::array();
    for (const auto& key : notices)
        arr.push_back(key);
    return arr;
}

nlohmann::json saveNpcState(const PlayerProfile& c)
{
    nlohmann::json npcs = nlohmann::json::object();
    for (const auto& [npc_id, state] : c.npc_state)
    {
        nlohmann::json rec = nlohmann::json::object();
        rec["times_talked"] = state.times_talked;
        if (!state.topics_seen.empty())
        {
            nlohmann::json arr = nlohmann::json::array();
            for (const auto& t : state.topics_seen)
                arr.push_back(t);
            rec["topics_seen"] = std::move(arr);
        }
        npcs[npc_id] = std::move(rec);
    }
    return npcs;
}

// Serialize a string-keyed uint32 map (kill_counts / examine_counts).
json saveCountMap(const std::unordered_map<std::string, std::uint32_t>& src)
{
    json out = json::object();
    for (const auto& [key, count] : src)
        out[key] = count;
    return out;
}

// Serialize one workbench-node vector to a JSON array.
json saveWorkbenchVec(const std::vector<PlayerProfile::WorkbenchNode>& v)
{
    json arr = json::array();
    for (const auto& w : v)
    {
        json o = json::object();
        o["id"] = w.id;
        o["x"] = w.x;
        o["y"] = w.y;
        if (!w.linked_observations.empty())
            o["linked_observations"] = w.linked_observations;
        if (!w.reading_id.empty())
            o["reading_id"] = w.reading_id;
        arr.push_back(std::move(o));
    }
    return arr;
}

// Write the optional insight-system fields (counts, conclusions,
// growth counters, workbench nodes). Skip-emit-when-empty keeps
// save files compact.
void saveInsightFields(nlohmann::json& dst, const PlayerProfile& c)
{
    if (!c.unlocked_insights.empty())
        dst["unlocked_insights"] = c.unlocked_insights;
    if (!c.kill_counts.empty())
        dst["kill_counts"] = saveCountMap(c.kill_counts);
    if (!c.certain_conclusions.empty())
        dst["certain_conclusions"] = c.certain_conclusions;
    if (!c.examine_counts.empty())
        dst["examine_counts"] = saveCountMap(c.examine_counts);
    if (c.perception_growth > 0)
        dst["perception_growth"] = c.perception_growth;
    if (c.cognition_growth > 0)
        dst["cognition_growth"] = c.cognition_growth;
    if (c.intelligence_growth > 0)
        dst["intelligence_growth"] = c.intelligence_growth;
    if (!c.workbench_observations.empty())
        dst["workbench_observations"] = saveWorkbenchVec(c.workbench_observations);
    if (!c.workbench_inferences.empty())
        dst["workbench_inferences"] = saveWorkbenchVec(c.workbench_inferences);
}

// Write the optional sangue economy fields. New characters and
// full-reclamation states stay compact.
void saveSangueFields(nlohmann::json& dst, const PlayerProfile& c)
{
    if (c.sangue_lifetime != 0u)
        dst["sangue_lifetime"] = c.sangue_lifetime;
    if (c.sangue_vessel != 0u)
        dst["sangue_vessel"] = c.sangue_vessel;
    if (c.sangue_riversato != 0u)
        dst["sangue_riversato"] = c.sangue_riversato;
}

nlohmann::json saveCharacter(const PlayerProfile& c)
{
    nlohmann::json char_json = {
        {"name", c.name},
        {"pos_x", c.pos_x},
        {"pos_y", c.pos_y},
        {"pos_z", c.pos_z},
        {"yaw", c.yaw},
        {"has_saved_pose", c.has_saved_pose},
        {"current_region_id", c.current_region_id.empty() ? "surface" : c.current_region_id},
    };
    if (!c.felled_bosses.empty())
        char_json["felled_bosses"] = c.felled_bosses;
    if (!c.flags.empty())
        char_json["flags"] = c.flags;
    saveInsightFields(char_json, c);
    if (!c.door_states.empty())
        char_json["door_states"] = saveDoorStates(c);
    if (!c.active_gather_nodes.empty())
        char_json["active_gather_nodes"] = saveActiveGatherNodes(c);
    if (c.next_gather_node_id > 1)
        char_json["next_gather_node_id"] = c.next_gather_node_id;
    if (!c.gather_flows.empty())
        char_json["gather_flows"] = saveGatherFlows(c);
    if (!c.craft_counts.empty())
        char_json["craft_counts"] = saveCountMap(c.craft_counts);
    if (!c.known_recipes.empty())
        char_json["known_recipes"] = c.known_recipes;
    if (!c.quick_slot_assigned.empty())
        char_json["quick_slot_assigned"] = c.quick_slot_assigned;
    if (c.quick_slot_primed_index >= 0)
        char_json["quick_slot_primed_index"] = c.quick_slot_primed_index;
    if (!c.inventory.by_category.empty() || c.inventory.next_id > 1)
        char_json["inventory"] = saveInventory(c);
    if (!equipmentIsEmpty(c.equipment))
        char_json["equipment"] = saveEquipment(c.equipment);
    if (!c.compendium.discovered.empty())
        char_json["compendium"] = saveCompendium(c.compendium);
    if (!c.unread_notices.empty())
        char_json["unread_notices"] = saveUnreadNotices(c.unread_notices);
    if (!c.npc_state.empty())
        char_json["npc_state"] = saveNpcState(c);
    saveSangueFields(char_json, c);
    if (c.player_class != PlayerClass::None)
        char_json["player_class"] = playerClassName(c.player_class);
    if (!c.appearance_path.empty())
        char_json["appearance_path"] = c.appearance_path;
    return char_json;
}

bool ensureSaveDir(const std::string& resolved)
{
    try
    {
        const std::filesystem::path dir = std::filesystem::path(resolved).parent_path();
        if (!dir.empty())
            std::filesystem::create_directories(dir);
    }
    catch (const std::exception& e)
    {
        std::fprintf(stderr, "[SaveManager] Cannot create directory for %s: %s\n", resolved.c_str(),
                     e.what());
        return false;
    }
    return true;
}
} // namespace

bool save(const SaveData& data, const std::string& path)
{
    const std::string resolved = path.empty() ? defaultSavePath() : path;
    if (!ensureSaveDir(resolved))
        return false;
    json j;
    j["schema_version"] = data.schema_version;
    j["characters"] = json::array();
    for (const auto& c : data.characters)
        j["characters"].push_back(saveCharacter(c));
    j["settings"] = {
        {"bgm_volume", data.settings.bgm_volume},
        {"sfx_volume", data.settings.sfx_volume},
        {"fov_degrees_third_person", data.settings.fov_degrees_third_person},
        {"fov_degrees_first_person", data.settings.fov_degrees_first_person},
        {"show_interact_ring", data.settings.show_interact_ring},
        {"auto_assign_consumables_to_quick_slot",
         data.settings.auto_assign_consumables_to_quick_slot},
    };
    if (data.has_last_played)
    {
        j["last_played_character"] = data.last_played_character;
        j["has_last_played"] = true;
    }
    std::ofstream file(resolved);
    if (!file.is_open())
    {
        std::fprintf(stderr, "[SaveManager] Cannot write %s\n", resolved.c_str());
        return false;
    }
    file << j.dump(4);
    std::fprintf(stderr, "[SaveManager] Saved to %s\n", resolved.c_str());
    return true;
}

void addCharacter(SaveData& data, const std::string& name)
{
    PlayerProfile p;
    p.name = name;
    // Default appearance for newly-created characters. The forthcoming
    // character designer overwrites this with a per-character variant;
    // until then every new pilgrim shares the default body. Existing
    // saves with empty appearance_path are backfilled by migrate() to
    // the same default.
    p.appearance_path = "config/appearances/default_humanoid.json";
    data.characters.push_back(std::move(p));
}

void deleteCharacter(SaveData& data, const std::string& name)
{
    auto& chars = data.characters;
    chars.erase(std::remove_if(chars.begin(), chars.end(),
                               [&](const PlayerProfile& p) { return p.name == name; }),
                chars.end());
}

void migrate(SaveData& data)
{
    // v4 -> v5: gather-node persistence added. v4 saves have no
    // active_gather_nodes / gather_flows fields. Defaults are correct
    // (empty list + next_id=1 + empty flow timers) -- the GatherSpawner's
    // first tick will see an empty population, run initial fill, and
    // commit fresh state to the next save write.
    //
    // v5 -> v6: quick_slot_assigned + quick_slot_primed_index added.
    // v5 saves have no quick_slot fields; defaults are empty rotation
    // + primed_index=-1. Q-press is a no-op until the player assigns
    // a consumable (or auto-assign is enabled in settings and they
    // craft/pick one up).
    //
    // v6 -> v7: shade_path field added.
    // v7 -> v8: renamed shade_path -> appearance_path; config dir
    // renamed config/shades -> config/appearances. (Reason: the
    // parameter set is universal across cosmologies -- burdened
    // shades, the Unburdened Vagrant, the Guide, divine emissaries
    // all use the same body schema. "Shade" was overloaded to mean
    // both a kind of being AND the visual config; "appearance"
    // separates them.)
    //
    // The character JSON loader above accepts either key during this
    // window: appearance_path wins; falls back to shade_path. Here we
    // also rewrite any legacy path string (config/shades/X.json ->
    // config/appearances/X.json) so the next save persists the
    // canonical form. Finally we backfill the default for any
    // character still empty -- without this, characters created on
    // v6 or earlier silently lock at body_scale=1.0 even after JSON
    // edits.
    for (auto& c : data.characters)
    {
        const std::string kLegacyPrefix = "config/shades/";
        const std::string kNewPrefix = "config/appearances/";
        if (c.appearance_path.rfind(kLegacyPrefix, 0) == 0)
            c.appearance_path = kNewPrefix + c.appearance_path.substr(kLegacyPrefix.size());
        if (c.appearance_path.empty())
            c.appearance_path = "config/appearances/default_humanoid.json";
    }

    // v8 -> v9: ItemInstance.size added (Small/Normal/Large). v8
    // saves have no "size" key in inventory entries; the load helper
    // defaults to Normal so legacy items keep their pre-size visible
    // behavior. No explicit per-instance backfill needed -- the
    // default IS the migration.

    data.schema_version = SaveData::CURRENT_VERSION;
}

} // namespace selva::SaveManager
