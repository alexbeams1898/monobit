#include "formats/RoomGen.h"

#include "ecs/EntityManager.h"
#include "formats/AreaLoader.h"
#include "formats/FloorTypes.h"
#include "ops/LogUtils.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <queue>
#include <random>
#include <sstream>

namespace roomgen
{
namespace
{
// A placed room's footprint in tile coordinates -- kept so later placements can
// check for overlap and so corridors know what to connect.
struct Placed
{
    int col = 0;
    int row = 0;
    const Chamber* chamber = nullptr;

    int centreCol() const
    {
        return col + chamber->width / 2;
    }
    int centreRow() const
    {
        return row + chamber->height / 2;
    }
};

// Row-major tile index, widened BEFORE the multiply so the arithmetic cannot overflow as int.
// Every tile lookup in this file goes through here rather than casting inline -- six copies of
// the same three-cast expression is how one of them ends up wrong.
inline std::size_t tileIndex(int row, int col, int width)
{
    return static_cast<std::size_t>(row) * static_cast<std::size_t>(width) +
           static_cast<std::size_t>(col);
}

// How the floor is laid out, from config/descent.json.
struct Config
{
    int width = 96; // the whole floor, in tiles
    int height = 72;
    int tile_size = 32;
    int chamber_attempts = 40; // placement tries; more rooms than this is unlikely
    // A CEILING, not a goal: placement stops once this many are down, but what decides the
    // count in practice is the box measured against the templates -- an overlapping room is
    // dropped rather than shuffled, so a floor runs out of legal spots long before it runs
    // out of attempts.
    int chamber_limit = 6;
    int corridor_half = 1; // corridors are (2*half + 1) tiles wide
    // Placement rules for marker letters that are spawners. The generator stays agnostic about
    // what a letter MEANS -- the config names which letters carry the rule.
    std::string spaced_types;
    float spaced_min_from_spawn = 6.0f; // tiles
    float spaced_min_apart = 5.0f;      // tiles
    int spaced_min_count = 2;           // a lone spawner is a fight with one bearing -- campable
};

Config loadConfig(const nlohmann::json& j)
{
    Config cfg;
    cfg.width = j.value("width", cfg.width);
    cfg.height = j.value("height", cfg.height);
    cfg.tile_size = j.value("tile_size", cfg.tile_size);
    cfg.chamber_attempts = j.value("chamber_attempts", cfg.chamber_attempts);
    cfg.chamber_limit = j.value("chamber_limit", cfg.chamber_limit);
    cfg.corridor_half = j.value("corridor_half", cfg.corridor_half);
    const auto& sm = j.value("spaced_markers", nlohmann::json::object());
    cfg.spaced_types = sm.value("types", cfg.spaced_types);
    cfg.spaced_min_from_spawn =
        static_cast<float>(sm.value("min_from_spawn_tiles", cfg.spaced_min_from_spawn));
    cfg.spaced_min_apart = static_cast<float>(sm.value("min_apart_tiles", cfg.spaced_min_apart));
    cfg.spaced_min_count = sm.value("min_count", cfg.spaced_min_count);
    return cfg;
}

// WHAT EACH ID IS CALLED WHERE THE ART IS. A cell tagged `Wall` in the tileset editor is the
// wall, and that is the whole of the mapping -- no column and row copied into a config to fall
// out of step with the art the first time the sheet is rearranged.
const std::pair<int, const char*> kRoles[] = {
    {TileMap::WALKABLE_ID, "Floor"},
    {TileMap::SOLID_ID, "Wall"},
};

// Point a kind of space at a tileset in the LDtk project and let each id find its own cell.
// Tiles a tag does not name keep their flat colour, so a half-drawn set renders both ways
// rather than waiting to be finished.
void loadTiles(const nlohmann::json& j, TileConfig& out)
{
    const std::string named = j.value("tileset", std::string{});
    if (named.empty())
        return;
    const area::Tiles set =
        area::tileset(j.value("project", std::string{"assets/maps/world.ldtk"}), named);
    if (!set.ok)
        return;
    out.tileset_path = set.atlas;
    out.atlas_tile_size = set.grid;
    for (const auto& [id, role] : kRoles)
    {
        const auto found = set.tagged.find(role);
        if (found == set.tagged.end() || found->second.empty())
            continue;
        TileConfig::TileVisual& tv = out.tile_visuals[id];
        tv.uv_col = found->second.front() % set.cols;
        tv.uv_row = found->second.front() / set.cols;
        tv.has_cell = true;
    }
}

void loadVisuals(const nlohmann::json& j, TileConfig& out)
{
    const auto vis = j.find("tile_visuals");
    if (vis == j.end() || !vis->is_object())
        return;
    for (const auto& [id, v] : vis->items())
    {
        TileConfig::TileVisual tv;
        tv.r = v.value("r", tv.r);
        tv.g = v.value("g", tv.g);
        tv.b = v.value("b", tv.b);
        tv.has_cell = false; // a colour, until a tag says where its cell is
        out.tile_visuals[std::stoi(id)] = tv;
    }
}

// Layout cells a template seals off from its own floor: connected-component count from the first
// walkable cell, minus the total. Zero for a well-formed room.
int sealedCellCount(const Chamber& chamber)
{
    std::vector<char> seen(chamber.tiles.size(), 0);
    int total = 0;
    int first = -1;
    for (int i = 0; i < static_cast<int>(chamber.tiles.size()); ++i)
        if (chamber.tiles[static_cast<std::size_t>(i)] != TileMap::SOLID_ID)
        {
            ++total;
            if (first < 0)
                first = i;
        }
    if (first < 0)
        return 0;
    std::queue<int> q;
    q.push(first);
    seen[static_cast<std::size_t>(first)] = 1;
    int reached = 0;
    while (!q.empty())
    {
        const int i = q.front();
        q.pop();
        ++reached;
        const int c = i % chamber.width;
        const int r = i / chamber.width;
        const int dc[4] = {1, -1, 0, 0};
        const int dr[4] = {0, 0, 1, -1};
        for (int k = 0; k < 4; ++k)
        {
            const int nc = c + dc[k];
            const int nr = r + dr[k];
            if (nc < 0 || nr < 0 || nc >= chamber.width || nr >= chamber.height)
                continue;
            const int ni = nr * chamber.width + nc; // bounded by a validated template
            if (seen[static_cast<std::size_t>(ni)] ||
                chamber.tiles[static_cast<std::size_t>(ni)] == TileMap::SOLID_ID)
                continue;
            seen[static_cast<std::size_t>(ni)] = 1;
            q.push(ni);
        }
    }
    return total - reached;
}

std::vector<Chamber> loadChambers(const std::string& dir)
{
    std::vector<Chamber> rooms;
    std::error_code ec;
    if (!std::filesystem::is_directory(dir, ec))
    {
        poe::log().error("floor: no rooms directory at '{}'", dir);
        return rooms;
    }
    // Sorted, so a given seed lays out the same floor between runs -- an
    // unordered directory scan would quietly make the seed meaningless.
    std::vector<std::filesystem::path> files;
    for (const auto& e : std::filesystem::directory_iterator(dir, ec))
        if (e.is_regular_file() && e.path().extension() == ".chamber")
            files.push_back(e.path());
    std::sort(files.begin(), files.end());

    for (const auto& f : files)
    {
        const std::ifstream in(f);
        if (!in)
            continue;
        std::stringstream ss;
        ss << in.rdbuf();
        Chamber r = parseChamber(ss.str(), f.filename().string());
        if (r.width > 0 && r.height > 0)
        {
            // A template whose floor is not all one piece has sealed cells -- floor drawn inside
            // a pillar, a walled-off corner. Nothing can ever reach them, and a hole placed there
            // would be a wave that cannot end. Authoring error: refuse the room, loudly, so it is
            // fixed the day it is drawn rather than found by a player.
            const int sealed = sealedCellCount(r);
            if (sealed > 0)
            {
                poe::log().error("floor: room '{}' has {} sealed floor cell(s) -- fix the "
                                 "template; skipping it",
                                 r.name, sealed);
                continue;
            }
            rooms.push_back(std::move(r));
        }
    }
    if (rooms.empty())
        poe::log().error("floor: '{}' held no readable .room files", dir);
    return rooms;
}

// Stamp a room's tiles into the map at (col,row).
void stamp(TileMap& map, const Placed& p)
{
    for (int r = 0; r < p.chamber->height; ++r)
        for (int c = 0; c < p.chamber->width; ++c)
        {
            const int mc = p.col + c;
            const int mr = p.row + r;
            if (mc < 0 || mr < 0 || mc >= map.width || mr >= map.height)
                continue;
            // Widen before multiplying, so the index arithmetic itself cannot overflow as int.
            const int id = p.chamber->tiles[static_cast<std::size_t>(r) *
                                                static_cast<std::size_t>(p.chamber->width) +
                                            static_cast<std::size_t>(c)];
            auto& tile =
                map.tiles[static_cast<std::size_t>(mr) * static_cast<std::size_t>(map.width) +
                          static_cast<std::size_t>(mc)];
            tile.tile_id = id;
            tile.walkable = id != TileMap::SOLID_ID;
        }
}

// THE WALLS GET A FRONT. Every solid cell with floor to its SOUTH is a wall the player is
// looking at, so it takes a face; every other solid cell is a wall seen from above and stays as
// it is. That one test is the whole convention: the wall at the top of a room faces the camera
// and gets a front, while the wall at the BOTTOM has floor to its north, is never touched, and
// so reads as a plain band of wall -- which is what stops it standing between the camera and the
// room. Nothing is special-cased into that; it is what the predicate already says.
//
// A DECORATION rather than a second solid tile id. Walkability is worked out from the id when a
// chamber is stamped and then kept on the tile, so a second solid id is a wall that whoever
// stamps next silently makes walkable. Decoration carries no collision at all, so this cannot
// reach the map's solidity however it is extended. Runs AFTER every stamp and carve, since it
// reads the finished shape.
// Carve a walkable run between two cells, `half` tiles either side of the line.
void carve(TileMap& map, int c0, int r0, int c1, int r1, int half)
{
    const auto open = [&](int c, int r)
    {
        for (int dr = -half; dr <= half; ++dr)
            for (int dc = -half; dc <= half; ++dc)
            {
                const int mc = c + dc;
                const int mr = r + dr;
                if (mc <= 0 || mr <= 0 || mc >= map.width - 1 || mr >= map.height - 1)
                    continue;
                auto& tile = map.tiles[tileIndex(mr, mc, map.width)];
                tile.tile_id = TileMap::WALKABLE_ID;
                tile.walkable = true;
            }
    };
    // L-shaped: all the way across, then all the way down.
    for (int c = std::min(c0, c1); c <= std::max(c0, c1); ++c)
        open(c, r0);
    for (int r = std::min(r0, r1); r <= std::max(r0, r1); ++r)
        open(c1, r);
}

// The walkable tile nearest (c0,r0), by breadth-first ring. Rooms are allowed interior
// architecture -- pillars, alcoves -- so nothing may assume a room's geometric centre is floor:
// the spawn and every corridor endpoint are derived from the STAMPED tiles instead. (The old
// centre-is-floor assumption mostly held only because corridors happened to carve the centre
// open whenever a second room existed.)
std::pair<int, int> nearestWalkable(const TileMap& map, int c0, int r0)
{
    std::vector<char> seen(static_cast<std::size_t>(map.width * map.height), 0);
    std::queue<std::pair<int, int>> q;
    q.emplace(c0, r0);
    if (c0 >= 0 && r0 >= 0 && c0 < map.width && r0 < map.height)
        seen[tileIndex(r0, c0, map.width)] = 1;
    while (!q.empty())
    {
        const auto [c, r] = q.front();
        q.pop();
        if (map.tiles[tileIndex(r, c, map.width)].walkable)
            return {c, r};
        const int dc[4] = {1, -1, 0, 0};
        const int dr[4] = {0, 0, 1, -1};
        for (int i = 0; i < 4; ++i)
        {
            const int nc = c + dc[i];
            const int nr = r + dr[i];
            if (nc < 0 || nr < 0 || nc >= map.width || nr >= map.height)
                continue;
            const auto idx = tileIndex(nr, nc, map.width);
            if (!seen[idx])
            {
                seen[idx] = 1;
                q.emplace(nc, nr);
            }
        }
    }
    return {c0, r0}; // no floor anywhere -- generation has already failed loudly
}

bool overlaps(const Placed& a, const std::vector<Placed>& placed)
{
    // One tile of padding, so two rooms never share a wall and corridors always
    // have something to cut through.
    for (const auto& b : placed)
        if (a.col < b.col + b.chamber->width + 1 && a.col + a.chamber->width + 1 > b.col &&
            a.row < b.row + b.chamber->height + 1 && a.row + a.chamber->height + 1 > b.row)
            return true;
    return false;
}
} // namespace

Chamber parseChamber(const std::string& text, const std::string& name)
{
    Chamber room;
    room.name = name;

    std::istringstream stream(text);
    std::string line;
    int row = 0;
    while (std::getline(stream, line))
    {
        if (!line.empty() && line.back() == '\r') // .room files may be CRLF
            line.pop_back();
        if (line.empty())
            continue;

        const int cols = static_cast<int>(line.size());
        if (room.width == 0)
            room.width = cols;
        else if (cols != room.width)
            poe::log().warn("floor: room '{}' row {} is {} wide, expected {} -- padding", name, row,
                            cols, room.width);

        for (int col = 0; col < room.width; ++col)
        {
            const char ch = col < cols ? line[static_cast<std::size_t>(col)] : '.';
            int tile_id = TileMap::WALKABLE_ID;
            if (ch == 'W')
                tile_id = TileMap::SOLID_ID;
            else if (ch != '.' && ch != ' ')
                // Any other letter is a MARKER on walkable floor. The generator
                // does not care what it means; the game reads the character.
                room.markers.push_back(
                    Marker{ch, static_cast<float>(col), static_cast<float>(row)});
            room.tiles.push_back(tile_id);
        }
        ++row;
    }
    room.height = row;
    return room;
}

namespace
{
// ROOMS, PLACED WITHOUT TOUCHING. Templates are tried at random spots until the floor holds as
// many as it wants or the tries run out; a room that overlaps one already down is dropped rather
// than shuffled, because a floor that is one room short is still a floor.
std::vector<Placed> layChambers(TileMap& map, const Config& cfg, const std::vector<Chamber>& rooms,
                                std::mt19937& rng)
{
    std::vector<Placed> placed;
    std::uniform_int_distribution<int> pick(0, static_cast<int>(rooms.size()) - 1);
    for (int attempt = 0;
         attempt < cfg.chamber_attempts &&
         static_cast<int>(placed.size()) < static_cast<std::size_t>(cfg.chamber_limit);
         ++attempt)
    {
        const Chamber& room = rooms[static_cast<std::size_t>(pick(rng))];
        if (room.width + 2 >= cfg.width || room.height + 2 >= cfg.height)
            continue; // too big for this floor
        std::uniform_int_distribution<int> cx(1, cfg.width - room.width - 2);
        std::uniform_int_distribution<int> cy(1, cfg.height - room.height - 2);
        const Placed p{cx(rng), cy(rng), &room};
        if (overlaps(p, placed))
            continue;
        stamp(map, p);
        placed.push_back(p);
    }

    return placed;
}

// AS MANY WELL-SPREAD SPAWNERS AS THE FLOOR WILL TAKE, from a list already ordered
// farthest-from-the-spawn first. The apart-rule BENDS before it starves the floor: fewer than
// min_count means a fight with too few bearings, which is campable, so it halves until enough
// survive or until it is meaningless. The from-spawn rule never bends -- however cramped the
// floor, nothing surfaces beside the way in.
std::vector<Marker> spreadOut(const std::vector<Marker>& spawners, float spawnX, float spawnY,
                              float minSpawn, float minApart, int minCount, float ts)
{
    const auto dist2 = [](float ax, float ay, float bx, float by)
    { return (ax - bx) * (ax - bx) + (ay - by) * (ay - by); };
    std::vector<Marker> kept;
    for (float apart = minApart;; apart *= 0.5f)
    {
        kept.clear();
        for (const auto& m : spawners)
        {
            if (!kept.empty() && dist2(m.x, m.y, spawnX, spawnY) < minSpawn * minSpawn)
                continue;
            bool crowded = false;
            for (const auto& k : kept)
                if (dist2(m.x, m.y, k.x, k.y) < apart * apart)
                    crowded = true;
            if (!crowded)
                kept.push_back(m);
        }
        if (static_cast<int>(kept.size()) >= minCount || apart < ts)
            return kept;
    }
}

// SPAWNER SPACING. Templates put markers where their author drew them, which knows nothing about
// where this floor's spawn landed or where another room's marker sits. The rule: spawner-type
// markers keep their distance from the way in and from each other; violators are dropped, and a
// floor keeps at least one -- the farthest from the spawn -- because a chamber with nothing
// coming into it is not a chamber. Greedy farthest-first, so the survivors are the well-spread ones
// rather than whichever the template list happened to order first.
void spaceSpawners(const Config& cfg, float ts, Layout& out)
{
    if (cfg.spaced_types.empty())
        return;
    const float minSpawn = cfg.spaced_min_from_spawn * ts;
    const float minApart = cfg.spaced_min_apart * ts;
    std::vector<Marker> spawners;
    std::vector<Marker> rest;
    for (const auto& m : out.markers)
        (cfg.spaced_types.find(m.type) != std::string::npos ? spawners : rest).push_back(m);

    const auto dist2 = [](float ax, float ay, float bx, float by)
    { return (ax - bx) * (ax - bx) + (ay - by) * (ay - by); };
    std::sort(spawners.begin(), spawners.end(),
              [&](const Marker& a, const Marker& b) {
                  return dist2(a.x, a.y, out.spawn_x, out.spawn_y) >
                         dist2(b.x, b.y, out.spawn_x, out.spawn_y);
              });
    std::vector<Marker> kept =
        spreadOut(spawners, out.spawn_x, out.spawn_y, minSpawn, minApart, cfg.spaced_min_count, ts);
    if (kept.empty() && !spawners.empty())
        kept.push_back(spawners.front()); // the farthest survives even a cramped floor
    if (kept.size() < spawners.size())
        poe::log().info("floor: {} spawner marker(s) dropped by spacing rules",
                        spawners.size() - kept.size());
    out.markers = std::move(rest);
    out.markers.insert(out.markers.end(), kept.begin(), kept.end());
}

// AN ATTEMPT BUILDS ITS OWN MAP and hands it back beside the layout that describes it.
//
// Writing into the world instead meant every attempt destroyed the one before it, so choosing
// the best of twelve required generating them all AGAIN to get the winner's map back -- twice
// the work, and a Layout that described a floor the world was not holding whenever the second
// pass failed to land on it. A map and the markers into it are one answer; returning half of it
// through the world was what let the halves disagree.
Layout generateOnce(const Config& cfg, const std::vector<Chamber>& rooms, unsigned seed,
                    TileMap& map)
{
    Layout out;
    out.seed = seed;
    std::mt19937 rng(seed);

    // Solid to begin with; rooms and corridors carve into it.
    map.tile_size = cfg.tile_size;
    map.width = cfg.width;
    map.height = cfg.height;
    map.tiles.assign(static_cast<std::size_t>(cfg.width) * static_cast<std::size_t>(cfg.height),
                     TileMap::Tile{TileMap::SOLID_ID, false});
    map.decoration.clear();
    map.overhang.clear();

    const std::vector<Placed> placed = layChambers(map, cfg, rooms, rng);

    if (placed.empty())
    {
        poe::log().error("floor: no room would fit in a {}x{} floor", cfg.width, cfg.height);
        return out;
    }

    // Connect each room to the one before it, so every room is reachable. Endpoints are real
    // floor near each centre, so a corridor joins room to room instead of tunnelling through
    // whatever architecture happened to stand at the centre.
    for (std::size_t i = 1; i < placed.size(); ++i)
    {
        const auto [ac, ar] =
            nearestWalkable(map, placed[i - 1].centreCol(), placed[i - 1].centreRow());
        const auto [bc, br] = nearestWalkable(map, placed[i].centreCol(), placed[i].centreRow());
        carve(map, ac, ar, bc, br, cfg.corridor_half);
    }

    // Markers, lifted from room-local tiles into world pixels.
    const float ts = static_cast<float>(cfg.tile_size);
    for (const auto& p : placed)
        for (const auto& m : p.chamber->markers)
            out.markers.push_back(Marker{m.type, (static_cast<float>(p.col) + m.x + 0.5f) * ts,
                                         (static_cast<float>(p.row) + m.y + 0.5f) * ts});

    const auto [spawnC, spawnR] =
        nearestWalkable(map, placed.front().centreCol(), placed.front().centreRow());
    out.spawn_x = (static_cast<float>(spawnC) + 0.5f) * ts;
    out.spawn_y = (static_cast<float>(spawnR) + 0.5f) * ts;

    // SPAWNER SPACING. Templates put markers where their author drew them, which knows nothing
    // about where this floor's spawn landed or where another room's marker sits. The rule:
    // spawner-type markers keep their distance from the way in and from each other; violators
    // are dropped, and a floor keeps at least one -- the farthest from the spawn -- because a
    // chamber with nothing coming into it is not a chamber. Greedy farthest-first, so the survivors
    // are the well-spread ones rather than whichever the template list happened to order first.
    spaceSpawners(cfg, ts, out);

    out.ok = true;
    return out;
}

// How many of the floor's markers are spawner-typed under this config.
int spawnerCount(const Config& cfg, const Layout& floor)
{
    int n = 0;
    for (const auto& m : floor.markers)
        if (cfg.spaced_types.find(m.type) != std::string::npos)
            ++n;
    return n;
}
} // namespace

// A POCKET'S SIZE, from the slab it copies. Scaled up because a slab is measured in tiles of
// wall and a room has to be stood in -- a four-by-two block is narrower than the man.
//
// TWO BOUNDS, AND NEITHER IS A NUMBER SOMEONE CHOSE. The ceiling is the type's own size, because
// a space inside a room cannot come out bigger than the rooms around it. The floor is the
// smallest template that exists plus the margin placement needs, because below that NOTHING can
// be stamped and the floor comes back unbuildable -- a minimum written down in config is one
// that can be set under what the templates require, and the failure is a room he cannot enter.
Config resized(Config cfg, Extent extent, const nlohmann::json& type,
               const std::vector<Chamber>& rooms)
{
    if (extent.cols <= 0 || extent.rows <= 0 || rooms.empty())
        return cfg;
    const int scale = type.value("pocket", nlohmann::json::object()).value("scale", 3);
    int leastW = rooms.front().width;
    int leastH = rooms.front().height;
    for (const auto& r : rooms)
    {
        leastW = std::min(leastW, r.width);
        leastH = std::min(leastH, r.height);
    }
    // `placeChambers` needs strictly more than the template plus its two-tile margin.
    cfg.width = std::min(cfg.width, std::max(leastW + 3, extent.cols * scale));
    cfg.height = std::min(cfg.height, std::max(leastH + 3, extent.rows * scale));
    return cfg;
}

Layout generate(EntityManager& em, const std::string& typePath, unsigned seed, Extent extent)
{
    const nlohmann::json type = formats::read(typePath);
    // POOLS, shared first: a type draws on the common templates plus whatever is its own, so a
    // plain corridor is authored once rather than copied into every kind of space.
    std::vector<Chamber> rooms;
    for (const auto& dir :
         type.value("chambers", std::vector<std::string>{"config/chambers/common"}))
    {
        std::vector<Chamber> pool = loadChambers(dir);
        rooms.insert(rooms.end(), std::make_move_iterator(pool.begin()),
                     std::make_move_iterator(pool.end()));
    }
    if (rooms.empty())
        return Layout{};
    const Config cfg = resized(loadConfig(type), extent, type, rooms);

    if (seed == 0)
        seed = static_cast<unsigned>(std::chrono::steady_clock::now().time_since_epoch().count() &
                                     0xFFFFFFFF);
    loadVisuals(type, em.tile_config); // seed-independent; once, not per attempt
    loadTiles(type, em.tile_config);   // AFTER the colours: a drawn cell replaces its fallback

    // THE HOLE GUARANTEE. A floor with one spawner is a fight with one bearing, which is
    // campable however the emergence behaves -- so a layout that cannot seat min_count holes
    // (templates too sparse, or the spawn exclusion swallowing them) is not accepted. Reroll on
    // a seed DERIVED from the requested one, so a caller passing a fixed seed still gets a
    // repeatable floor; give up loudly after enough tries and ship the best attempt rather
    // than nothing, because a playable floor with one hole beats no floor at all.
    constexpr int kAttempts = 12;
    Layout best;
    TileMap bestMap;
    int bestCount = -1;
    for (int attempt = 0; attempt < kAttempts; ++attempt)
    {
        const unsigned derived = seed + static_cast<unsigned>(attempt) * 2654435761u;
        TileMap map;
        Layout f = generateOnce(cfg, rooms, derived, map);
        if (!f.ok)
            continue;
        const int count = cfg.spaced_types.empty() ? cfg.spaced_min_count : spawnerCount(cfg, f);
        if (count >= cfg.spaced_min_count)
        {
            poe::log().info("floor: seed {} (attempt {}), {} markers", derived, attempt + 1,
                            f.markers.size());
            em.tile_map = std::move(map);
            return f;
        }
        if (count > bestCount)
        {
            best = f;
            bestMap = std::move(map);
            bestCount = count;
        }
    }
    // Give up loudly and ship the best seen, WITH ITS OWN MAP -- kept all along, so there is
    // nothing to rebuild and no way for the markers and the world to be describing different
    // floors.
    poe::log().warn("floor: no layout seated {} spawners in {} attempts -- shipping one with {}",
                    cfg.spaced_min_count, kAttempts, bestCount);
    if (bestCount >= 0)
        em.tile_map = std::move(bestMap);
    return best;
}

} // namespace roomgen
