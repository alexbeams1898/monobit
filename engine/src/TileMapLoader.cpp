#include "TileMapLoader.h"

#include <algorithm>
#include <chrono>
#include <fstream>
#include <iostream>
#include <nlohmann/json.hpp>
#include <random>
#include <sstream>

// POSIX directory iteration -- avoids <filesystem>/<codecvt> which is broken
// when MSYS2 ucrt64 headers are mixed with the mingw64 linker runtime.
// dirent.h is available on Linux, macOS, and MSYS2/MinGW.
#include <dirent.h>
#include <sys/stat.h>

using json = nlohmann::json;

// ---------------------------------------------------------------------------
// parseRoom -- converts an ASCII template string into a Room struct.
//
// Character mapping:
//   '.' / ' ' -> WALKABLE_ID (walkable)
//   'W'       -> SOLID_ID  (solid)
//   'X'       -> tile_id 3 (solid)
//   Other letters -> WALKABLE_ID + SpawnPoint with that character as type
//                    (game interprets the marker meaning)
// ---------------------------------------------------------------------------
Room TileMapLoader::parseRoom(const std::string& text, const std::string& name)
{
    Room room;
    room.name = name;

    std::istringstream stream(text);
    std::string line;
    int row = 0;

    while (std::getline(stream, line))
    {
        // Strip trailing \r (Windows line endings in .room files)
        if (!line.empty() && line.back() == '\r')
            line.pop_back();
        if (line.empty())
            continue;

        const int col_count = static_cast<int>(line.size());
        if (room.width == 0)
        {
            room.width = col_count;
        }
        else if (col_count != room.width)
        {
            std::cout << "[TileMapLoader] Warning: room '" << name << "' row " << row << " width "
                      << col_count << " != expected " << room.width << " -- padding/truncating\n";
        }

        for (int col = 0; col < room.width; ++col)
        {
            const char ch = (col < col_count) ? line[static_cast<std::size_t>(col)] : '.';
            int tile_id = TileMap::WALKABLE_ID;
            char spawn = 0;

            switch (ch)
            {
            case 'W':
                tile_id = TileMap::SOLID_ID;
                break;
            case 'X':
                tile_id = 3; // solid non-wall tile
                break;
            case 'E':
                spawn = 'E';
                break;
            case 'C':
                spawn = 'C';
                break;
            case 'R':
                spawn = 'R';
                break;
            default:
                break; // '.' and ' ' -> walkable tile
            }

            room.tiles.push_back(tile_id);
            if (spawn != 0)
                room.spawn_points.push_back({col, row, spawn});
        }
        ++row;
    }

    room.height = row;
    return room;
}

// ---------------------------------------------------------------------------
// loadConfig -- parses config/tilemap.json.
// Falls back to hardcoded defaults if the file is missing.
// ---------------------------------------------------------------------------
TileConfig TileMapLoader::loadConfig(const std::string& path)
{
    TileConfig cfg;

    // Minimal structural defaults -- walkable/solid distinction only.
    // Game's tilemap.json config provides sprite paths, visuals, and full tile set.
    cfg.tiles[TileMap::WALKABLE_ID] = {"", true};
    cfg.tiles[TileMap::SOLID_ID] = {"", false};

    // Fallback flat colors (dark grey for walkable, darker for solid).
    cfg.tile_visuals[TileMap::WALKABLE_ID] = {0, 0, 0.20f, 0.20f, 0.20f};
    cfg.tile_visuals[TileMap::SOLID_ID] = {0, 0, 0.10f, 0.10f, 0.10f};

    std::ifstream f(path);
    if (!f.is_open())
    {
        std::cout << "[TileMapLoader] Cannot open config: " << path << " -- using defaults\n";
        return cfg;
    }

    try
    {
        const json j = json::parse(f);
        if (j.contains("tiles") && j["tiles"].is_object())
        {
            for (const auto& [id_str, entry] : j["tiles"].items())
            {
                const int tile_id = std::stoi(id_str);
                TileConfig::Entry e;
                e.sprite = entry.value("sprite",
                                       cfg.tiles.count(tile_id) ? cfg.tiles[tile_id].sprite : "");
                e.walkable = entry.value("walkable", true);
                cfg.tiles[tile_id] = e;

                // Read visual overrides from the same entry.
                if (entry.contains("uv_col") || entry.contains("uv_row") || entry.contains("r"))
                {
                    TileConfig::TileVisual vis = cfg.tile_visuals.count(tile_id)
                                                     ? cfg.tile_visuals[tile_id]
                                                     : TileConfig::TileVisual{};
                    vis.uv_col = entry.value("uv_col", vis.uv_col);
                    vis.uv_row = entry.value("uv_row", vis.uv_row);
                    vis.r = entry.value("r", vis.r);
                    vis.g = entry.value("g", vis.g);
                    vis.b = entry.value("b", vis.b);
                    cfg.tile_visuals[tile_id] = vis;
                }
            }
        }

        cfg.tileset_path = j.value("tileset", std::string{});
    }
    catch (const std::exception& ex)
    {
        std::cout << "[TileMapLoader] Parse error in " << path << ": " << ex.what()
                  << " -- using defaults\n";
    }

    return cfg;
}

