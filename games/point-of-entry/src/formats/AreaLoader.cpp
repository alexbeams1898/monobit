#include "formats/AreaLoader.h"

#include "ecs/EntityManager.h"
#include "ops/LogUtils.h"

#include <cctype>
#include <filesystem>
#include <fstream>
#include <unordered_map>
#include <unordered_set>

namespace area
{
namespace
{

std::unordered_map<std::string, Builder>& registry()
{
    static std::unordered_map<std::string, Builder> sBuilders;
    return sBuilders;
}

// PascalCase entity identifier -> snake_case builder key (Door -> door,
// PlayerStart -> player_start). One convention, applied mechanically.
std::string toSnake(const std::string& pascal)
{
    std::string out;
    for (const char c : pascal)
    {
        if (std::isupper(static_cast<unsigned char>(c)) != 0)
        {
            if (!out.empty())
                out.push_back('_');
            out.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(c))));
        }
        else
            out.push_back(c);
    }
    return out;
}

nlohmann::json parseFile(const std::string& path)
{
    std::ifstream in(path);
    if (!in)
    {
        poe::log().error("area: no project at '{}'", path);
        return nlohmann::json{nullptr};
    }
    nlohmann::json j = nlohmann::json::parse(in, nullptr, /*allow_exceptions=*/false);
    if (j.is_discarded())
        poe::log().error("area: '{}' is not valid JSON", path);
    return j;
}

const nlohmann::json* findLayer(const nlohmann::json& level, const char* identifier)
{
    const auto layers = level.find("layerInstances");
    if (layers == level.end() || !layers->is_array())
        return nullptr;
    for (const auto& layer : *layers)
        if (layer.value("__identifier", std::string{}) == identifier)
            return &layer;
    return nullptr;
}

int cellId(int uvCol, int uvRow, int atlasCols)
{
    return uvRow * atlasCols + uvCol;
}

// The tileset the project paints with: its atlas path, column count, and the
// set of cells tagged Solid. One tileset for the whole authored world.
struct Tileset
{
    std::string path;
    int cols = 1;
    std::unordered_set<int> solid;
    bool ok = false;
};

Tileset parseTileset(const nlohmann::json& j, const std::string& ldtkPath)
{
    Tileset ts;
    const auto defs = j.find("defs");
    if (defs == j.end())
        return ts;
    const auto sets = defs->find("tilesets");
    if (sets == defs->end() || !sets->is_array() || sets->empty())
        return ts;
    const auto& def = sets->front();
    // The .ldtk stores the image path relative to the project file; resolve
    // against where the project actually sits, normalized to the game root
    // the runtime runs from.
    const std::filesystem::path rel = def.value("relPath", std::string{});
    ts.path =
        (std::filesystem::path(ldtkPath).parent_path() / rel).lexically_normal().generic_string();
    ts.cols = std::max(1, def.value("__cWid", 1));
    for (const auto& tag : def.value("enumTags", nlohmann::json::array()))
        if (tag.value("enumValueId", std::string{}) == "Solid")
            for (const auto& id : tag.value("tileIds", nlohmann::json::array()))
                ts.solid.insert(id.get<int>());
    ts.ok = true;
    return ts;
}

const nlohmann::json* chooseLevel(const nlohmann::json& j, const std::string& level,
                                  const std::string& path)
{
    const auto levels = j.find("levels");
    if (levels == j.end() || !levels->is_array() || levels->empty())
    {
        poe::log().error("area: '{}' has no levels", path);
        return nullptr;
    }
    if (level.empty())
        return &levels->front();
    for (const auto& l : *levels)
        if (l.value("identifier", std::string{}) == level)
            return &l;
    poe::log().error("area: '{}' has no level '{}'", path, level);
    return nullptr;
}

void parseGround(const nlohmann::json& ground, const Tileset& ts, Data& d)
{
    const int gs = ground.value("__gridSize", 16);
    // Authoring happens at the art's native 16px; the world runs at twice
    // that. The x2 lives here and nowhere else.
    d.tile_size = gs * 2;
    d.width = ground.value("__cWid", 0);
    d.height = ground.value("__cHei", 0);
    const auto total = static_cast<std::size_t>(d.width) * static_cast<std::size_t>(d.height);
    // Unpainted cells are void: solid, drawn as the clear colour.
    d.tiles.assign(total, TileMap::Tile{-1, false});

    d.config = TileConfig{};
    d.config.tileset_path = ts.path;
    d.config.atlas_tile_size = gs; // the atlas stays at authoring scale

    std::unordered_set<std::size_t> seen;
    for (const auto& t : ground.value("gridTiles", nlohmann::json::array()))
    {
        const auto px = t.find("px");
        const auto src = t.find("src");
        if (px == t.end() || src == t.end() || !px->is_array() || !src->is_array())
            continue;
        const int col = (*px)[0].get<int>() / gs;
        const int row = (*px)[1].get<int>() / gs;
        if (col < 0 || row < 0 || col >= d.width || row >= d.height)
            continue;
        const int uvCol = (*src)[0].get<int>() / gs;
        const int uvRow = (*src)[1].get<int>() / gs;
        const int id = cellId(uvCol, uvRow, ts.cols);
        const bool walk = ts.solid.count(id) == 0;
        d.config.tiles[id] = {ts.path, walk};
        TileConfig::TileVisual vis;
        vis.uv_col = uvCol;
        vis.uv_row = uvRow;
        d.config.tile_visuals[id] = vis;

        const std::size_t idx = static_cast<std::size_t>(row) * static_cast<std::size_t>(d.width) +
                                static_cast<std::size_t>(col);
        if (seen.insert(idx).second)
            d.tiles[idx] = TileMap::Tile{id, walk};
        else
        {
            // Stacked paint keeps its order; a cell walks only if every layer
            // in it does.
            d.decoration.push_back({idx, TileMap::Tile{id, true}});
            if (!walk)
                d.tiles[idx].walkable = false;
        }
    }
}

