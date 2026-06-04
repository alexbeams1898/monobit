#include "dialog/Handlers.h"

#include <cstdio>
#include <unordered_map>

namespace selva::dialog
{

namespace
{

std::unordered_map<std::string, ConditionFn>& conditionMap()
{
    static std::unordered_map<std::string, ConditionFn> m;
    return m;
}

std::unordered_map<std::string, ActionFn>& actionMap()
{
    static std::unordered_map<std::string, ActionFn> m;
    return m;
}

} // namespace

void registerConditionHandler(const std::string& key, ConditionFn fn)
{
    if (conditionMap().count(key) > 0)
    {
        std::fprintf(stderr, "[dialog-handlers] condition '%s' re-registered\n", key.c_str());
        std::fflush(stderr);
    }
    conditionMap()[key] = std::move(fn);
}

void registerActionHandler(const std::string& key, ActionFn fn)
{
    if (actionMap().count(key) > 0)
    {
        std::fprintf(stderr, "[dialog-handlers] action '%s' re-registered\n", key.c_str());
        std::fflush(stderr);
    }
    actionMap()[key] = std::move(fn);
}

const ConditionFn* getConditionHandler(const std::string& key)
{
    const auto it = conditionMap().find(key);
    return (it == conditionMap().end()) ? nullptr : &it->second;
}

const ActionFn* getActionHandler(const std::string& key)
{
    const auto it = actionMap().find(key);
    return (it == actionMap().end()) ? nullptr : &it->second;
}

} // namespace selva::dialog
