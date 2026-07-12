#include "Observations.h"

#include <nlohmann/json.hpp>

#include <cmath>
#include <fstream>

namespace observations
{
namespace
{
// A thing counts as "faced" if it's within radius and roughly in the direction
// the player is looking. cos(60deg) = 0.5 -> a 120-degree forward cone; generous
// enough that casual facing works, tight enough that you don't observe what's
// behind you.
constexpr float kFacingCosThreshold = 0.5f;

bool allObserved(const State& s, const std::vector<std::string>& ids)
{
    for (const auto& id : ids)
        if (s.observed.find(id) == s.observed.end())
            return false; // any missing -> not all observed
    return true;
}

// The observable the player faces, or nullptr. Nearest faced one wins.
const Observable* facedObservable(const State& s, float px, float py, float dx, float dy)
{
    const float dlen = std::sqrt(dx * dx + dy * dy);
    if (dlen <= 0.0f)
        return nullptr;
    const float ndx = dx / dlen;
    const float ndy = dy / dlen;

    const Observable* best = nullptr;
    float bestDist = 0.0f;
    for (const auto& o : s.observables)
    {
        const float ox = o.x - px;
        const float oy = o.y - py;
        const float dist = std::sqrt(ox * ox + oy * oy);
        if (dist > o.radius || dist <= 0.0f)
            continue;
        // Direction to the observable vs. where the player looks.
        if ((ox / dist) * ndx + (oy / dist) * ndy < kFacingCosThreshold)
            continue;
        if (!best || dist < bestDist)
        {
            best = &o;
            bestDist = dist;
        }
    }
    return best;
}

// Deepest tier index whose requires are all observed. Tier 0 is always eligible.
int deepestAvailableTier(const State& s, const Observable& o)
{
    for (int i = static_cast<int>(o.tiers.size()) - 1; i >= 0; --i)
        if (allObserved(s, o.tiers[static_cast<std::size_t>(i)].requires_ids))
            return i;
    return 0;
}

std::vector<std::string> parseIds(const nlohmann::json& e, const char* key)
{
    std::vector<std::string> out;
    if (const auto it = e.find(key); it != e.end() && it->is_array())
        for (const auto& v : *it)
            out.push_back(v.get<std::string>());
    return out;
}

// Form any conclusions whose requirements are now met (queue text). Returns the
// total Spirit EXP earned from conclusions formed this call (0 if none). Loops
// so a conclusion that completes another's requirements also forms this call.
int formConclusions(State& state)
{
    int earned = 0;
    bool changed = true;
    while (changed)
    {
        changed = false;
        for (const auto& c : state.conclusions)
        {
            if (state.formed.count(c.id))
                continue;
            if (!allObserved(state, c.requires_ids))
                continue;
            state.formed.insert(c.id);
            state.pending.push_back(c.text);
            earned += c.spirit_exp;
            changed = true;
        }
    }
    return earned;
}
} // namespace

void load(State& state, const std::string& path)
{
    state.observables.clear();
    state.conclusions.clear();
    std::ifstream f(path);
    if (!f)
        return;
    const nlohmann::json j = nlohmann::json::parse(f, nullptr, /*allow_exceptions=*/false);
    if (j.is_discarded())
        return;

    for (const auto& e : j.value("observables", nlohmann::json::array()))
    {
        Observable o;
        o.id = e.value("id", std::string{});
        o.x = e.value("x", 0.0f);
        o.y = e.value("y", 0.0f);
        o.radius = e.value("radius", 48.0f);
        if (const auto it = e.find("tiers"); it != e.end() && it->is_array())
        {
            for (const auto& t : *it)
            {
                Tier tier;
                tier.text = t.value("text", std::string{});
                tier.spirit_exp = t.value("spirit_exp", 0);
                tier.requires_ids = parseIds(t, "requires");
                o.tiers.push_back(std::move(tier));
            }
        }
        if (!o.id.empty() && !o.tiers.empty())
            state.observables.push_back(std::move(o));
    }

    for (const auto& e : j.value("conclusions", nlohmann::json::array()))
    {
        Conclusion c;
        c.id = e.value("id", std::string{});
        c.text = e.value("text", std::string{});
        c.spirit_exp = e.value("spirit_exp", 0);
        c.requires_ids = parseIds(e, "requires");
        if (!c.id.empty())
            state.conclusions.push_back(std::move(c));
    }
}

std::string facedId(const State& state, float px, float py, float dir_x, float dir_y)
{
    const Observable* o = facedObservable(state, px, py, dir_x, dir_y);
    return o ? o->id : std::string{};
}

bool facingObservable(const State& state, float px, float py, float dir_x, float dir_y)
{
    return facedObservable(state, px, py, dir_x, dir_y) != nullptr;
}

bool exhausted(const State& state, const std::string& observable_id)
{
    for (const auto& o : state.observables)
    {
        if (o.id != observable_id)
            continue;
        const auto it = state.observed.find(observable_id);
        if (it == state.observed.end())
            return false; // never observed -> not exhausted
        return it->second >= deepestAvailableTier(state, o);
    }
    return false;
}

ObserveResult observe(State& state, float px, float py, float dir_x, float dir_y)
{
    const Observable* o = facedObservable(state, px, py, dir_x, dir_y);
    if (!o)
        return {Outcome::None, 0};

    const int tier = deepestAvailableTier(state, *o);
    const auto prev = state.observed.find(o->id);
    const int prevTier = (prev == state.observed.end()) ? -1 : prev->second;

    // Always surface the thought for the reached tier.
    state.pending.push_back(o->tiers[static_cast<std::size_t>(tier)].text);

    if (tier <= prevTier)
        return {Outcome::Reobserved, 0}; // no new understanding, no Spirit EXP

    // Newly-reached (deeper) tier: record + earn its Spirit EXP.
    state.observed[o->id] = tier;
    int earned = o->tiers[static_cast<std::size_t>(tier)].spirit_exp;

    const int fromConclusions = formConclusions(state);
    earned += fromConclusions;
    return {fromConclusions > 0 ? Outcome::Conclusion : Outcome::NewTier, earned};
}

} // namespace observations
