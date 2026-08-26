#include "formats/HoleKinds.h"

#include "formats/FloorTypes.h"
#include "ops/LogUtils.h"

#include <nlohmann/json.hpp>

#include <fstream>

namespace holes
{

Kind load(const std::string& path)
{
    Kind kind;
    kind.path = path;
    std::ifstream sf(path);
    const nlohmann::json sj =
        sf ? nlohmann::json::parse(sf, nullptr, /*allow_exceptions=*/false) : nlohmann::json{};
    if (!sj.is_discarded() && sj.is_object())
    {
        kind.def = sprite_def::load(sj.value("sprite", std::string{}));
        kind.on_wall = sj.value("placement", std::string{"floor"}) == "wall";
        kind.opens = sj.value("opens", std::string{});
        const auto& pests = sj.value("pests", nlohmann::json::array());
        if (!pests.empty())
            kind.first_pest = pests.front().value("pest", std::string{});
    }
    return kind;
}

// WHERE A KIND OF HOLE SITS AND WHAT IT OPENS. Cached by path: which way a hole goes is asked
// every frame by the prompt, and a per-frame question must not reopen a config file to answer.
const Kind& facts(const std::string& path)
{
    static std::unordered_map<std::string, Kind> cache;
    const auto known = cache.find(path);
    return known != cache.end() ? known->second : cache.emplace(path, load(path)).first->second;
}

bool onWall(const std::string& path)
{
    return facts(path).on_wall;
}

// THE DESCENT'S DEFAULT SPACE, for a hole that names none: the house's own foundation.
const std::string& defaultType()
{
    static const std::string type = []
    {
        std::ifstream in("config/descent.json");
        const nlohmann::json j =
            in ? nlohmann::json::parse(in, nullptr, /*allow_exceptions=*/false) : nlohmann::json{};
        return j.is_discarded() || !j.is_object()
                   ? std::string{"config/rooms/cellar.json"}
                   : j.value("default", std::string{"config/rooms/cellar.json"});
    }();
    return type;
}

// WHICH KIND OF SPACE A FLOOR IS, with the fallback applied once here so nothing downstream has
// to remember that an empty string means the default.
const std::string& typeOf(const descent::Room& room)
{
    return room.type.empty() ? defaultType() : room.type;
}

// This kind of space's mix of hole kinds, plus any letter-pinned kinds. Read through the type's
// base chain, so a type that does not name a mix inherits the one it varies from.
Rules loadTable(const std::string& typePath, std::vector<Kind>& kinds,
                std::unordered_map<char, std::string>& pinned)
{
    Rules rules;
    const nlohmann::json j = formats::read(typePath);
    if (!j.is_object())
        return rules;
    for (const auto& entry : j.value("hole_types", nlohmann::json::array()))
    {
        Kind kind = load(entry.value("hole", std::string{}));
        kind.weight = entry.value("weight", 1);
        kinds.push_back(std::move(kind));
    }
    const nlohmann::json ms = j.value("marker_holes", nlohmann::json::object());
    for (const auto& [letter, path] : ms.items())
        if (!letter.empty() && path.is_string())
            pinned.emplace(letter.front(), path.get<std::string>());
    rules.rooms_per_floor = j.value("rooms_per_floor", rules.rooms_per_floor);
    rules.wall_depth = j.value("wall_depth", rules.wall_depth);
    return rules;
}

const Kind* byPath(std::vector<Kind>& kinds, const std::string& path)
{
    for (const auto& k : kinds)
        if (k.path == path)
            return &k;
    kinds.push_back(load(path));
    return &kinds.back();
}

} // namespace holes
