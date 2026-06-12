#include "items/UseHandlers.h"

#include <cstdio>
#include <unordered_map>

namespace selva::items
{

namespace
{

std::unordered_map<std::string, UseActionFn>& actionMap()
{
    static std::unordered_map<std::string, UseActionFn> m;
    return m;
}

std::unordered_map<std::string, UseConditionFn>& conditionMap()
{
    static std::unordered_map<std::string, UseConditionFn> m;
    return m;
}

} // namespace

void registerUseAction(const std::string& key, UseActionFn fn)
{
    if (actionMap().count(key) > 0)
    {
        std::fprintf(stderr, "[use-handlers] action '%s' re-registered\n", key.c_str());
        std::fflush(stderr);
    }
    actionMap()[key] = std::move(fn);
}

void registerUseCondition(const std::string& key, UseConditionFn fn)
{
    if (conditionMap().count(key) > 0)
    {
        std::fprintf(stderr, "[use-handlers] condition '%s' re-registered\n", key.c_str());
        std::fflush(stderr);
    }
    conditionMap()[key] = std::move(fn);
}

const UseActionFn* getUseAction(const std::string& key)
{
    auto it = actionMap().find(key);
    return (it == actionMap().end()) ? nullptr : &it->second;
}

const UseConditionFn* getUseCondition(const std::string& key)
{
    auto it = conditionMap().find(key);
    return (it == conditionMap().end()) ? nullptr : &it->second;
}

} // namespace selva::items
