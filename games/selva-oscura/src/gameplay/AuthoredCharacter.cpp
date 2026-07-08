#include "gameplay/AuthoredCharacter.h"

#include <nlohmann/json.hpp>

#include <cstdio>
#include <filesystem>
#include <fstream>

namespace selva::gameplay
{

namespace
{

void readStatsFromJson(const nlohmann::json& j, Stats& out)
{
    if (!j.is_object())
        return;
    if (j.contains("str") && j["str"].is_number())
        out.str = j["str"].get<int>();
    if (j.contains("dex") && j["dex"].is_number())
        out.dex = j["dex"].get<int>();
    if (j.contains("end") && j["end"].is_number())
        out.end = j["end"].get<int>();
    if (j.contains("lck") && j["lck"].is_number())
        out.lck = j["lck"].get<int>();
    if (j.contains("per") && j["per"].is_number())
        out.per = j["per"].get<int>();
    if (j.contains("cog") && j["cog"].is_number())
        out.cog = j["cog"].get<int>();
    if (j.contains("intl") && j["intl"].is_number())
        out.intl = j["intl"].get<int>();
}

nlohmann::json statsToJson(const Stats& s)
{
    nlohmann::json j = nlohmann::json::object();
    j["str"] = s.str;
    j["dex"] = s.dex;
    j["end"] = s.end;
    j["lck"] = s.lck;
    j["per"] = s.per;
    j["cog"] = s.cog;
    j["intl"] = s.intl;
    return j;
}

} // namespace

AuthoredCharacter loadAuthoredCharacter(const std::string& path)
{
    AuthoredCharacter out;
    if (path.empty())
        return out;
    if (!std::filesystem::exists(path))
    {
        std::fprintf(stderr, "[authored-char] file not found: %s (using defaults)\n", path.c_str());
        std::fflush(stderr);
        return out;
    }
    std::ifstream f(path);
    if (!f.is_open())
    {
        std::fprintf(stderr, "[authored-char] failed to open: %s\n", path.c_str());
        std::fflush(stderr);
        return out;
    }
    nlohmann::json j;
    try
    {
        f >> j;
    }
    catch (const std::exception& e)
    {
        std::fprintf(stderr, "[authored-char] parse error in %s: %s\n", path.c_str(), e.what());
        std::fflush(stderr);
        return out;
    }
    // Appearance keys share the top-level namespace with identity
    // keys. Pre-existing pure-Appearance files (foundling.json,
    // keeper.json, ...) load cleanly -- the identity readers just
    // don't find their fields and leave defaults in place.
    readAppearanceFromJson(j, out.appearance, path);
    if (j.contains("display_name_key") && j["display_name_key"].is_string())
    {
        out.display_name_key = j["display_name_key"].get<std::string>();
        out.has_display_name_key = true;
    }
    if (j.contains("player_class") && j["player_class"].is_string())
    {
        out.player_class = parsePlayerClass(j["player_class"].get<std::string>());
        out.has_player_class = true;
    }
    if (j.contains("stats"))
    {
        readStatsFromJson(j["stats"], out.stats);
        out.has_stats = true;
    }
    if (j.contains("rh_item") && j["rh_item"].is_string())
    {
        out.rh_item = j["rh_item"].get<std::string>();
        out.has_rh_item = true;
    }
    if (j.contains("lh_item") && j["lh_item"].is_string())
    {
        out.lh_item = j["lh_item"].get<std::string>();
        out.has_lh_item = true;
    }
    return out;
}

bool saveAuthoredCharacter(const std::string& path, const AuthoredCharacter& character)
{
    if (path.empty())
    {
        std::fprintf(stderr, "[authored-char] save called with empty path\n");
        std::fflush(stderr);
        return false;
    }
    nlohmann::json j;
    writeAppearanceToJson(character.appearance, j);
    // Emit identity keys ONLY when the author explicitly opted in
    // (via has_* flag). Files with no identity keys behave as
    // "appearance-only" at load time -- spawn sites leave the
    // Actor's stats/class/equipment at archetype defaults. This is
    // load-bearing: the presence of a key IS the opt-in signal.
    if (character.has_display_name_key)
        j["display_name_key"] = character.display_name_key;
    if (character.has_player_class)
        j["player_class"] = playerClassName(character.player_class);
    if (character.has_stats)
        j["stats"] = statsToJson(character.stats);
    if (character.has_rh_item)
        j["rh_item"] = character.rh_item;
    if (character.has_lh_item)
        j["lh_item"] = character.lh_item;
    std::ofstream f(path);
    if (!f.is_open())
    {
        std::fprintf(stderr, "[authored-char] failed to open for write: %s\n", path.c_str());
        std::fflush(stderr);
        return false;
    }
    f << j.dump(2) << '\n';
    return true;
}

} // namespace selva::gameplay
