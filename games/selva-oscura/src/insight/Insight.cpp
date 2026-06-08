#include "insight/Insight.h"

#include "AppStateGlobal.h"

#include <nlohmann/json.hpp>

#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <string>
#include <unordered_map>
#include <vector>

namespace selva::insight
{

namespace
{

enum class TriggerKind : std::uint8_t
{
    FlagSet,
    DialogBegan,
    Examined,
    KillCount,
    SangueAccumulated,
};

// One node = one trigger. Triggers are described in JSON; this struct
// holds the parsed form. Only the fields relevant to the kind are
// populated; the rest stay default. v1 keeps the union flat (no
// std::variant) -- five kinds, two string fields and one threshold
// covers all of them.
struct Node
{
    std::string node_id;
    TriggerKind kind = TriggerKind::FlagSet;
    std::string string_arg;           // flag name / npc id / mesh name / archetype id
    std::uint32_t threshold_arg = 0u; // kill_count threshold / sangue threshold
};

std::vector<Node>& sNodes()
{
    static std::vector<Node> v;
    return v;
}

bool parseTriggerKind(const std::string& s, TriggerKind& out)
{
    if (s == "flag_set")
    {
        out = TriggerKind::FlagSet;
        return true;
    }
    if (s == "dialog_began")
    {
        out = TriggerKind::DialogBegan;
        return true;
    }
    if (s == "examined")
    {
        out = TriggerKind::Examined;
        return true;
    }
    if (s == "kill_count")
    {
        out = TriggerKind::KillCount;
        return true;
    }
    if (s == "sangue_accumulated")
    {
        out = TriggerKind::SangueAccumulated;
        return true;
    }
    return false;
}

// Convention for synthesized flag names used by event hooks. These
// land in PlayerProfile.flags via setFlag(); the FlagSet trigger
// kind reads them. Keeping the namespace ("examined:", "talked:")
// distinct from gameplay flags ("lupa_felled") prevents collisions.
std::string examinedFlag(const std::string& mesh_debug_name)
{
    return "examined:" + mesh_debug_name;
}
std::string talkedFlag(const std::string& npc_id)
{
    return "talked:" + npc_id;
}

bool loadOneFile(const std::filesystem::path& path)
{
    std::ifstream in(path);
    if (!in.is_open())
    {
        std::fprintf(stderr, "[insight] cannot open '%s'\n", path.string().c_str());
        return false;
    }
    nlohmann::json j;
    try
    {
        in >> j;
    }
    catch (const std::exception& e)
    {
        std::fprintf(stderr, "[insight] '%s' parse error: %s\n", path.string().c_str(), e.what());
        return false;
    }
    if (!j.is_object())
    {
        std::fprintf(stderr, "[insight] '%s' top-level must be an object\n", path.string().c_str());
        return false;
    }
    int loaded = 0;
    for (auto it = j.begin(); it != j.end(); ++it)
    {
        const std::string& node_id = it.key();
        if (node_id.empty() || node_id[0] == '_')
            continue;
        const auto& v = it.value();
        if (!v.is_object() || !v.contains("trigger") || !v["trigger"].is_object())
        {
            std::fprintf(stderr, "[insight] '%s' node '%s' missing 'trigger' object\n",
                         path.string().c_str(), node_id.c_str());
            continue;
        }
        const auto& t = v["trigger"];
        if (!t.contains("kind") || !t["kind"].is_string())
        {
            std::fprintf(stderr, "[insight] '%s' node '%s' trigger missing 'kind'\n",
                         path.string().c_str(), node_id.c_str());
            continue;
        }
        Node n;
        n.node_id = node_id;
        if (!parseTriggerKind(t["kind"].get<std::string>(), n.kind))
        {
            std::fprintf(stderr, "[insight] '%s' node '%s' unknown trigger kind '%s'\n",
                         path.string().c_str(), node_id.c_str(),
                         t["kind"].get<std::string>().c_str());
            continue;
        }
        // Per-kind required fields. Schema mismatch = skip with a
        // loud log so the missing field surfaces in dev.
        switch (n.kind)
        {
        case TriggerKind::FlagSet:
            if (!t.contains("flag") || !t["flag"].is_string())
            {
                std::fprintf(stderr,
                             "[insight] '%s' node '%s' flag_set trigger missing 'flag' string\n",
                             path.string().c_str(), node_id.c_str());
                continue;
            }
            n.string_arg = t["flag"].get<std::string>();
            break;
        case TriggerKind::DialogBegan:
            if (!t.contains("npc_id") || !t["npc_id"].is_string())
            {
                std::fprintf(stderr,
                             "[insight] '%s' node '%s' dialog_began trigger missing 'npc_id'\n",
                             path.string().c_str(), node_id.c_str());
                continue;
            }
            n.string_arg = t["npc_id"].get<std::string>();
            break;
        case TriggerKind::Examined:
            // Accepts either 'subject' (preferred, neutral term covering
            // both static-mesh debug_names AND actor archetype ids) or
            // legacy 'mesh_debug_name'. The bookkeeping flag namespace
            // is unified ('examined:<subject>'); authors must avoid id
            // collisions across actor archetypes + mesh debug names.
            if (t.contains("subject") && t["subject"].is_string())
                n.string_arg = t["subject"].get<std::string>();
            else if (t.contains("mesh_debug_name") && t["mesh_debug_name"].is_string())
                n.string_arg = t["mesh_debug_name"].get<std::string>();
            else
            {
                std::fprintf(stderr,
                             "[insight] '%s' node '%s' examined trigger missing 'subject' string\n",
                             path.string().c_str(), node_id.c_str());
                continue;
            }
            break;
        case TriggerKind::KillCount:
            if (!t.contains("archetype") || !t["archetype"].is_string() ||
                !t.contains("threshold") || !t["threshold"].is_number_unsigned())
            {
                std::fprintf(
                    stderr,
                    "[insight] '%s' node '%s' kill_count trigger missing 'archetype'/'threshold'\n",
                    path.string().c_str(), node_id.c_str());
                continue;
            }
            n.string_arg = t["archetype"].get<std::string>();
            n.threshold_arg = t["threshold"].get<std::uint32_t>();
            break;
        case TriggerKind::SangueAccumulated:
            if (!t.contains("threshold") || !t["threshold"].is_number_unsigned())
            {
                std::fprintf(stderr,
                             "[insight] '%s' node '%s' sangue_accumulated trigger missing "
                             "'threshold'\n",
                             path.string().c_str(), node_id.c_str());
                continue;
            }
            n.threshold_arg = t["threshold"].get<std::uint32_t>();
            break;
        }
        sNodes().push_back(std::move(n));
        ++loaded;
    }
    std::fprintf(stderr, "[insight] loaded %d nodes from %s\n", loaded, path.string().c_str());
    return true;
}

bool evaluate(const Node& n, const PlayerProfile* p)
{
    if (p == nullptr)
        return false;
    switch (n.kind)
    {
    case TriggerKind::FlagSet:
        return selva::hasFlag(p, n.string_arg);
    case TriggerKind::DialogBegan:
        return selva::hasFlag(p, talkedFlag(n.string_arg));
    case TriggerKind::Examined:
        return selva::hasFlag(p, examinedFlag(n.string_arg));
    case TriggerKind::KillCount:
    {
        auto it = p->kill_counts.find(n.string_arg);
        if (it == p->kill_counts.end())
            return false;
        return it->second >= n.threshold_arg;
    }
    case TriggerKind::SangueAccumulated:
        return p->sangue_lifetime >= n.threshold_arg;
    }
    return false;
}

} // namespace

void loadDirectory(const std::string& dir_path)
{
    sNodes().clear();
    std::error_code ec;
    if (!std::filesystem::exists(dir_path, ec) || !std::filesystem::is_directory(dir_path, ec))
    {
        std::fprintf(stderr, "[insight] directory '%s' does not exist; no nodes loaded\n",
                     dir_path.c_str());
        return;
    }
    for (const auto& entry : std::filesystem::directory_iterator(dir_path, ec))
    {
        if (!entry.is_regular_file())
            continue;
        if (entry.path().extension() != ".json")
            continue;
        loadOneFile(entry.path());
    }
    std::fprintf(stderr, "[insight] total nodes loaded: %zu\n", sNodes().size());
}

void tick()
{
    PlayerProfile* p = selva::activePlayerProfile();
    if (p == nullptr)
        return;
    for (const auto& n : sNodes())
    {
        if (selva::hasInsight(p, n.node_id))
            continue;
        if (evaluate(n, p))
        {
            selva::setInsight(p, n.node_id);
            std::fprintf(stderr, "[insight] node fired: '%s'\n", n.node_id.c_str());
            std::fflush(stderr);
        }
    }
}

void notifyDialogBegan(const std::string& npc_id)
{
    if (npc_id.empty())
        return;
    // Bookkeeping flag the DialogBegan trigger reads. Use the flag
    // helper so it persists in the profile (across save/load) without
    // a separate storage path.
    selva::setFlag(talkedFlag(npc_id));
}

void notifyExamined(const std::string& mesh_debug_name)
{
    if (mesh_debug_name.empty())
        return;
    selva::setFlag(examinedFlag(mesh_debug_name));
}

void notifyKill(const std::string& archetype_id)
{
    if (archetype_id.empty())
        return;
    PlayerProfile* p = selva::activePlayerProfile();
    if (p == nullptr)
        return;
    p->kill_counts[archetype_id] += 1u;
}

void resetGraph()
{
    sNodes().clear();
}

} // namespace selva::insight
