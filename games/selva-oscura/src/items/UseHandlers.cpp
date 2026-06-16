#include "items/UseHandlers.h"

#include "AppState.h"
#include "AppStateGlobal.h"
#include "items/ItemRegistry.h"
#include "ops/InventoryOps.h"
#include "ui/Notifications.h"

#include <glm/vec4.hpp>

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

UseResult useItem(engine::ecs::Inventory& inv, engine::ecs::ItemInstanceId item_id)
{
    UseResult r;
    const engine::ecs::ItemInstance* inst = engine::ops::inventory::findById(inv, item_id);
    if (inst == nullptr)
        return r;
    const std::string config_path = inst->config_path;

    const ItemExtensions* ext = itemExtensions(config_path);
    if (ext == nullptr || ext->use_handler.empty())
        return r;
    const auto* action = getUseAction(ext->use_handler);
    if (action == nullptr)
        return r;

    r = (*action)(inv, item_id);

    if (r.fired)
    {
        selva::PlayerProfile* profile = selva::activePlayerProfile();
        if (profile != nullptr)
            engine::ops::inventory::consumeItems(inv, profile->equipment, config_path, 1);
        if (!r.success_message.empty())
        {
            constexpr glm::vec4 kSuccessColor{0.85f, 0.85f, 0.85f, 1.0f}; // soft gray
            selva::ui::pushNotification(r.success_message, kSuccessColor, std::string{});
        }
    }
    else if (!r.rejection_reason.empty())
    {
        // Rejection toast: muted red so it reads as "this thing didn't
        // happen" rather than "you took damage" (bright red would
        // alarm).
        constexpr glm::vec4 kRejectColor{0.75f, 0.45f, 0.45f, 1.0f};
        selva::ui::pushNotification(r.rejection_reason, kRejectColor, std::string{});
    }
    return r;
}

} // namespace selva::items
