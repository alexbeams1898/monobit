#include "Crafting.h"

#include "JsonConfig.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <filesystem>
#include <fstream>

namespace crafting
{
namespace
{
Ingredient parseIngredient(const nlohmann::json& j)
{
    Ingredient in;
    in.item = j.value("item", std::string{});
    in.qty = std::max(1, j.value("qty", 1));
    return in;
}

Recipe parseRecipe(const nlohmann::json& j, const std::string& fallbackId)
{
    Recipe r;
    r.id = j.value("id", fallbackId);
    if (const auto ins = j.find("inputs"); ins != j.end() && ins->is_array())
        for (const auto& i : *ins)
            if (i.is_object())
            {
                Ingredient parsed = parseIngredient(i);
                if (!parsed.item.empty())
                    r.inputs.push_back(std::move(parsed));
            }
    r.output_item = j.value("output_item", std::string{});
    r.output_qty = std::max(1, j.value("output_qty", 1));
    r.kind = j.value("kind", std::string{}) == "permanent" ? OutputKind::Permanent
                                                           : OutputKind::Consumable;
    if (const auto it = j.find("unlock_when"); it != j.end())
        r.unlock_when = unlock::parseCondition(*it);
    if (const auto s = j.find("scaling"); s != j.end() && s->is_object())
    {
        r.scaling.stat = s->value("stat", std::string{});
        r.scaling.base = s->value("base", 1.0f);
    }
    r.xp_stat = j.value("xp_stat", std::string{});
    r.xp_base = j.value("xp_base", 0);
    r.reveals_flag = j.value("reveals_flag", std::string{});
    return r;
}

// The set of distinct item types a recipe requires.
std::unordered_set<std::string> requiredTypes(const Recipe& r)
{
    std::unordered_set<std::string> out;
    for (const auto& in : r.inputs)
        out.insert(in.item);
    return out;
}
} // namespace

void load(Registry& out, const std::string& dir)
{
    namespace fs = std::filesystem;
    std::error_code ec;
    if (!fs::exists(dir, ec) || !fs::is_directory(dir, ec))
        return;

    for (const auto& entry : fs::directory_iterator(dir, ec))
    {
        if (!entry.is_regular_file() || entry.path().extension() != ".json")
            continue;
        std::ifstream f(entry.path());
        if (!f)
            continue;
        const nlohmann::json j = nlohmann::json::parse(f, nullptr, /*allow_exceptions=*/false);
        if (j.is_discarded() || !j.is_object())
            continue;
        Recipe r = parseRecipe(j, entry.path().stem().string());
        if (!r.id.empty() && !r.output_item.empty())
            out.recipes[r.id] = std::move(r);
    }
}

void loadConfig(Config& cfg, const std::string& path)
{
    const auto loaded = config::load(path);
    if (!loaded)
        return;
    const nlohmann::json& j = *loaded;
    cfg.quality_per_stat = j.value("quality_per_stat", cfg.quality_per_stat);
    cfg.quality_luck = j.value("quality_luck", cfg.quality_luck);
    cfg.quality_max = j.value("quality_max", cfg.quality_max);
    cfg.xp_reach_gain = j.value("xp_reach_gain", cfg.xp_reach_gain);
    cfg.xp_mastery_falloff = j.value("xp_mastery_falloff", cfg.xp_mastery_falloff);
    cfg.xp_floor = j.value("xp_floor", cfg.xp_floor);
    cfg.xp_ceiling = j.value("xp_ceiling", cfg.xp_ceiling);
}

bool attemptable(const Recipe& recipe, const unlock::Knowledge& k)
{
    return unlock::satisfied(recipe.unlock_when, k);
}

Match match(const std::vector<std::string>& selectedTypes, const Registry& registry,
            const unlock::Knowledge& k)
{
    const std::unordered_set<std::string> sel(selectedTypes.begin(), selectedTypes.end());
    Match best;
    for (const auto& [id, r] : registry.recipes)
    {
        if (!attemptable(r, k))
            continue;
        const std::unordered_set<std::string> req = requiredTypes(r);
        // Exact type-set match: every required type is selected AND nothing extra is selected.
        if (sel.size() == req.size())
        {
            bool exact = true;
            for (const auto& t : req)
                if (!sel.count(t))
                {
                    exact = false;
                    break;
                }
            if (exact)
            {
                best.recipe = &r;
                return best; // an exact match wins outright
            }
        }
        // Closeness: fraction of the recipe's required types the attempt covers. Tracks the
        // nearest for the near-miss signal.
        int have = 0;
        for (const auto& t : req)
            if (sel.count(t))
                ++have;
        const float frac =
            req.empty() ? 0.0f : static_cast<float>(have) / static_cast<float>(req.size());
        if (frac > best.closeness)
        {
            best.closeness = frac;
            best.nearest = &r;
        }
    }
    return best;
}

float outcomeQuality(const Recipe& recipe, int craftStat, const observations::RollRng& rng,
                     const Config& cfg)
{
    if (recipe.kind == OutputKind::Permanent)
        return 1.0f; // a key is a key -- no scaling
    // base + stat scaling + a bounded luck roll (in [0, quality_luck]).
    const float statPart = cfg.quality_per_stat * static_cast<float>(std::max(0, craftStat));
    const int luckSteps = 100;
    const float luck = cfg.quality_luck * static_cast<float>(rng(luckSteps)) /
                       static_cast<float>(luckSteps); // rng in [0,100] -> [0, quality_luck]
    const float q = recipe.scaling.base + statPart + luck;
    return std::clamp(q, 0.0f, cfg.quality_max);
}

int xpGained(const Recipe& recipe, int craftStat, float quality, const Config& cfg)
{
    if (recipe.xp_base <= 0 || recipe.xp_stat.empty())
        return 0;
    // Inverse-mastery: a good outcome relative to a low craft stat pays more; routine work
    // (high craft stat) pays little. Never zero (a mastered craft still trickles).
    const float factor = std::clamp(
        cfg.xp_reach_gain * quality - cfg.xp_mastery_falloff * static_cast<float>(craftStat) + 1.0f,
        cfg.xp_floor, cfg.xp_ceiling);
    return std::max(1, static_cast<int>(static_cast<float>(recipe.xp_base) * factor + 0.5f));
}

Outcome craft(const Recipe& recipe, inventory::Satchel& satchel, const inventory::Registry& items,
              int craftStat, const observations::RollRng& rng, const Config& cfg, State& state)
{
    Outcome out;
    // Verify all inputs are present before consuming any (all-or-nothing).
    for (const auto& in : recipe.inputs)
        if (inventory::count(satchel, in.item) < in.qty)
            return out; // made = false; nothing changed
    for (const auto& in : recipe.inputs)
        inventory::remove(satchel, in.item, in.qty);

    out.made = true;
    out.output_item = recipe.output_item;
    out.output_qty = recipe.output_qty;
    out.quality = outcomeQuality(recipe, craftStat, rng, cfg);
    out.xp_stat = recipe.xp_stat;
    out.xp = xpGained(recipe, craftStat, out.quality, cfg);
    inventory::add(satchel, items, inventory::ItemInstance{recipe.output_item, recipe.output_qty});

    // Discovery + first-craft reveal. `known` records the recipe (realized by making it); the
    // reveal flag fires only the FIRST time (opens new understanding -- the two-way loop).
    out.first_time = state.known.insert(recipe.id).second;
    if (out.first_time && !recipe.reveals_flag.empty())
        out.revealed_flag = recipe.reveals_flag;
    return out;
}

} // namespace crafting