// ---------------------------------------------------------------------------
// loadRooms -- scans 'dir' for *.room files and parses each one.
// ---------------------------------------------------------------------------
std::vector<Room> TileMapLoader::loadRooms(const std::string& dir)
{
    std::vector<Room> rooms;

    // Check directory exists via POSIX stat.
    struct stat st;
    if (stat(dir.c_str(), &st) != 0 || !S_ISDIR(st.st_mode))
    {
        std::cout << "[TileMapLoader] Rooms directory not found: " << dir << "\n";
        return rooms;
    }

    DIR* dp = opendir(dir.c_str());
    if (!dp)
    {
        std::cout << "[TileMapLoader] Cannot open rooms directory: " << dir << "\n";
        return rooms;
    }

    struct dirent* de;
    while ((de = readdir(dp)) != nullptr)
    {
        const std::string fileName = de->d_name;
        // Skip entries that don't end in ".room".
        if (fileName.size() < 5 || fileName.compare(fileName.size() - 5, 5, ".room") != 0)
            continue;

        const std::string filePath = dir + "/" + fileName;
        std::ifstream roomFile(filePath);
        if (!roomFile.is_open())
        {
            std::cout << "[TileMapLoader] Cannot open room: " << filePath << "\n";
            continue;
        }

        const std::string text((std::istreambuf_iterator<char>(roomFile)),
                               std::istreambuf_iterator<char>());
        Room room = parseRoom(text, fileName);

        if (room.width < 3 || room.height < 3)
        {
            std::cout << "[TileMapLoader] Skipping tiny room: " << room.name << "\n";
            continue;
        }

        rooms.push_back(std::move(room));
        std::cout << "[TileMapLoader] Loaded room '" << rooms.back().name << "' ("
                  << rooms.back().width << "x" << rooms.back().height << ")\n";
    }

    closedir(dp);
    return rooms;
}

// ---------------------------------------------------------------------------
// canPlace -- returns true if the room can be stamped at (col, row) without
// overlapping any existing non-Wall tiles (+ a 2-tile border margin).
// ---------------------------------------------------------------------------
bool TileMapLoader::canPlace(const TileMap& map, const Room& room, int col, int row)
{
    constexpr int MARGIN = 2;
    const int c0 = col - MARGIN;
    const int r0 = row - MARGIN;
    const int c1 = col + room.width + MARGIN;
    const int r1 = row + room.height + MARGIN;

    // Must be fully inside the map (including margin).
    if (c0 < 0 || r0 < 0 || c1 > map.width || r1 > map.height)
        return false;

    // Check that every cell in the footprint + margin is still Wall (untouched).
    for (int r = r0; r < r1; ++r)
    {
        for (int c = c0; c < c1; ++c)
        {
            if (map.at(c, r).tile_id != TileMap::SOLID_ID)
                return false;
        }
    }
    return true;
}

// ---------------------------------------------------------------------------
// stampRoom -- copies room tiles into the TileMap at (origin_col, origin_row).
// ---------------------------------------------------------------------------
void TileMapLoader::stampRoom(TileMap& map, const Room& room, int origin_col, int origin_row)
{
    for (int r = 0; r < room.height; ++r)
    {
        for (int c = 0; c < room.width; ++c)
        {
            const int id =
                room.tiles[static_cast<std::size_t>(r) * static_cast<std::size_t>(room.width) +
                           static_cast<std::size_t>(c)];
            auto& tile = map.at(origin_col + c, origin_row + r);
            tile.tile_id = id;
            tile.walkable = (id == TileMap::WALKABLE_ID);
        }
    }
}

