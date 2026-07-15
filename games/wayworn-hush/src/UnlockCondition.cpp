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
    if (!c.flag.empty() && !k.has(k.flags, c.flag))
        return false;
    for (const auto& [name, level] : c.stat)
        if (k.stat(name) < level)
            return false;
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
    for (const auto& cj : j)
    {
        Clause c;
        c.flag = cj.value("flag", std::string{});
        // `observed` accepts a single string or an array (all required).
        if (const auto it = cj.find("observed"); it != cj.end())
        {
            if (it->is_string())
                c.observed.push_back(it->get<std::string>());
            else if (it->is_array())
                for (const auto& v : *it)
                    c.observed.push_back(v.get<std::string>());
        }
        if (const auto it = cj.find("stat"); it != cj.end() && it->is_object())
            for (const auto& [name, lvl] : it->items())
                c.stat[name] = lvl.get<int>();
        cond.any.push_back(std::move(c));
    }
    return cond;
}

} // namespace unlock
