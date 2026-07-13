#include "UnlockCondition.h"

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

} // namespace unlock
