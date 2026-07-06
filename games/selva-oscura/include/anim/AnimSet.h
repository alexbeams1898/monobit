#pragma once

#include "anim/AnimIntent.h"

#include <array>
#include <string>
#include <string_view>

namespace selva::anim
{

// Intent -> clip-key resolution table for one variant of a character
// (e.g. humanoid_unarmed, humanoid_sword_and_shield). The set is
// loaded once at startup from JSON and held by pointer on each actor;
// equipment-change code swaps the pointer to switch variants.
//
// Load-time validation asserts every AnimIntent value has a non-empty
// clip key, so a missing JSON entry fails loud at startup rather than
// silently in a combat encounter.
class AnimSet
{
  public:
    // Load from JSON at `path`. Returns true on success. On failure
    // (file missing, parse error, missing intent, unknown intent name)
    // logs to stderr and returns false; the set stays in its prior
    // state.
    bool loadFromFile(const std::string& path);

    // Clip key for an intent. The key is the engine-side clip name
    // (e.g. "jogging", "sword_and_shield_run") -- the same key the
    // existing skeletal-clip registry uses. Empty for COUNT.
    std::string_view clipKey(AnimIntent intent) const;

    // Stable id from the JSON (e.g. "humanoid_unarmed"). Used by debug
    // logs + equipment-change code that looks up sets by id.
    const std::string& id() const
    {
        return set_id_;
    }

    // Skeleton this set's clips were baked against (e.g.
    // "humanoid_male"). Caller can sanity-check against the actor's
    // skeleton id before swapping.
    const std::string& skeletonId() const
    {
        return skeleton_id_;
    }

  private:
    std::string set_id_;
    std::string skeleton_id_;
    std::array<std::string, static_cast<size_t>(AnimIntent::COUNT)> clip_by_intent_;
};

} // namespace selva::anim
