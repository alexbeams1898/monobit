#include "LdtkImport.h"

#include <nlohmann/json.hpp>

#include <fstream>
#include <unordered_map>

namespace ldtk
{
namespace
{
using nlohmann::json;

// A tileset source pixel (src:[sx,sy]) at the authoring grid size maps to an atlas
// CELL index; that index is identical in the x2 render atlas (x2 cancels). So the
// engine tile id is a stable cell index, and uv_col/uv_row = the same cell.
int cellId(int uv_col, int uv_row, int atlas_cols)
{
    return uv_row * atlas_cols + uv_col;
}

// Find a layer instance by its __IDENTIFIER (the layer's name: "Ground",
// "Overhang", "Entities"). NOT __type -- Ground and Overhang are both __type
// "Tiles", so only the identifier distinguishes them.
const json* findLayer(const json& level, const char* identifier)
{
    const auto li = level.find("layerInstances");
    if (li == level.end() || !li->is_array())
        return nullptr;
    for (const auto& layer : *li)
        if (layer.value("__identifier", std::string{}) == identifier)
            return &layer;
    return nullptr;
}
} // namespace

Region load(const std::string& ldtk_path, const std::string& tileset_path)
{
    Region r;
    std::ifstream f(ldtk_path);
    if (!f)
        return r;
    const json j = json::parse(f, nullptr, /*allow_exceptions=*/false);
    if (j.is_discarded())
        return r;

    // Single-level import for now (the first level); multi-level worlds come with
    // region connections (MAP-ARCHITECTURE §3).
    const auto levels = j.find("levels");
    if (levels == j.end() || !levels->is_array() || levels->empty())
        return r;
    const json& level = (*levels)[0];

    // Authoring grid size (16) -> world grid is x2 (32). Atlas columns come from
    // the tileset def so a src pixel resolves to a cell index.
    int atlas_cols = 40; // Overworld default; overwritten from the tileset def
    if (const auto ts = j.find("defs"); ts != j.end())
        if (const auto arr = ts->find("tilesets"); arr != ts->end() && arr->is_array())
            for (const auto& t : *arr)
                if (t.value("relPath", std::string{}).find("Overworld") != std::string::npos)
                    atlas_cols = t.value("__cWid", atlas_cols);

    // Ground = the terrain layer ("Ground", or the legacy single "Tiles" layer).
    const json* ground = findLayer(level, "Ground");
    if (!ground)
        ground = findLayer(level, "Tiles");
    if (!ground)
        return r;
    const int gridSize = ground->value("__gridSize", 16);
    const int cw = ground->value("__cWid", 0);
    const int ch = ground->value("__cHei", 0);
    if (cw <= 0 || ch <= 0)
        return r;

    r.map.tile_size = gridSize * 2; // 16 -> 32 world grid
    r.map.width = cw;
    r.map.height = ch;

    // fill_tile (level field) -> the default/fill cell index.
    if (const auto fis = level.find("fieldInstances"); fis != level.end() && fis->is_array())
        for (const auto& fi : *fis)
            if (fi.value("__identifier", std::string{}) == "fill_tile")
                if (const auto v = fi.find("__value"); v != fi.end() && v->is_object())
                {
                    r.fill_uv_col = v->value("x", 0) / gridSize;
                    r.fill_uv_row = v->value("y", 0) / gridSize;
                }
    const int fillId = cellId(r.fill_uv_col, r.fill_uv_row, atlas_cols);

    std::unordered_map<int, std::pair<int, int>> uvById; // id -> (uv_col,uv_row)
    uvById[fillId] = {r.fill_uv_col, r.fill_uv_row};

    const auto cellIndex = [&](int col, int row)
    {
        return static_cast<std::size_t>(row) * static_cast<std::size_t>(cw) +
               static_cast<std::size_t>(col);
    };

    // Walk a layer's gridTiles, calling `place(cellIndex, id, isFirstAtCell)` for
    // each. LDtk stacks multiple tiles per cell (grass then a flower on top) as
    // separate gridTiles entries in paint order; the FIRST at a cell is the base,
    // later ones are stacked decoration. Records each tile's uv in uvById.
    const auto forEachTile = [&](const json* layer, auto&& place)
    {
        if (!layer)
            return;
        std::unordered_map<std::size_t, bool> seen; // cell -> already placed a base?
        if (const auto gt = layer->find("gridTiles"); gt != layer->end() && gt->is_array())
            for (const auto& t : *gt)
            {
                const auto px = t.find("px");
                const auto src = t.find("src");
                if (px == t.end() || src == t.end() || !px->is_array() || !src->is_array())
                    continue;
                const int col = (*px)[0].get<int>() / gridSize;
                const int row = (*px)[1].get<int>() / gridSize;
                if (col < 0 || row < 0 || col >= cw || row >= ch)
                    continue;
                const int uv_col = (*src)[0].get<int>() / gridSize;
                const int uv_row = (*src)[1].get<int>() / gridSize;
                const int id = cellId(uv_col, uv_row, atlas_cols);
                uvById[id] = {uv_col, uv_row};
                const std::size_t idx = cellIndex(col, row);
                const bool first = !seen[idx];
                seen[idx] = true;
                place(idx, id, first);
            }
    };

    const std::size_t total = static_cast<std::size_t>(cw) * static_cast<std::size_t>(ch);

    // GROUND: base tile per cell -> r.map.tiles (fill grass elsewhere). Any EXTRA
    // stacked tile on a ground cell (a flower over grass) -> decoration, drawn under
    // the player so he walks on top of it (grass still shows below).
    r.map.tiles.assign(total, TileMap::Tile{fillId, true});
    r.map.decoration.assign(total, TileMap::Tile{0, true});
    forEachTile(ground,
                [&](std::size_t idx, int id, bool first)
                {
                    if (first)
                        r.map.tiles[idx] = TileMap::Tile{id, true};
                    else
                        r.map.decoration[idx] = TileMap::Tile{id, true}; // topmost stacked wins
                });

    // OVERHANG (optional): sparse props drawn ABOVE characters (canopy tops).
    if (const json* over = findLayer(level, "Overhang"))
    {
        r.map.overhang.assign(total, TileMap::Tile{0, true});
        forEachTile(over, [&](std::size_t idx, int id, bool)
                    { r.map.overhang[idx] = TileMap::Tile{id, true}; });
    }

    // Fill the tile config: atlas + per-id uv. Walkable defaults true here; the
    // IntGrid behavior layer (later) is the real source of walkable per §1.2/§4.
    r.config = TileConfig{};
    r.config.tileset_path = tileset_path;
    r.config.atlas_tile_size = r.map.tile_size; // 32
    for (const auto& [id, uv] : uvById)
    {
        r.config.tiles[id] = {tileset_path, true};
        TileConfig::TileVisual vis;
        vis.uv_col = uv.first;
        vis.uv_row = uv.second;
        r.config.tile_visuals[id] = vis;
    }

    // Entities -> objects, at WORLD pixels (LDtk px is authoring-grid px -> x2).
    if (const json* ents = findLayer(level, "Entities"))
        if (const auto ei = ents->find("entityInstances"); ei != ents->end() && ei->is_array())
            for (const auto& e : *ei)
            {
                const auto px = e.find("px");
                if (px == e.end() || !px->is_array())
                    continue;
                Object o;
                o.type = e.value("__identifier", std::string{});
                o.wx = static_cast<float>((*px)[0].get<int>() * 2);
                o.wy = static_cast<float>((*px)[1].get<int>() * 2);
                r.objects.push_back(o);
            }

    r.ok = true;
    return r;
}

} // namespace ldtk
