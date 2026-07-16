#include "SaveGame.h"

#include "GameLoop.h"
#include "utils/SaveFile.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cstdlib>

using json = nlohmann::json;

namespace savegame
{
namespace
{

// --- json helpers -------------------------------------------------------------
// Reads are tolerant by convention: an absent key takes its default, so adding a
// field needs no migration (see SaveGame.h).

json fromSet(const std::unordered_set<std::string>& s)
{
    return json(s); // nlohmann serializes a set of strings as an array
}

void toSet(const json& j, const char* key, std::unordered_set<std::string>& out)
{
    const auto it = j.find(key);
    if (it == j.end() || !it->is_array())
        return;
    for (const auto& e : *it)
        if (e.is_string())
            out.insert(e.get<std::string>());
}

void toIntMap(const json& j, const char* key, std::unordered_map<std::string, int>& out)
{
    const auto it = j.find(key);
    if (it == j.end() || !it->is_object())
        return;
    for (const auto& [k, v] : it->items())
        if (v.is_number_integer())
            out[k] = v.get<int>();
}

json encodeRecord(const Record& r)
{
    return json{{"observed_tier", r.observed_tier},
                {"fired", fromSet(r.fired)},
                {"flags", fromSet(r.flags)},
                {"taken", fromSet(r.taken)}};
}

Record decodeRecord(const json& j)
{
    Record r;
    toIntMap(j, "observed_tier", r.observed_tier);
    toSet(j, "fired", r.fired);
    toSet(j, "flags", r.flags);
    toSet(j, "taken", r.taken);
    return r;
}

json encodeSelf(const Self& s)
{
    return json{{"spirit_exp", s.spirit_exp},
                {"stat_levels", s.stat_levels},
                {"buff_levels", s.buff_levels}};
}

Self decodeSelf(const json& j)
{
    Self s;
    s.spirit_exp = j.value("spirit_exp", 0);
    toIntMap(j, "stat_levels", s.stat_levels);
    toIntMap(j, "buff_levels", s.buff_levels);
    return s;
}

json encodeSatchel(const std::vector<Item>& items)
{
    json a = json::array();
    for (const auto& i : items)
        a.push_back(json{{"id", i.id}, {"quantity", i.quantity}, {"is_new", i.is_new}});
    return a;
}

std::vector<Item> decodeSatchel(const json& j)
{
    std::vector<Item> out;
    const auto it = j.find("satchel");
    if (it == j.end() || !it->is_array())
        return out;
    for (const auto& e : *it)
    {
        if (!e.is_object())
            continue;
        Item i;
        i.id = e.value("id", std::string{});
        i.quantity = e.value("quantity", 1);
        i.is_new = e.value("is_new", false);
        if (!i.id.empty() && i.quantity > 0)
            out.push_back(std::move(i));
    }
    return out;
}

json encodeNotebook(const std::vector<NotebookEntry>& entries)
{
    json a = json::array();
    for (const auto& e : entries)
        a.push_back(json{{"kind", e.kind},
                         {"text", e.text},
                         {"faculty", e.faculty},
                         {"difficulty", e.difficulty},
                         {"day", e.day}});
    return a;
}

std::vector<NotebookEntry> decodeNotebook(const json& j)
{
    std::vector<NotebookEntry> out;
    const auto it = j.find("notebook");
    if (it == j.end() || !it->is_array())
        return out;
    for (const auto& e : *it)
    {
        if (!e.is_object())
            continue;
        NotebookEntry n;
        n.kind = e.value("kind", 0);
        n.text = e.value("text", std::string{});
        n.faculty = e.value("faculty", std::string{});
        n.difficulty = e.value("difficulty", 0);
        n.day = e.value("day", 0);
        out.push_back(std::move(n));
    }
    return out;
}

// An identity no pilgrim has ever had in this file. Draws from the file's own
// ever-minted counter rather than the roster, because the roster forgets: deleting the
// only pilgrim would otherwise reset the count and hand their id to the next one.
//
// The counter is also floored past any id already present, so a hand-edited file (or one
// whose counter was lost) can't mint a duplicate.
std::string mintId(File& file)
{
    for (const auto& p : file.pilgrims)
        if (p.id.size() > 1 && p.id[0] == 'p')
            file.minted = std::max(file.minted, std::atoi(p.id.c_str() + 1));
    ++file.minted;
    return "p" + std::to_string(file.minted);
}

json encodePilgrim(const Data& d)
{
    return json{{"id", d.id},
                {"name", d.name},
                {"record", encodeRecord(d.record)},
                {"self", encodeSelf(d.self)},
                {"satchel", encodeSatchel(d.satchel)},
                {"notebook", encodeNotebook(d.notebook)},
                {"known_recipes", fromSet(d.known_recipes)},
                {"announced", fromSet(d.announced)},
                {"clock_seconds", d.clock_seconds},
                {"place", json{{"x", d.place.x},
                               {"y", d.place.y},
                               {"region", d.place.region},
                               {"walked", d.place.walked}}}};
}

Data decodePilgrim(const json& j)
{
    Data d;
    d.id = j.value("id", std::string{});
    d.name = j.value("name", std::string{});
    if (const auto it = j.find("record"); it != j.end() && it->is_object())
        d.record = decodeRecord(*it);
    if (const auto it = j.find("self"); it != j.end() && it->is_object())
        d.self = decodeSelf(*it);
    d.satchel = decodeSatchel(j);
    d.notebook = decodeNotebook(j);
    toSet(j, "known_recipes", d.known_recipes);
    toSet(j, "announced", d.announced);
    d.clock_seconds = j.value("clock_seconds", 0.0);
    if (const auto it = j.find("place"); it != j.end() && it->is_object())
    {
        d.place.x = it->value("x", 0.0f);
        d.place.y = it->value("y", 0.0f);
        d.place.region = it->value("region", std::string{});
        d.place.walked = it->value("walked", false);
    }
    return d;
}

json encode(const File& f)
{
    json pilgrims = json::array();
    for (const auto& p : f.pilgrims)
        pilgrims.push_back(encodePilgrim(p));
    return json{{"schema_version", f.schema_version},
                {"minted", f.minted},
                {"pilgrims", std::move(pilgrims)}};
}

File decode(const json& j)
{
    File f;
    f.schema_version = j.value("schema_version", 0); // 0 = pre-versioning; migrate decides
    f.minted = j.value("minted", 0);                 // a file without one gets floored in mintId
    if (const auto it = j.find("pilgrims"); it != j.end() && it->is_array())
        for (const auto& e : *it)
            if (e.is_object())
                f.pilgrims.push_back(decodePilgrim(e));
    return f;
}

} // namespace

void migrate(File& file)
{
    // No older shapes exist yet -- v1 is the first. When one does, step it forward
    // here (v1 -> v2 -> ...), then stamp the version. Additive fields never land
    // here; they read as their defaults.
    //
    // A pilgrim read from a file that predates stable ids has none; mint one so the
    // rest of the game can rely on every pilgrim having an identity.
    for (auto& p : file.pilgrims)
        if (p.id.empty())
            p.id = mintId(file);
    file.schema_version = kSchemaVersion;
}

File load(const std::string& path)
{
    const std::string resolved = path.empty() ? engine::save::path(kOrgName, kAppName) : path;
    const auto doc = engine::save::readJson(resolved);
    if (!doc)
        return File{}; // no save / unreadable -> nobody has walked yet
    File file = decode(*doc);
    migrate(file); // AFTER the read, never during it
    return file;
}

bool save(const File& file, const std::string& path)
{
    const std::string resolved = path.empty() ? engine::save::path(kOrgName, kAppName) : path;
    return engine::save::writeJson(encode(file), resolved);
}

Data* find(File& file, const std::string& id)
{
    if (id.empty())
        return nullptr;
    for (auto& p : file.pilgrims)
        if (p.id == id)
            return &p;
    return nullptr;
}

const Data* find(const File& file, const std::string& id)
{
    if (id.empty())
        return nullptr;
    for (const auto& p : file.pilgrims)
        if (p.id == id)
            return &p;
    return nullptr;
}

std::string add(File& file, const std::string& name)
{
    Data d;
    d.id = mintId(file);
    d.name = name;
    file.pilgrims.push_back(std::move(d));
    return file.pilgrims.back().id;
}

void remove(File& file, const std::string& id)
{
    // Exactly the one with this id -- a namesake is a different pilgrim and stays.
    for (auto it = file.pilgrims.begin(); it != file.pilgrims.end(); ++it)
        if (it->id == id)
        {
            file.pilgrims.erase(it);
            return;
        }
}

void capture(const GameState& gs, float player_x, float player_y, Data& d)
{
    // Identity (id/name) is NOT captured from the world -- the world doesn't own it. This
    // writes the walk into an existing pilgrim, leaving who they are untouched.
    d.record.observed_tier = gs.observations.observed_tier;
    d.record.fired = gs.observations.fired;
    d.record.flags = gs.observations.flags;
    d.record.taken = gs.observations.taken;

    d.self.spirit_exp = gs.growth.spirit_exp;
    d.self.stat_levels = gs.growth.stat_levels;
    d.self.buff_levels = gs.growth.buff_levels;

    d.satchel.reserve(gs.satchel.items.size());
    for (const auto& e : gs.satchel.items)
        d.satchel.push_back(Item{e.id, e.quantity, e.is_new});

    d.notebook.reserve(gs.notebook.entries.size());
    for (const auto& e : gs.notebook.entries)
        d.notebook.push_back(
            NotebookEntry{static_cast<int>(e.kind), e.text, e.faculty, e.difficulty, e.day});

    d.known_recipes = gs.crafting_state.known;
    d.announced = gs.announced_unlocks;
    d.clock_seconds = gs.clock.seconds;
    d.place.x = player_x;
    d.place.y = player_y;
    d.place.walked = true; // they have been somewhere now
}

void apply(const Data& data, GameState& gs)
{
    gs.observations.observed_tier = data.record.observed_tier;
    gs.observations.fired = data.record.fired;
    gs.observations.flags = data.record.flags;
    gs.observations.taken = data.record.taken;

    gs.growth.spirit_exp = data.self.spirit_exp;
    // Saved levels REPLACE the authored starting levels (config seeds a new
    // pilgrimage; a save is where this one got to).
    for (const auto& [name, level] : data.self.stat_levels)
        gs.growth.stat_levels[name] = level;
    gs.growth.buff_levels = data.self.buff_levels;

    gs.satchel.items.clear();
    gs.satchel.items.reserve(data.satchel.size());
    for (const auto& i : data.satchel)
        gs.satchel.items.push_back(inventory::ItemInstance{i.id, i.quantity, i.is_new});

    gs.notebook.entries.clear();
    gs.notebook.entries.reserve(data.notebook.size());
    for (const auto& e : data.notebook)
        gs.notebook.entries.push_back(notebook::Entry{static_cast<observations::LineKind>(e.kind),
                                                      e.text, e.faculty, e.difficulty, e.day});

    gs.crafting_state.known = data.known_recipes;
    gs.announced_unlocks = data.announced;
    gs.clock.seconds = data.clock_seconds;
}

} // namespace savegame
