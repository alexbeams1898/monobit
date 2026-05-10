#pragma once

#include <nlohmann/json.hpp>

#include <string>
#include <unordered_map>

namespace selva::anim
{

// Per-clip locomotion metadata. `blend_in_seconds` = inertialization
// decay duration when this clip becomes the active loco_current.
// (Per-joint decay scales further by offset magnitude on top of this
// base.) JSON config: games/selva-oscura/config/locomotion.json.
struct LocomotionClipConfig
{
    std::string name;
    float blend_in_seconds = 0.20f;
};
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(LocomotionClipConfig, name, blend_in_seconds);

// Top-level config wrapping the clip list. Wrapped (vs naked array) so
// we can add more locomotion-wide tunables later without changing the
// file shape.
struct LocomotionConfigFile
{
    std::vector<LocomotionClipConfig> clips;
};
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(LocomotionConfigFile, clips);

// Runtime registry. Loaded once at startup; queried in the per-frame
// update before calling sSampler.update().
struct LocomotionConfig
{
    std::unordered_map<std::string, float> blend_in_by_clip;

    // Load from a JSON file. Returns true on success; on failure, the
    // map stays empty and queries fall through to the default. Logs to
    // stderr.
    bool loadFromFile(const std::string& path);

    // Returns the per-clip blend-in duration, or `default_seconds` if
    // the clip isn't in the registry. Callers pass tun.anim_blend_seconds
    // as the fallback so non-locomotion clips (or unconfigured ones)
    // still get a sensible value.
    float blendInSeconds(const std::string& clip_name, float default_seconds) const;
};

} // namespace selva::anim