// ---------------------------------------------------------------------------
// placeRooms -- randomly places rooms on the map.
// ---------------------------------------------------------------------------
void TileMapLoader::placeRooms(TileMap& map, const std::vector<Room>& rooms, std::mt19937& rng,
                               int count, std::vector<std::pair<int, int>>& centers)
{
    if (rooms.empty())
    {
        std::cout << "[TileMapLoader] No room templates -- map will be all solid\n";
        return;
    }

    constexpr int MAX_TRIES = 30;

    // Separate rest rooms (filename contains "rest") from normal combat rooms.
    // Slot 0 = largest normal room (player start).
    // Slot 1 = one rest room (guaranteed if templates exist).
    // Slots 2..N = random normal rooms.
    std::vector<const Room*> normal_rooms, rest_rooms;
    for (const auto& roomRef : rooms)
    {
        if (roomRef.name.find("rest") != std::string::npos)
            rest_rooms.push_back(&roomRef);
        else
            normal_rooms.push_back(&roomRef);
    }
    if (normal_rooms.empty())
        normal_rooms = rest_rooms; // graceful fallback -- use everything

    const Room& start_room = **std::max_element(
        normal_rooms.begin(), normal_rooms.end(),
        [](const Room* a, const Room* b) { return a->width * a->height < b->width * b->height; });

    std::uniform_int_distribution<int> pick_normal(0, static_cast<int>(normal_rooms.size()) - 1);
    std::uniform_int_distribution<int> pick_rest(0, static_cast<int>(rest_rooms.size()) - 1);

    for (int i = 0; i < count; ++i)
    {
        const Room* pick = nullptr;
        if (i == 0)
            pick = &start_room;
        else if (i == 1 && !rest_rooms.empty())
            pick = rest_rooms[static_cast<std::size_t>(pick_rest(rng))];
        else
            pick = normal_rooms[static_cast<std::size_t>(pick_normal(rng))];
        const Room& room = *pick;
        bool placed = false;

        for (int attempt = 0; attempt < MAX_TRIES; ++attempt)
        {
            std::uniform_int_distribution<int> col_dist(2, map.width - room.width - 2);
            std::uniform_int_distribution<int> row_dist(2, map.height - room.height - 2);

            if (col_dist.a() > col_dist.b() || row_dist.a() > row_dist.b())
                break; // room is too large for the map

            const int placedCol = col_dist(rng);
            const int placedRow = row_dist(rng);

            if (canPlace(map, room, placedCol, placedRow))
            {
                stampRoom(map, room, placedCol, placedRow);

                // Record world-space center for corridor connections.
                const int cx = placedCol + room.width / 2;
                const int cy = placedRow + room.height / 2;
                centers.push_back({cx, cy});

                // Store room rect for runtime queries (e.g. spawn scoping).
                map.placed_rooms.push_back({placedCol, placedRow, room.width, room.height});

                // Collect spawn points as world-space positions.
                for (const auto& sp : room.spawn_points)
                {
                    map.spawn_points.push_back(
                        {static_cast<float>((placedCol + sp.col) * TileMap::TILE_SIZE) +
                             TileMap::TILE_SIZE * 0.5f,
                         static_cast<float>((placedRow + sp.row) * TileMap::TILE_SIZE) +
                             TileMap::TILE_SIZE * 0.5f,
                         sp.type});
                }

                placed = true;
                std::cout << "[TileMapLoader] Placed room '" << room.name << "' at (" << placedCol
                          << "," << placedRow << ")\n";
                break;
            }
        }

        if (!placed)
            std::cout << "[TileMapLoader] Could not place room " << i << " after " << MAX_TRIES
                      << " attempts\n";
    }
}

