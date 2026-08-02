#include "UnlockCondition.h"

#include <nlohmann/json.hpp>

#include <string>

namespace unlock
{

bool Knowledge::has(const std::unordered_set<std::string>* set, const std::string& id) const
{
    return set != nullptr && set->find(id) != set->end();
}

int Knowledge::stat(const std::string& name) const
{
    if (stats == nullptr)
        return 0;
    const auto it = stats->find(name);
    return it != stats->end() ? it->second : 0;
}

namespace
{
// What he has done and become: everything the clause names must be true of him.
bool hasEverything(const Clause& c, const Knowledge& k)
{
    for (const auto& id : c.observed)
        if (!k.has(k.observed, id))
            return false;
    for (const auto& f : c.flags)
        if (!k.has(k.flags, f))
            return false;
    for (const auto& item : c.carrying)
        if (!k.has(k.carrying, item))
            return false;
    for (const auto& [name, level] : c.stat)
        if (k.stat(name) < level)
            return false;
    return c.holding.empty() || c.holding == k.holding;
}

// When: a window of the day (from > to wraps midnight) and a stretch of days.
// An unset bound is no bound at all.
bool inSeason(const Clause& c, const Knowledge& k)
{
    if (c.from >= 0.0 && c.to >= 0.0)
    {
        const bool inside = c.from <= c.to ? (k.day_frac >= c.from && k.day_frac < c.to)
                                           : (k.day_frac >= c.from || k.day_frac < c.to);
        if (!inside)
            return false;
    }
    return !((c.day_min > 0 && k.day < c.day_min) || (c.day_max > 0 && k.day > c.day_max));
}

// The negative half: a prefixed name that must NOT be held. Unprefixed reads as
// a flag (the common case).
bool holdsNothingForbidden(const Clause& c, const Knowledge& k)
{
    for (const auto& id : c.without)
    {
        const bool held = id.rfind("item:", 0) == 0   ? k.has(k.carrying, id.substr(5))
                          : id.rfind("obs:", 0) == 0  ? k.has(k.observed, id.substr(4))
                          : id.rfind("flag:", 0) == 0 ? k.has(k.flags, id.substr(5))
                                                      : k.has(k.flags, id);
        if (held)
            return false;
    }
    return true;
}
} // namespace

bool clauseHolds(const Clause& c, const Knowledge& k)
{
    return hasEverything(c, k) && inSeason(c, k) && holdsNothingForbidden(c, k);
}

bool satisfied(const Condition& cond, const Knowledge& k)
{
    if (cond.any.empty())
        return true; // unconditional
    for (const auto& c : cond.any)
        if (clauseHolds(c, k))
            return true;
    return false;
}

namespace
{
// A time as a fraction of the day: "HH:MM" on the 24-hour face, or a raw 0..1
// number. Parsed here rather than in worldclock because this primitive must stay
// free of the clock -- the caller hands it fractions; this only has to read the
// same authored spelling. -1 on anything unreadable.
double parseDayFraction(const nlohmann::json& v)
{
    if (v.is_number())
        return v.get<double>();
    if (!v.is_string())
        return -1.0;
    const std::string s = v.get<std::string>();
    const std::size_t colon = s.find(':');
    if (colon == std::string::npos)
        return -1.0;
    try
    {
        const int h = std::stoi(s.substr(0, colon));
        const int m = std::stoi(s.substr(colon + 1));
        if (h == 24 && m == 0)
            return 1.0;
        if (h < 0 || h > 23 || m < 0 || m > 59)
            return -1.0;
        return (h + m / 60.0) / 24.0;
    }
    catch (...)
    {
        return -1.0;
    }
}
} // namespace

Condition parseCondition(const nlohmann::json& j)
{
    Condition cond;
    if (!j.is_array())
        return cond;
    // `observed` and `flag` each accept a single string or an array (all required).
    const auto stringList =
        [](const nlohmann::json& cj, const char* key, std::vector<std::string>& out)
    {
        const auto it = cj.find(key);
        if (it == cj.end())
            return;
        if (it->is_string())
            out.push_back(it->get<std::string>());
        else if (it->is_array())
            for (const auto& v : *it)
                out.push_back(v.get<std::string>());
    };
    for (const auto& cj : j)
    {
        Clause c;
        stringList(cj, "flag", c.flags);
        stringList(cj, "observed", c.observed);
        stringList(cj, "carrying", c.carrying);
        c.holding = cj.value("holding", std::string{});
        stringList(cj, "without", c.without);
        // A window authored as {"between": ["06:00", "20:00"]}, and day bounds as
        // {"day_min": 2} / {"day_max": 3}. Times parse through the ONE reader in
        // worldclock -- but unlock:: must not depend on the clock, so the caller
        // passes fractions and this accepts either form: "HH:MM" or a raw fraction.
        if (const auto it = cj.find("between"); it != cj.end() && it->is_array() && it->size() == 2)
        {
            c.from = parseDayFraction((*it)[0]);
            c.to = parseDayFraction((*it)[1]);
        }
        c.day_min = cj.value("day_min", 0);
        c.day_max = cj.value("day_max", 0);
        if (const auto it = cj.find("stat"); it != cj.end() && it->is_object())
            for (const auto& [name, lvl] : it->items())
                c.stat[name] = lvl.get<int>();
        cond.any.push_back(std::move(c));
    }
    return cond;
}

} // namespace unlock
