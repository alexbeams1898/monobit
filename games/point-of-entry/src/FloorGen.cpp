#include "FloorGen.h"

#include "ecs/EntityManager.h"
#include "ops/LogUtils.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <queue>
#include <random>
#include <sstream>

namespace floorgen
{
namespace
{
// A placed room's footprint in tile coordinates -- kept so later placements can
// check for overlap and so corridors know what to connect.
struct Placed
{
    int col = 0;
    int row = 0;
    const Room* room = nullptr;

    int centreCol() const
    {
        return col + room->width / 2;
    }
    int centreRow() const
    {
        return row + room->height / 2;
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

// How the floor is laid out, from config/floor.json.
struct Config
{
    int width = 96; // the whole floor, in tiles
    int height = 72;
    int tile_size = 32;
    int room_attempts = 40; // placement tries; more rooms than this is unlikely
    int room_target = 6;    // stop once this many are down
    int corridor_half = 1;  // corridors are (2*half + 1) tiles wide
    // Placement rules for marker letters that are spawners. The generator stays agnostic about
    // what a letter MEANS -- the config names which letters carry the rule.
    std::string spaced_types;
    float spaced_min_from_spawn = 6.0f; // tiles
    float spaced_min_apart = 5.0f;      // tiles
    int spaced_min_count = 2;           // a lone spawner is a fight with one bearing -- campable
};

Config loadConfig(const std::string& path)
{
    Config cfg;
    std::ifstream in(path);
    if (!in)
    {
        poe::log().warn("floor: no config at '{}' -- using defaults", path);
        return cfg;
    }
    const nlohmann::json j = nlohmann::json::parse(in, nullptr, /*allow_exceptions=*/false);
    if (j.is_discarded())
    {
        poe::log().warn("floor: config at '{}' is not valid JSON -- using defaults", path);
        return cfg;
    }
    cfg.width = j.value("width", cfg.width);
    cfg.height = j.value("height", cfg.height);
    cfg.tile_size = j.value("tile_size", cfg.tile_size);
    cfg.room_attempts = j.value("room_attempts", cfg.room_attempts);
    cfg.room_target = j.value("room_target", cfg.room_target);
    cfg.corridor_half = j.value("corridor_half", cfg.corridor_half);
    const auto& sm = j.value("spaced_markers", nlohmann::json::object());
    cfg.spaced_types = sm.value("types", cfg.spaced_types);
    cfg.spaced_min_from_spawn =
        static_cast<float>(sm.value("min_from_spawn_tiles", cfg.spaced_min_from_spawn));
    cfg.spaced_min_apart = static_cast<float>(sm.value("min_apart_tiles", cfg.spaced_min_apart));
    cfg.spaced_min_count = sm.value("min_count", cfg.spaced_min_count);
    return cfg;
}

// Per-tile-id colours. With no tileset the renderer draws flat quads from these,
// which is what the art-less skeleton wants.
void loadVisuals(const std::string& path, TileConfig& out)
{
    std::ifstream in(path);
    if (!in)
        return;
    const nlohmann::json j = nlohmann::json::parse(in, nullptr, /*allow_exceptions=*/false);
    if (j.is_discarded())
        return;
    const auto vis = j.find("tile_visuals");
    if (vis == j.end() || !vis->is_object())
        return;
    for (const auto& [id, v] : vis->items())
    {
        TileConfig::TileVisual tv;
        tv.r = v.value("r", tv.r);
        tv.g = v.value("g", tv.g);
        tv.b = v.value("b", tv.b);
        out.tile_visuals[std::stoi(id)] = tv;
    }
}

// Floor cells a template seals off from its own floor: connected-component count from the first
// walkable cell, minus the total. Zero for a well-formed room.
int sealedCellCount(const Room& room)
{
    std::vector<char> seen(room.tiles.size(), 0);
    int total = 0;
    int first = -1;
    for (int i = 0; i < static_cast<int>(room.tiles.size()); ++i)
        if (room.tiles[static_cast<std::size_t>(i)] != TileMap::SOLID_ID)
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
        const int c = i % room.width;
        const int r = i / room.width;
        const int dc[4] = {1, -1, 0, 0};
        const int dr[4] = {0, 0, 1, -1};
        for (int k = 0; k < 4; ++k)
        {
            const int nc = c + dc[k];
            const int nr = r + dr[k];
            if (nc < 0 || nr < 0 || nc >= room.width || nr >= room.height)
                continue;
            const int ni = nr * room.width + nc; // bounded by a validated template
            if (seen[static_cast<std::size_t>(ni)] ||
                room.tiles[static_cast<std::size_t>(ni)] == TileMap::SOLID_ID)
                continue;
            seen[static_cast<std::size_t>(ni)] = 1;
            q.push(ni);
        }
    }
    return total - reached;
}

std::vector<Room> loadRooms(const std::string& dir)
{
    std::vector<Room> rooms;
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
        if (e.is_regular_file() && e.path().extension() == ".room")
            files.push_back(e.path());
    std::sort(files.begin(), files.end());

    for (const auto& f : files)
    {
        const std::ifstream in(f);
        if (!in)
            continue;
        std::stringstream ss;
        ss << in.rdbuf();
        Room r = parseRoom(ss.str(), f.filename().string());
        if (r.width > 0 && r.height > 0)
        {
            // A template whose floor is not all one piece has sealed cells -- floor drawn inside
            // a pillar, a walled-off corner. Nothing can ever reach them, and a seep placed there
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
    for (int r = 0; r < p.room->height; ++r)
        for (int c = 0; c < p.room->width; ++c)
        {
            const int mc = p.col + c;
            const int mr = p.row + r;
            if (mc < 0 || mr < 0 || mc >= map.width || mr >= map.height)
                continue;
            // Widen before multiplying, so the index arithmetic itself cannot overflow as int.
            const int id =
                p.room
                    ->tiles[static_cast<std::size_t>(r) * static_cast<std::size_t>(p.room->width) +
                            static_cast<std::size_t>(c)];
            auto& tile =
                map.tiles[static_cast<std::size_t>(mr) * static_cast<std::size_t>(map.width) +
                          static_cast<std::size_t>(mc)];
            tile.tile_id = id;
            tile.walkable = id != TileMap::SOLID_ID;
        }
}

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
        if (a.col < b.col + b.room->width + 1 && a.col + a.room->width + 1 > b.col &&
            a.row < b.row + b.room->height + 1 && a.row + a.room->height + 1 > b.row)
            return true;
    return false;
}
} // namespace

Room parseRoom(const std::string& text, const std::string& name)
{
    Room room;
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
Floor generateOnce(EntityManager& em, const Config& cfg, const std::vector<Room>& rooms,
                   unsigned seed)
{
    Floor out;
    out.seed = seed;
    std::mt19937 rng(seed);

    // Solid to begin with; rooms and corridors carve into it.
    TileMap& map = em.tile_map;
    map.tile_size = cfg.tile_size;
    map.width = cfg.width;
    map.height = cfg.height;
    map.tiles.assign(static_cast<std::size_t>(cfg.width) * static_cast<std::size_t>(cfg.height),
                     TileMap::Tile{TileMap::SOLID_ID, false});
    map.decoration.clear();
    map.overhang.clear();

    std::vector<Placed> placed;
    std::uniform_int_distribution<int> pick(0, static_cast<int>(rooms.size()) - 1);
    for (int attempt = 0;
         attempt < cfg.room_attempts &&
         static_cast<int>(placed.size()) < static_cast<std::size_t>(cfg.room_target);
         ++attempt)
    {
        const Room& room = rooms[static_cast<std::size_t>(pick(rng))];
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
        for (const auto& m : p.room->markers)
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
    // chamber with nothing seeping is not a chamber. Greedy farthest-first, so the survivors
    // are the well-spread ones rather than whichever the template list happened to order first.
    if (!cfg.spaced_types.empty())
    {
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
        // The spacing BENDS before it starves the floor: fewer spawners than min_count means a
        // fight with too few bearings, which is campable -- so the apart-rule halves until
        // enough survive (or until it is meaningless). The from-spawn rule never bends: however
        // cramped the floor, nothing surfaces beside the way in.
        std::vector<Marker> kept;
        for (float apart = minApart;; apart *= 0.5f)
        {
            kept.clear();
            for (const auto& m : spawners)
            {
                if (!kept.empty() &&
                    dist2(m.x, m.y, out.spawn_x, out.spawn_y) < minSpawn * minSpawn)
                    continue;
                bool crowded = false;
                for (const auto& k : kept)
                    if (dist2(m.x, m.y, k.x, k.y) < apart * apart)
                        crowded = true;
                if (!crowded)
                    kept.push_back(m);
            }
            if (static_cast<int>(kept.size()) >= cfg.spaced_min_count || apart < ts)
                break;
        }
        if (kept.empty() && !spawners.empty())
            kept.push_back(spawners.front()); // the farthest survives even a cramped floor
        if (kept.size() < spawners.size())
            poe::log().info("floor: {} spawner marker(s) dropped by spacing rules",
                            spawners.size() - kept.size());
        out.markers = std::move(rest);
        out.markers.insert(out.markers.end(), kept.begin(), kept.end());
    }
    out.ok = true;
    return out;
}

// How many of the floor's markers are spawner-typed under this config.
int spawnerCount(const Config& cfg, const Floor& floor)
{
    int n = 0;
    for (const auto& m : floor.markers)
        if (cfg.spaced_types.find(m.type) != std::string::npos)
            ++n;
    return n;
}
} // namespace

Floor generate(EntityManager& em, const std::string& configPath, const std::string& roomsDir,
               unsigned seed)
{
    const Config cfg = loadConfig(configPath);
    const std::vector<Room> rooms = loadRooms(roomsDir);
    if (rooms.empty())
        return Floor{};

    if (seed == 0)
        seed = static_cast<unsigned>(std::chrono::steady_clock::now().time_since_epoch().count() &
                                     0xFFFFFFFF);
    loadVisuals(configPath, em.tile_config); // seed-independent; once, not per attempt

    // THE SEEP GUARANTEE. A floor with one spawner is a fight with one bearing, which is
    // campable however the emergence behaves -- so a layout that cannot seat min_count seeps
    // (templates too sparse, or the spawn exclusion swallowing them) is not accepted. Reroll on
    // a seed DERIVED from the requested one, so a caller passing a fixed seed still gets a
    // repeatable floor; give up loudly after enough tries and ship the best attempt rather
    // than nothing, because a playable floor with one seep beats no floor at all.
    constexpr int kAttempts = 12;
    Floor best;
    int bestCount = -1;
    for (int attempt = 0; attempt < kAttempts; ++attempt)
    {
        const unsigned derived = seed + static_cast<unsigned>(attempt) * 2654435761u;
        Floor f = generateOnce(em, cfg, rooms, derived);
        if (!f.ok)
            continue;
        const int count = cfg.spaced_types.empty() ? cfg.spaced_min_count : spawnerCount(cfg, f);
        if (count > bestCount)
        {
            best = f;
            bestCount = count;
        }
        if (count >= cfg.spaced_min_count)
        {
            poe::log().info("floor: seed {} (attempt {}), {} markers", derived, attempt + 1,
                            f.markers.size());
            return f;
        }
    }
    poe::log().warn("floor: no layout seated {} spawners in {} attempts -- shipping one with {}",
                    cfg.spaced_min_count, kAttempts, bestCount);
    if (bestCount >= 0)
    {
        // The tile map in `em` currently holds the LAST attempt; rebuild the best one so the
        // returned markers and the world agree.
        Floor rebuilt;
        for (int attempt = 0; attempt < kAttempts; ++attempt)
        {
            const unsigned derived = seed + static_cast<unsigned>(attempt) * 2654435761u;
            rebuilt = generateOnce(em, cfg, rooms, derived);
            if (rebuilt.ok && spawnerCount(cfg, rebuilt) == bestCount)
                return rebuilt;
        }
    }
    return best;
}

} // namespace floorgen