// ---------------------------------------------------------------------------
// connectRooms -- connects adjacent room centers with 3-tile-wide L-corridors.
// ---------------------------------------------------------------------------
void TileMapLoader::connectRooms(TileMap& map, const std::vector<std::pair<int, int>>& centers)
{
    if (centers.size() < 2)
        return;

    // Sort a copy by center X for left-to-right connection order.
    auto sorted = centers;
    std::sort(sorted.begin(), sorted.end(),
              [](const std::pair<int, int>& a, const std::pair<int, int>& b)
              { return a.first < b.first; });

    auto carve_h = [&](int col_from, int col_to, int row)
    {
        const int c0 = std::min(col_from, col_to);
        const int c1 = std::max(col_from, col_to);
        for (int c = c0; c <= c1; ++c)
        {
            for (int dr = -1; dr <= 1; ++dr)
            {
                const int r = row + dr;
                if (!map.in_bounds(c, r))
                    continue;
                auto& tile = map.at(c, r);
                if (tile.tile_id == TileMap::SOLID_ID)
                {
                    tile.tile_id = TileMap::WALKABLE_ID;
                    tile.walkable = true;
                }
            }
        }
    };

    auto carve_v = [&](int col, int row_from, int row_to)
    {
        const int r0 = std::min(row_from, row_to);
        const int r1 = std::max(row_from, row_to);
        for (int r = r0; r <= r1; ++r)
        {
            for (int dc = -1; dc <= 1; ++dc)
            {
                const int c = col + dc;
                if (!map.in_bounds(c, r))
                    continue;
                auto& tile = map.at(c, r);
                if (tile.tile_id == TileMap::SOLID_ID)
                {
                    tile.tile_id = TileMap::WALKABLE_ID;
                    tile.walkable = true;
                }
            }
        }
    };

    for (std::size_t i = 0; i + 1 < sorted.size(); ++i)
    {
        const auto [x1, y1] = sorted[i];
        const auto [x2, y2] = sorted[i + 1];
        // L-shape: horizontal first, then vertical.
        carve_h(x1, x2, y1);
        carve_v(x2, y1, y2);
    }
}

// ---------------------------------------------------------------------------
// generate -- full pipeline: config -> rooms -> procgen -> ECS entities.
// ---------------------------------------------------------------------------
std::pair<float, float> TileMapLoader::generate(EntityManager& em,
                                                const std::string& tilemapConfigPath,
                                                const std::string& roomsDir, uint32_t seed)
{
    // --- Seed -----------------------------------------------------------
    if (seed == 0)
    {
        seed = static_cast<uint32_t>(std::chrono::system_clock::now().time_since_epoch().count());
    }
    std::cout << "[TileMap] Seed: " << seed << "\n";
    std::mt19937 rng(seed);

    // --- Load config + rooms --------------------------------------------
    TileConfig config = loadConfig(tilemapConfigPath);

    // Read map dimensions from JSON; fall back to 80x60.
    int map_width = 80;
    int map_height = 60;
    int room_count = 6;
    {
        std::ifstream f(tilemapConfigPath);
        if (f.is_open())
        {
            try
            {
                const json j = json::parse(f);
                map_width = j.value("width", map_width);
                map_height = j.value("height", map_height);
                room_count = j.value("room_count", room_count);
            }
            catch (...)
            {
                std::cerr << "[TileMapLoader] Failed to parse tilemap config; using defaults.\n";
            }
        }
    }

    std::vector<Room> roomList = loadRooms(roomsDir);

    // --- Build TileMap (all Solid initially) -----------------------------
    TileMap map;
    map.width = map_width;
    map.height = map_height;
    map.seed = seed;
    map.tiles.assign(static_cast<std::size_t>(map_width) * static_cast<std::size_t>(map_height),
                     TileMap::Tile{TileMap::SOLID_ID, false});

    // --- Place rooms + connect ------------------------------------------
    std::vector<std::pair<int, int>> centers;
    placeRooms(map, roomList, rng, room_count, centers);
    connectRooms(map, centers);

    // --- Write to EntityManager -----------------------------------------
    em.tile_map = std::move(map);
    em.tile_config = std::move(config);

    // --- Player spawn = center of first placed room ---------------------
    if (!centers.empty())
    {
        const float px =
            static_cast<float>(centers[0].first * TileMap::TILE_SIZE) + TileMap::TILE_SIZE * 0.5f;
        const float py =
            static_cast<float>(centers[0].second * TileMap::TILE_SIZE) + TileMap::TILE_SIZE * 0.5f;
        std::cout << "[TileMap] Player spawn: (" << px << ", " << py << ")\n";
        return {px, py};
    }

    // Fallback: map centre.
    return {static_cast<float>(map_width * TileMap::TILE_SIZE) * 0.5f,
            static_cast<float>(map_height * TileMap::TILE_SIZE) * 0.5f};
}
