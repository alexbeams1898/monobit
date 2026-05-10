#pragma once

#include <nlohmann/json.hpp>

#include <string>
#include <unordered_map>

namespace selva::anim
{

// Per-clip locomotion metadata.
//   `blend_in_seconds`: how long the crossfade INTO this clip lasts.
//   `family`: pose-family tag used by the cross-family blend
//      extender. Clips in the same family have similar t=0 poses
//      (e.g. all gait clips share neutral arms / mid-stride legs);
//      transitions WITHIN a family are smooth at the default
//      blend, transitions ACROSS families need the longer
//      cross_family_min_blend_seconds. Common values: "gait",
//      "idle". Empty = "no family" (matches everything; no
//      cross-family bump).
//
// JSON config: games/selva-oscura/config/locomotion.json.
struct LocomotionClipConfig
{
    std::string name;
    float blend_in_seconds = 0.20f;
    std::string family;
};
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(LocomotionClipConfig, name, blend_in_seconds,
                                                family);

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
    std::unordered_map<std::string, std::string> family_by_clip;

    // Load from a JSON file. Returns true on success; on failure, the
    // map stays empty and queries fall through to the default. Logs to
    // stderr.
    bool loadFromFile(const std::string& path);

    // Returns the per-clip blend-in duration, or `default_seconds` if
    // the clip isn't in the registry. Callers pass tun.anim_blend_seconds
    // as the fallback so non-locomotion clips (or unconfigured ones)
    // still get a sensible value.
    float blendInSeconds(const std::string& clip_name, float default_seconds) const;

    // Returns the family tag for `clip_name`, or empty string if the
    // clip isn't tagged. Used by the cross-family blend extender to
    // detect idle <-> gait transitions; empty family means no
    // cross-family bump (clip transitions to/from this clip use the
    // default blend).
    const std::string& family(const std::string& clip_name) const;
};

} // namespace selva::anim
