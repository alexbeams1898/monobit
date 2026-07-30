#include "UnlockCondition.h"

#include <nlohmann/json.hpp>

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

bool clauseHolds(const Clause& c, const Knowledge& k)
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
    // The negative half, LAST: a prefixed name that must not be held. Unprefixed
    // reads as a flag (the common case).
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

bool satisfied(const Condition& cond, const Knowledge& k)
{
    if (cond.any.empty())
        return true; // unconditional
    for (const auto& c : cond.any)
        if (clauseHolds(c, k))
            return true;
    return false;
}

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
        stringList(cj, "without", c.without);
        if (const auto it = cj.find("stat"); it != cj.end() && it->is_object())
            for (const auto& [name, lvl] : it->items())
                c.stat[name] = lvl.get<int>();
        cond.any.push_back(std::move(c));
    }
    return cond;
}

} // namespace unlock
