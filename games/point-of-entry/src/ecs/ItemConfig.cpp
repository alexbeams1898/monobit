#include "ecs/ItemConfig.h"

#include "ops/LogUtils.h"

#include <nlohmann/json.hpp>

#include <filesystem>
#include <fstream>
#include <unordered_map>

namespace items
{
namespace
{
std::unordered_map<std::string, ItemDef> sItems;
const ItemDef kMissing{};

Rarity parseRarity(const std::string& text)
{
    if (text == "uncommon")
        return Rarity::Uncommon;
    if (text == "rare")
        return Rarity::Rare;
    if (text == "exceptional")
        return Rarity::Exceptional;
    return Rarity::Common;
}
} // namespace

void load(const std::string& dir)
{
    sItems.clear();
    std::error_code ec;
    if (!std::filesystem::is_directory(dir, ec))
    {
        poe::log().error("items: no directory at '{}'", dir);
        return;
    }
    for (const auto& entry : std::filesystem::recursive_directory_iterator(dir, ec))
    {
        if (!entry.is_regular_file() || entry.path().extension() != ".json")
            continue;
        std::ifstream in(entry.path());
        const nlohmann::json j = nlohmann::json::parse(in, nullptr, /*allow_exceptions=*/false);
        if (j.is_discarded())
        {
            poe::log().error("items: '{}' is not valid JSON", entry.path().string());
            continue;
        }
        ItemDef def;
        // Identity is the config-relative path with forward slashes, so drop tables read the
        // same on every platform.
        def.path = entry.path().generic_string();
        def.name = j.value("name", std::string{"?"});
        def.rarity = parseRarity(j.value("rarity", std::string{"common"}));
        def.value = j.value("value", 1);
        def.ok = true;
        sItems.emplace(def.path, std::move(def));
    }
    poe::log().info("items: {} defined", sItems.size());
}

const ItemDef& get(const std::string& path)
{
    const auto it = sItems.find(path);
    if (it == sItems.end())
    {
        poe::log().error("items: nothing defined at '{}'", path);
        return kMissing;
    }
    return it->second;
}

} // namespace items
