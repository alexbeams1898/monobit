#include "SaveGame.h"

#include "ops/LogUtils.h"
#include "utils/SaveFile.h"

#include <nlohmann/json.hpp>

namespace savegame
{
namespace
{

nlohmann::json encode(const descent::Floor& f)
{
    return nlohmann::json{{"area", f.area},
                          {"seed", f.seed},
                          {"depth", f.depth},
                          {"parent", f.parent},
                          {"parent_hole", f.parent_hole},
                          {"child", f.child},
                          {"cleared", f.cleared},
                          {"opened", f.opened},
                          {"kind", f.kind},
                          {"killed", f.killed}};
}

descent::Floor decodeFloor(const nlohmann::json& j)
{
    descent::Floor f;
    if (!j.is_object())
        return f;
    f.area = j.value("area", f.area);
    f.seed = j.value("seed", f.seed);
    f.depth = j.value("depth", f.depth);
    f.parent = j.value("parent", f.parent);
    f.parent_hole = j.value("parent_hole", f.parent_hole);
    f.child = j.value("child", std::vector<int>{});
    f.cleared = j.value("cleared", std::vector<bool>{});
    f.opened = j.value("opened", std::vector<bool>{});
    f.kind = j.value("kind", std::vector<std::string>{});
    f.killed = j.value("killed", std::vector<int>{});
    // A hole is a hole in both lists or the floor cannot answer for it.
    const std::size_t holes = std::max(
        {f.child.size(), f.cleared.size(), f.opened.size(), f.killed.size(), f.kind.size()});
    f.child.resize(holes, -1);
    f.cleared.resize(holes, false);
    f.opened.resize(holes, false);
    f.killed.resize(holes, 0);
    f.kind.resize(holes);
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
                           {{"node", d.where.node},
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
    d.where.node = where.value("node", -1);
    d.where.area = where.value("area", std::string{});
    d.where.x = where.value("x", 0.0f);
    d.where.y = where.value("y", 0.0f);
    d.where.stood = where.value("stood", false);
    return d;
}

// Carry an older document forward. Runs AFTER the read, never during it: a
// decoder that migrates is a decoder that has to know every shape there has
// ever been.
void migrate(nlohmann::json& doc)
{
    const int from = doc.value("schema_version", 0);
    if (from == kSchemaVersion)
        return;
    poe::log().info("save: carrying a version {} file forward to {}", from, kSchemaVersion);
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
