#include "formats/SaveGame.h"

#include "ops/LogUtils.h"
#include "utils/SaveFile.h"

#include <nlohmann/json.hpp>

#include <array>

namespace savegame
{
namespace
{

nlohmann::json encode(const descent::Hole& h)
{
    return nlohmann::json{{"to", {{"room", h.to.room}, {"hole", h.to.hole}}},
                          {"kind", h.kind},
                          {"opened", h.opened},
                          {"cleared", h.cleared},
                          {"killed", h.killed}};
}

nlohmann::json encode(const descent::Room& f)
{
    nlohmann::json holes = nlohmann::json::array();
    for (const auto& h : f.holes)
        holes.push_back(encode(h));
    return nlohmann::json{{"area", f.area}, {"label", f.label}, {"type", f.type},
                          {"seed", f.seed}, {"depth", f.depth}, {"way_in", f.way_in},
                          {"holes", holes}};
}

descent::Hole decodeHole(const nlohmann::json& j)
{
    descent::Hole h;
    if (!j.is_object())
        return h;
    const auto to = j.value("to", nlohmann::json::object());
    h.to.room = to.value("room", to.value("node", -1));
    h.to.hole = to.value("hole", -1);
    h.kind = j.value("kind", h.kind);
    h.opened = j.value("opened", h.opened);
    h.cleared = j.value("cleared", h.cleared);
    h.killed = j.value("killed", h.killed);
    return h;
}

descent::Room decodeFloor(const nlohmann::json& j)
{
    descent::Room f;
    if (!j.is_object())
        return f;
    f.area = j.value("area", f.area);
    f.label = j.value("label", f.label);
    // A floor written before kinds of space existed is a cellar, which is what every floor was.
    f.type = j.value("type", f.type);
    f.seed = j.value("seed", f.seed);
    f.depth = j.value("depth", f.depth);
    f.way_in = j.value("way_in", f.way_in);
    for (const auto& h : j.value("holes", nlohmann::json::array()))
        f.holes.push_back(decodeHole(h));
    return f;
}

nlohmann::json encode(const Man& m)
{
    return nlohmann::json{
        {"chemical", m.chemical},
        {"physical", m.physical},
        {"biological", m.biological},
        {"endurance", m.endurance},
        {"inspection", m.inspection},
        {"banked", m.banked},
        {"thermos_fill", m.thermos_fill},
        {"thermos_sips", m.thermos_sips},
        {"tool", m.tool},
        {"health", m.health},
        {"charge", m.charge},
        {"satchel", [&]
         {
             nlohmann::json out = nlohmann::json::array();
             for (const auto& it : m.satchel)
                 out.push_back({{"id", it.id}, {"quality", it.quality}, {"count", it.count}});
             return out;
         }()}};
}

Man decodeMan(const nlohmann::json& j)
{
    Man m;
    if (!j.is_object())
        return m;
    m.chemical = j.value("chemical", m.chemical);
    m.physical = j.value("physical", m.physical);
    m.biological = j.value("biological", m.biological);
    m.endurance = j.value("endurance", m.endurance);
    m.inspection = j.value("inspection", m.inspection);
    m.banked = j.value("banked", m.banked);
    m.thermos_fill = j.value("thermos_fill", m.thermos_fill);
    m.thermos_sips = j.value("thermos_sips", m.thermos_sips);
    m.tool = j.value("tool", m.tool);
    m.health = j.value("health", m.health);
    m.charge = j.value("charge", m.charge);
    for (const auto& e : j.value("satchel", nlohmann::json::array()))
    {
        Item it;
        it.id = e.value("id", std::string{});
        it.quality = e.value("quality", 0);
        it.count = e.value("count", 1);
        if (!it.id.empty())
            m.satchel.push_back(std::move(it));
    }
    return m;
}

nlohmann::json encode(const Data& d)
{
    nlohmann::json floors = nlohmann::json::array();
    for (const auto& f : d.descent)
        floors.push_back(encode(f));
    return nlohmann::json{{"id", d.id},
                          {"record", d.record},
                          {"descent", floors},
                          {"man", encode(d.man)},
                          {"where",
                           {{"room", d.where.room},
                            {"area", d.where.area},
                            {"x", d.where.x},
                            {"y", d.where.y},
                            {"stood", d.where.stood}}}};
}

Data decodeData(const nlohmann::json& j)
{
    Data d;
    if (!j.is_object())
        return d;
    d.id = j.value("id", std::string{});
    d.record = j.value("record", std::map<std::string, int>{});
    for (const auto& f : j.value("descent", nlohmann::json::array()))
        d.descent.push_back(decodeFloor(f));
    d.man = decodeMan(j.value("man", nlohmann::json::object()));
    const auto where = j.value("where", nlohmann::json::object());
    d.where.room = where.value("room", where.value("node", -1));
    d.where.area = where.value("area", std::string{});
    d.where.x = where.value("x", 0.0f);
    d.where.y = where.value("y", 0.0f);
    d.where.stood = where.value("stood", false);
    return d;
}

// WHAT v1 KNEW ABOUT A TREE, read off every floor before any of them is rewritten. A floor's
// way in wears the kind of the hole on the far side, so by the time the fold reaches floor two
// the answer it needs from floor one is already gone -- it is taken up front instead.
struct Legacy
{
    std::vector<int> shifts; // 1 where a floor gains a way in, which shifts its own holes up
    std::vector<std::vector<std::string>> kinds;

