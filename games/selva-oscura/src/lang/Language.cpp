#include "lang/Language.h"

#include "AppStateGlobal.h"

#include <nlohmann/json.hpp>

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <string>
#include <unordered_map>

namespace selva::lang
{

namespace
{

// One language entry. tier_0 is REQUIRED; tier_1 / tier_2 are
// optional. unlock_node_tier_1 / unlock_node_tier_2 hold the
// insight-graph node ids that promote the entry to higher tiers; if
// empty, that tier cannot promote (stays at the lower authored tier
// even when other content of the same theme reveals).
struct Entry
{
    std::string tier_0;
    std::string tier_1;
    std::string tier_2;
    std::string unlock_node_tier_1;
    std::string unlock_node_tier_2;
};

std::unordered_map<std::string, Entry>& sMap()
{
    static std::unordered_map<std::string, Entry> m;
    return m;
}

// The fallback string returned for missing keys. Static so callers
// can keep a const-ref to it across calls. Updated by resolve() for
// the specific missing key just before return.
std::string& sMissingFallback()
{
    static std::string s;
    return s;
}

bool loadOneFile(const std::filesystem::path& path)
{
    std::ifstream in(path);
    if (!in.is_open())
    {
        std::fprintf(stderr, "[lang] cannot open '%s'\n", path.string().c_str());
        return false;
    }
    nlohmann::json j;
    try
    {
        in >> j;
    }
    catch (const std::exception& e)
    {
        std::fprintf(stderr, "[lang] '%s' parse error: %s\n", path.string().c_str(), e.what());
        return false;
    }
    if (!j.is_object())
    {
        std::fprintf(stderr, "[lang] '%s' top-level must be an object\n", path.string().c_str());
        return false;
    }
    auto& map = sMap();
    int loaded = 0;
    for (auto it = j.begin(); it != j.end(); ++it)
    {
        const std::string& key = it.key();
        if (key.empty() || key[0] == '_')
            continue; // skip _comment-style entries
        const auto& v = it.value();
        if (!v.is_object())
        {
            std::fprintf(stderr, "[lang] '%s' key '%s' must map to an object\n",
                         path.string().c_str(), key.c_str());
            continue;
        }
        if (!v.contains("tier_0") || !v["tier_0"].is_string())
        {
            std::fprintf(stderr, "[lang] '%s' key '%s' missing required 'tier_0' string\n",
                         path.string().c_str(), key.c_str());
            continue;
        }
        if (map.find(key) != map.end())
        {
            std::fprintf(stderr,
                         "[lang] '%s' key '%s' already loaded from another domain file -- "
                         "duplicate keys are an authoring error\n",
                         path.string().c_str(), key.c_str());
            continue;
        }
        Entry e;
        e.tier_0 = v["tier_0"].get<std::string>();
        if (v.contains("tier_1") && v["tier_1"].is_string())
            e.tier_1 = v["tier_1"].get<std::string>();
        if (v.contains("tier_2") && v["tier_2"].is_string())
            e.tier_2 = v["tier_2"].get<std::string>();
        if (v.contains("unlock_node_tier_1") && v["unlock_node_tier_1"].is_string())
            e.unlock_node_tier_1 = v["unlock_node_tier_1"].get<std::string>();
        if (v.contains("unlock_node_tier_2") && v["unlock_node_tier_2"].is_string())
            e.unlock_node_tier_2 = v["unlock_node_tier_2"].get<std::string>();
        map.emplace(key, std::move(e));
        ++loaded;
    }
    std::fprintf(stderr, "[lang] loaded %d entries from %s\n", loaded, path.string().c_str());
    return true;
}

} // namespace

void loadDirectory(const std::string& dir_path)
{
    sMap().clear();
    std::error_code ec;
    if (!std::filesystem::exists(dir_path, ec) || !std::filesystem::is_directory(dir_path, ec))
    {
        std::fprintf(stderr, "[lang] directory '%s' does not exist; no strings loaded\n",
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
    std::fprintf(stderr, "[lang] total entries loaded: %zu\n", sMap().size());
}

bool isUnlocked(const std::string& node_id)
{
    // Look up the active profile's unlocked_insights set. The set is
    // populated by selva::insight::tick() as triggers fire (see
    // src/insight/Insight.cpp). Empty node id or no active profile =
    // not unlocked, by definition.
    return selva::hasInsight(node_id);
}

const std::string& resolve(const std::string& key)
{
    const auto& map = sMap();
    const auto it = map.find(key);
    if (it == map.end())
    {
        sMissingFallback() = "[lang:" + key + "]";
        return sMissingFallback();
    }
    const Entry& e = it->second;
    // Walk tiers high -> low; first one with an authored string AND a
    // fired unlock node (or tier_0, which has no gate) wins.
    if (!e.tier_2.empty() && !e.unlock_node_tier_2.empty() && isUnlocked(e.unlock_node_tier_2))
        return e.tier_2;
    if (!e.tier_1.empty() && !e.unlock_node_tier_1.empty() && isUnlocked(e.unlock_node_tier_1))
        return e.tier_1;
    return e.tier_0;
}

void reset()
{
    sMap().clear();
    sMissingFallback().clear();
}

} // namespace selva::lang
