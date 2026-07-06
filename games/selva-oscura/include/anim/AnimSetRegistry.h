#pragma once

#include "anim/AnimSet.h"

#include <memory>
#include <string>
#include <string_view>
#include <unordered_map>

namespace selva::anim
{

// Process-wide registry of named AnimSets. Loaded once at startup
// (loadFromDirectory scans a config dir for *.json), queried by id
// when an actor needs to swap its active set.
class AnimSetRegistry
{
  public:
    // Load every .json under dir as an AnimSet, key by set_id. Returns
    // count loaded (failures are logged + skipped). Repeatable; later
    // loads with the same set_id replace earlier ones.
    int loadFromDirectory(const std::string& dir);

    // Lookup by set_id (matches the "set_id" field in the JSON).
    // Returns nullptr if not present.
    const AnimSet* get(std::string_view set_id) const;

    // Singleton accessor; the registry is process-wide.
    static AnimSetRegistry& instance();

  private:
    std::unordered_map<std::string, std::unique_ptr<AnimSet>> sets_;
};

} // namespace selva::anim
