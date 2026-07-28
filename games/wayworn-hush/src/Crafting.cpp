#include "Crafting.h"

#include "JsonConfig.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cmath>
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
    if (const auto s = j.find("scaling"); s != j.end() && s->is_object())
    {
        r.scaling.stat = s->value("stat", std::string{});
        r.scaling.base = s->value("base", 1.0f);
    }
    // "xp_stats": {"craftsmanship": 8, "survival": 2} -- relative shares of the craft's XP.
    if (const auto it = j.find("xp_stats"); it != j.end() && it->is_object())
        for (const auto& [stat, weight] : it->items())
            if (weight.is_number())
                r.xp_stats.push_back(StatShare{stat, weight.get<float>()});
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
    cfg.learn_faculty = j.value("learn_faculty", cfg.learn_faculty);
    cfg.default_xp_stat = j.value("default_xp_stat", cfg.default_xp_stat);
    cfg.learn_faculty_exp = j.value("learn_faculty_exp", cfg.learn_faculty_exp);
    cfg.learn_spirit_exp = j.value("learn_spirit_exp", cfg.learn_spirit_exp);
}

Match match(const std::vector<std::string>& selectedTypes, const Registry& registry)
{
    const std::unordered_set<std::string> sel(selectedTypes.begin(), selectedTypes.end());
    Match best;
    // Every recipe is always makeable -- the INGREDIENTS are the only requirement (Little-Alchemy:
    // the right things in the pot make the thing, whether or not you'd "learned" it). Learning is
    // just a record kept afterward (see State::known + the game's learnRecipe), never a gate here.
    for (const auto& [id, r] : registry.recipes)
    {
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

float outcomeQuality(const Recipe& recipe, int craftStat, const psyche::RollRng& rng,
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
    if (recipe.xp_base <= 0)
        return 0;
    // Inverse-mastery: a good outcome relative to a low craft stat pays more; routine work
    // (high craft stat) pays little. Never zero (a mastered craft still trickles).
    const float factor = std::clamp(
        cfg.xp_reach_gain * quality - cfg.xp_mastery_falloff * static_cast<float>(craftStat) + 1.0f,
        cfg.xp_floor, cfg.xp_ceiling);
    return static_cast<int>(std::max(1L, std::lround(static_cast<float>(recipe.xp_base) * factor)));
}

std::vector<std::pair<std::string, int>> splitXp(const Recipe& recipe, int total, const Config& cfg)
{
    std::vector<std::pair<std::string, int>> gains;
    if (total <= 0)
        return gains;
    if (recipe.xp_stats.empty())
    {
        if (!cfg.default_xp_stat.empty())
            gains.emplace_back(cfg.default_xp_stat, total);
        return gains;
    }

    float sum = 0.0f;
    for (const auto& s : recipe.xp_stats)
        if (!s.stat.empty() && s.weight > 0.0f)
            sum += s.weight;
    if (sum <= 0.0f)
        return gains;

    // Floor each share, then hand the leftover to the largest fractional parts, so the parts sum
    // to exactly `total` and a small-but-real share never floors away to zero.
    std::vector<float> frac;
    int assigned = 0;
    for (const auto& s : recipe.xp_stats)
    {
        if (s.stat.empty() || s.weight <= 0.0f)
            continue;
        const float exact = static_cast<float>(total) * s.weight / sum;
        const int whole = static_cast<int>(exact);
        gains.emplace_back(s.stat, whole);
        frac.push_back(exact - static_cast<float>(whole));
        assigned += whole;
    }
    for (int left = total - assigned; left > 0; --left)
    {
        std::size_t best = 0;
        for (std::size_t i = 1; i < frac.size(); ++i)
            if (frac[i] > frac[best])
                best = i;
        gains[best].second += 1;
        frac[best] = -1.0f; // spent -- don't win the next unit too
    }
    return gains;
}

Outcome craft(const Recipe& recipe, inventory::Satchel& satchel, const inventory::Registry& items,
              int craftStat, const psyche::RollRng& rng, const Config& cfg,
              const State& state)
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
    out.xp = xpGained(recipe, craftStat, out.quality, cfg);
    out.stat_gains = splitXp(recipe, out.xp, cfg);
    inventory::add(satchel, items, inventory::ItemInstance{recipe.output_item, recipe.output_qty});

    // first_time = the recipe wasn't known before this craft (a genuine discovery-by-making, as
    // opposed to re-making something already learned, or crafting one a deed taught). craft only
    // READS `known`; the caller owns marking a recipe learned (one place -- see learnRecipe), so
    // teaching-by-deed and discovery-by-craft can't double-insert or double-reward.
    out.first_time = state.known.count(recipe.id) == 0;
    if (out.first_time && !recipe.reveals_flag.empty())
        out.revealed_flag = recipe.reveals_flag;
    return out;
}

} // namespace crafting
