#pragma once

#include <functional>
#include <string>

namespace selva::dialog
{

// Custom show_when predicate. Returns true if the gate is OPEN
// (topic / choice should be visible / enabled). Used for conditions
// beyond flag set/unset (e.g. "player has item X", "evolution_stage
// >= L2"). Registered at boot via registerConditionHandler.
using ConditionFn = std::function<bool()>;

// Action handler for on_enter / on_exit / on_select. Called with the
// npc_id and topic_id context so a single handler can serve multiple
// topics if needed (rare). Most handlers ignore both args.
using ActionFn = std::function<void(const std::string& npc_id, const std::string& topic_id)>;

void registerConditionHandler(const std::string& key, ConditionFn fn);
void registerActionHandler(const std::string& key, ActionFn fn);

// Lookup. Returns nullptr if the key is unregistered. Dialog runtime
// uses these; unknown keys log loudly and no-op (no crash).
const ConditionFn* getConditionHandler(const std::string& key);
const ActionFn* getActionHandler(const std::string& key);

} // namespace selva::dialog