    int shift(int at) const
    {
        return at >= 0 && static_cast<std::size_t>(at) < shifts.size()
                   ? shifts[static_cast<std::size_t>(at)]
                   : 0;
    }
    std::string kindAt(int floor, int hole) const
    {
        if (floor < 0 || static_cast<std::size_t>(floor) >= kinds.size() || hole < 0)
            return {};
        const auto& theirs = kinds[static_cast<std::size_t>(floor)];
        return static_cast<std::size_t>(hole) < theirs.size()
                   ? theirs[static_cast<std::size_t>(hole)]
                   : std::string{};
    }
};

Legacy readLegacy(const nlohmann::json& floors)
{
    Legacy was;
    for (const auto& f : floors)
    {
        was.shifts.push_back(f.value("from", f.value("parent", -1)) >= 0 ? 1 : 0);
        was.kinds.push_back(f.value("kind", std::vector<std::string>{}));
    }
    return was;
}

// One floor's five parallel per-hole arrays become ONE list of holes, and the way he came in --
// which v1 kept on the far side as from/from_hole and drew as its own kind of square -- becomes
// hole zero of the floor it leads into.
void foldFloor(nlohmann::json& f, const Legacy& was)
{
    const auto child = f.value("child", std::vector<int>{});
    const auto cleared = f.value("cleared", std::vector<bool>{});
    const auto opened = f.value("opened", std::vector<bool>{});
    const auto kind = f.value("kind", std::vector<std::string>{});
    const auto killed = f.value("killed", std::vector<int>{});
    const int from = f.value("from", f.value("parent", -1));
    const int fromHole = f.value("from_hole", f.value("parent_hole", -1));

    nlohmann::json holes = nlohmann::json::array();
    if (from >= 0)
        // The way in wears the kind of the hole on the other side, because it IS that hole seen
        // from this end -- and it arrives spent, which is what a passage is.
        holes.push_back({{"to", {{"room", from}, {"hole", fromHole + was.shift(from)}}},
                         {"kind", was.kindAt(from, fromHole)},
                         {"opened", true},
                         {"cleared", true},
                         {"killed", 0}});

    const std::size_t held =
        std::max({child.size(), cleared.size(), opened.size(), kind.size(), killed.size()});
    for (std::size_t h = 0; h < held; ++h)
    {
        // Where a hole leads, in the new numbering: its far floor, entered at that floor's own
        // way in -- which is hole zero wherever one exists.
        const int to = h < child.size() ? child[h] : -1;
        holes.push_back({{"to", {{"room", to}, {"hole", to >= 0 && was.shift(to) == 1 ? 0 : -1}}},
                         {"kind", h < kind.size() ? kind[h] : std::string{}},
                         // v1 HAD NO `opened`: a hole pressed the moment the floor was built,
                         // so a document that never carried the field means every hole was
                         // open, not none of them. Reading a missing array as false invents a
                         // hole that is spent yet was never opened.
                         {"opened", h < opened.size() ? opened[h] : true},
                         {"cleared", h < cleared.size() && cleared[h]},
                         {"killed", h < killed.size() ? killed[h] : 0}});
    }

    f["way_in"] = from >= 0 ? 0 : -1;
    f["holes"] = std::move(holes);
    for (const char* gone : {"child", "cleared", "opened", "kind", "killed", "from", "from_hole",
                             "parent", "parent_hole"})
        f.erase(gone);
}

void foldHoles(nlohmann::json& lives)
{
    for (auto& life : lives)
    {
        if (!life.is_object() || !life["descent"].is_array())
            continue;
        auto& floors = life["descent"];
        const Legacy was = readLegacy(floors);
        for (auto& f : floors)
            foldFloor(f, was);
    }
}

// v2 -> v3. The words the game uses for its own parts were tightened and the files moved with
// them: a hole's kind lived under config/seeps, a room's kind under config/floors, a pest under
// config/pests. A save holds those paths VERBATIM -- in a hole's kind, a room's type, every
// key of the record -- so a file written before the move points at directories that are not
// there any more, and a room whose kind cannot be read builds no chambers at all. That loads as
// a blank map rather than as an error, which is the worst way for it to fail.
//
// Rewritten wherever a string sits, keys included, because the record is keyed BY path. Adding
// a row is how any future move is carried: the walk itself never needs to know what moved.
void movePaths(nlohmann::json& doc)
{
    static constexpr std::array<std::pair<const char*, const char*>, 3> kMoved{{
        {"config/seeps/", "config/holes/"},
        {"config/floors/", "config/rooms/"},
        {"config/creatures/", "config/pests/"},
    }};
    // NOTE TO ANY FUTURE SWEEP: the strings on the LEFT are history. They name directories that
    // no longer exist and must never be "corrected" to the current spelling -- a row mapping a
    // path to itself silently stops migrating, and the save it fails to carry is somebody's.
    const auto moved = [](std::string s)
    {
        for (const auto& [was, now] : kMoved)
            if (s.rfind(was, 0) == 0)
                return std::string(now) + s.substr(std::string(was).size());
        return s;
    };
    if (doc.is_string())
    {
        doc = moved(doc.get<std::string>());
        return;
    }
    if (doc.is_array())
    {
        for (auto& item : doc)
            movePaths(item);
        return;
    }
    if (!doc.is_object())
        return;
    nlohmann::json rebuilt = nlohmann::json::object();
    for (auto& [key, value] : doc.items())
    {
        movePaths(value);
        rebuilt[moved(key)] = value;
    }
    doc = std::move(rebuilt);
}

// v3 -> v4. REPAIR OF AN IMPOSSIBLE STATE, left by the fold above before it read a missing
// `opened` correctly: holes marked spent that were never opened and never took anything. No
// version of this game can produce that -- a hole is spent by killing its way through, so what
// is spent was opened, and what was opened has a tally. The room it happens to belong to reads
// as finished the moment it is entered, which is silent rather than loud.
//
// A hole that is spent AND opened is left alone whatever its tally: the way he came in is
// exactly that, and it never had a program of its own.
void repairSpentHoles(nlohmann::json& lives)
{
    int mended = 0;
    for (auto& life : lives)
    {
        if (!life.is_object() || !life["descent"].is_array())
            continue;
        for (auto& room : life["descent"])
        {
            if (!room.is_object() || !room["holes"].is_array())
                continue;
            const int wayIn = room.value("way_in", -1);
            for (std::size_t h = 0; h < room["holes"].size(); ++h)
            {
                nlohmann::json& hole = room["holes"][h];
                if (static_cast<int>(h) == wayIn || !hole.value("cleared", false) ||
                    hole.value("opened", false) || hole.value("killed", 0) != 0)
                    continue;
                hole["cleared"] = false; // never opened, never fought: it is sealed, as it was
                ++mended;
            }
        }
    }
    if (mended > 0)
        poe::log().info("save: {} hole(s) were marked spent without ever being opened -- sealed",
                        mended);
}

// Carry an older document forward. Runs AFTER the read, never during it: a decoder that
// migrates is a decoder that has to know every shape there has ever been.
void migrate(nlohmann::json& doc)
{
    const int from = doc.value("schema_version", 0);
    if (from == kSchemaVersion)
        return;
    poe::log().info("save: carrying a version {} file forward to {}", from, kSchemaVersion);
    if (from < 2 && doc["lives"].is_array())
        foldHoles(doc["lives"]);
    if (from < 3 && doc["lives"].is_array())
        movePaths(doc["lives"]);
    if (from < 4 && doc["lives"].is_array())
        repairSpentHoles(doc["lives"]);
    doc["schema_version"] = kSchemaVersion;
}

std::string resolve(const std::string& path)
{
    return path.empty() ? engine::save::path(kOrgName, kAppName) : path;
}

} // namespace

File load(const std::string& path)
{
    auto doc = engine::save::readJson(resolve(path));
    if (!doc.has_value() || !doc->is_object())
        return File{};
    migrate(*doc);

    File file;
    file.schema_version = doc->value("schema_version", kSchemaVersion);
    file.minted = doc->value("minted", 0);
    for (const auto& life : doc->value("lives", nlohmann::json::array()))
        file.lives.push_back(decodeData(life));
    return file;
}

bool save(const File& file, const std::string& path)
{
    nlohmann::json lives = nlohmann::json::array();
    for (const auto& life : file.lives)
        lives.push_back(encode(life));
    const nlohmann::json doc{
        {"schema_version", kSchemaVersion}, {"minted", file.minted}, {"lives", lives}};
    return engine::save::writeJson(doc, resolve(path));
}

bool exists(const std::string& path)
{
    return !load(path).lives.empty();
}

} // namespace savegame
