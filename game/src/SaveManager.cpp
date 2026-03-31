#include "SaveManager.h"

#include <algorithm>
#include <cmath>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <nlohmann/json.hpp>

using json = nlohmann::json;

namespace SaveManager
{

// ---------------------------------------------------------------------------
// JSON serialization helpers
// ---------------------------------------------------------------------------

static json runStatsToJson(const RunStats& s)
{
    return {{"kills", s.kills},         {"time", s.time},   {"wave", s.wave},
            {"xp_earned", s.xp_earned}, {"money", s.money}, {"score", s.score}};
}

static RunStats runStatsFromJson(const json& j)
{
    RunStats s;
    s.kills = j.value("kills", 0);
    s.time = j.value("time", 0.0f);
    s.wave = j.value("wave", 0);
    s.xp_earned = j.value("xp_earned", 0);
    s.money = j.value("money", 0);
    s.score = j.value("score", 0);
    return s;
}

static json runToJson(const Run& r)
{
    return {{"stats", runStatsToJson(r.stats)},
            {"character_name", r.character_name},
            {"timestamp", r.timestamp},
            {"escaped", r.escaped}};
}

static Run runFromJson(const json& j)
{
    Run r;
    if (j.contains("stats"))
        r.stats = runStatsFromJson(j["stats"]);
    r.character_name = j.value("character_name", std::string{});
    r.timestamp = j.value("timestamp", std::string{});
    r.escaped = j.value("escaped", false);
    return r;
}

// ---------------------------------------------------------------------------
// Public API
// ---------------------------------------------------------------------------

SaveData load(const std::string& path)
{
    SaveData data;

    std::ifstream file(path);
    if (!file.is_open())
        return data;

    json j;
    try
    {
        file >> j;
    }
    catch (const std::exception& e)
    {
        std::cerr << "[SaveManager] Error parsing " << path << ": " << e.what()
                  << " -- using defaults\n";
        return data;
    }

    data.schema_version = j.value("schema_version", 1);
    migrate(data);

    if (j.contains("characters") && j["characters"].is_array())
    {
        for (const auto& c : j["characters"])
        {
            PlayerProfile p;
            p.name = c.value("name", std::string{});
            p.money = c.value("money", 0);
            if (!p.name.empty())
                data.characters.push_back(std::move(p));
        }
    }

    if (j.contains("runs") && j["runs"].is_array())
    {
        for (const auto& r : j["runs"])
            data.runs.push_back(runFromJson(r));
    }

    // Migrate old global money into first character if present.
    if (j.contains("money") && !data.characters.empty())
        data.characters[0].money += j.value("money", 0);

    std::cout << "[SaveManager] Loaded " << data.characters.size() << " characters, "
              << data.runs.size() << " runs from " << path << "\n";
    return data;
}

bool save(const SaveData& data, const std::string& path)
{
    try
    {
        std::filesystem::path dir = std::filesystem::path(path).parent_path();
        if (!dir.empty())
            std::filesystem::create_directories(dir);
    }
    catch (const std::exception& e)
    {
        std::cerr << "[SaveManager] Cannot create directory for " << path << ": " << e.what()
                  << "\n";
        return false;
    }

    json j;
    j["schema_version"] = data.schema_version;

    j["characters"] = json::array();
    for (const auto& c : data.characters)
        j["characters"].push_back({{"name", c.name}, {"money", c.money}});

    j["runs"] = json::array();
    for (const auto& r : data.runs)
        j["runs"].push_back(runToJson(r));

    std::ofstream file(path);
    if (!file.is_open())
    {
        std::cerr << "[SaveManager] Cannot write " << path << "\n";
        return false;
    }

    file << j.dump(4);
    std::cout << "[SaveManager] Saved to " << path << "\n";
    return true;
}

void addCharacter(SaveData& data, const std::string& name)
{
    PlayerProfile p;
    p.name = name;
    data.characters.push_back(std::move(p));
}

void deleteCharacter(SaveData& data, const std::string& name)
{
    auto& chars = data.characters;
    chars.erase(std::remove_if(chars.begin(), chars.end(),
                               [&](const PlayerProfile& p) { return p.name == name; }),
                chars.end());

    auto& runs = data.runs;
    runs.erase(std::remove_if(runs.begin(), runs.end(),
                              [&](const Run& r) { return r.character_name == name; }),
               runs.end());
}

void recordRun(SaveData& data, const Run& run)
{
    data.runs.push_back(run);
}

std::vector<Run> topRuns(const SaveData& data, int count)
{
    std::vector<Run> sorted = data.runs;
    std::sort(sorted.begin(), sorted.end(),
              [](const Run& a, const Run& b) { return a.stats.score > b.stats.score; });
    if (static_cast<int>(sorted.size()) > count)
        sorted.resize(static_cast<std::size_t>(count));
    return sorted;
}

int computeScore(const RunStats& stats, const ScoringConfig& cfg, bool escaped)
{
    float score = 0.0f;
    score += static_cast<float>(stats.kills) * cfg.kill_weight;
    // Count completed waves (current wave is in-progress, not yet cleared).
    const int completedWaves = std::max(0, stats.wave - 1);
    score += static_cast<float>(completedWaves) * cfg.wave_weight;
    score += static_cast<float>(stats.xp_earned) * cfg.xp_weight;
    score += static_cast<float>(stats.money) * cfg.money_weight;
    if (score < 0.0f)
        score = 0.0f;
    if (escaped)
        score *= cfg.escape_multiplier;
    return static_cast<int>(score);
}

void migrate(SaveData& data)
{
    // v1 is current -- no migrations needed yet.
    // Future: add migration steps here (v1->v2, v2->v3, etc.)
    data.schema_version = SaveData::CURRENT_VERSION;
}

} // namespace SaveManager