void parseEntities(const nlohmann::json& level, Data& d)
{
    const nlohmann::json* layer = findLayer(level, "Entities");
    if (layer == nullptr)
        return;
    for (const auto& e : layer->value("entityInstances", nlohmann::json::array()))
    {
        Object o;
        o.type = toSnake(e.value("__identifier", std::string{}));
        const auto px = e.find("px");
        if (o.type.empty() || px == e.end() || !px->is_array())
            continue;
        // Entity coordinates are authoring px; the world is twice that. px is
        // the entity's top-left (the default pivot); objects live by centre.
        o.w = static_cast<float>(e.value("width", d.tile_size / 2)) * 2.0f;
        o.h = static_cast<float>(e.value("height", d.tile_size / 2)) * 2.0f;
        o.x = (*px)[0].get<float>() * 2.0f + o.w * 0.5f;
        o.y = (*px)[1].get<float>() * 2.0f + o.h * 0.5f;
        o.props = nlohmann::json::object();
        for (const auto& fi : e.value("fieldInstances", nlohmann::json::array()))
        {
            const std::string key = fi.value("__identifier", std::string{});
            // A field left empty in the editor arrives as null -- it does not
            // exist. Letting nulls through makes every consumer's
            // value(key, default) a live grenade (null is present, so the
            // default never applies and the type assert throws).
            if (!key.empty() && fi.contains("__value") && !fi.at("__value").is_null())
                o.props[key] = fi.at("__value");
        }
        d.objects.push_back(std::move(o));
    }
}

} // namespace

Data loadLevel(const std::string& ldtkPath, const std::string& level)
{
    Data d;
    const nlohmann::json j = parseFile(ldtkPath);
    if (!j.is_object())
        return d;
    const Tileset ts = parseTileset(j, ldtkPath);
    if (!ts.ok)
    {
        poe::log().error("area: '{}' declares no tileset", ldtkPath);
        return d;
    }
    const nlohmann::json* lvl = chooseLevel(j, level, ldtkPath);
    if (lvl == nullptr)
        return d;
    d.name = lvl->value("identifier", std::string{});

    const nlohmann::json* ground = findLayer(*lvl, "Ground");
    if (ground == nullptr)
    {
        poe::log().error("area: level '{}' has no Ground layer", d.name);
        return d;
    }
    parseGround(*ground, ts, d);
    if (d.width <= 0 || d.height <= 0)
    {
        poe::log().error("area: level '{}' has no ground", d.name);
        return d;
    }
    parseEntities(*lvl, d);
    d.ok = true;
    return d;
}

std::string startLevel(const std::string& ldtkPath)
{
    const nlohmann::json j = parseFile(ldtkPath);
    if (!j.is_object())
        return {};
    for (const auto& l : j.value("levels", nlohmann::json::array()))
    {
        const nlohmann::json* layer = findLayer(l, "Entities");
        if (layer == nullptr)
            continue;
        for (const auto& e : layer->value("entityInstances", nlohmann::json::array()))
            if (e.value("__identifier", std::string{}) == "PlayerStart")
                return l.value("identifier", std::string{});
    }
    return {};
}

std::vector<std::string> levels(const std::string& ldtkPath)
{
    std::vector<std::string> out;
    const nlohmann::json j = parseFile(ldtkPath);
    if (!j.is_object())
        return out;
    for (const auto& l : j.value("levels", nlohmann::json::array()))
    {
        const std::string id = l.value("identifier", std::string{});
        if (!id.empty())
            out.push_back(id);
    }
    return out;
}

void registerBuilder(const std::string& type, Builder fn)
{
    registry()[type] = std::move(fn);
}

bool build(EntityManager& em, const Data& d)
{
    if (!d.ok)
        return false;

    em.tile_map = TileMap{};
    em.tile_map.tile_size = d.tile_size;
    em.tile_map.width = d.width;
    em.tile_map.height = d.height;
    em.tile_map.tiles = d.tiles;
    em.tile_map.decoration = d.decoration;
    em.tile_config = d.config;

    for (const auto& o : d.objects)
    {
        const auto it = registry().find(o.type);
        if (it == registry().end())
        {
            poe::log().error("area: '{}' object type '{}' has no builder", d.name, o.type);
            continue;
        }
        it->second(em, o);
    }
    return true;
}

} // namespace area
