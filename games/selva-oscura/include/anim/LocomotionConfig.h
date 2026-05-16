#pragma once

#include <nlohmann/json.hpp>

#include <string>
#include <unordered_map>

namespace selva::anim
{

// How world translation is produced while this clip is the active
// loco track. The two values represent the only two valid patterns
// in the engine; a third option (mixed) is what produces the
// treadmill bug — see feedback_hip_delta_two_sides.md.
enum class TranslationSource
{
    // Default for unspecified clips. World translation comes from
    // `velocity_xz * dt`, calibrated by `walk_speed` / `run_speed`
    // tunables to match the clip's authored cadence (`walking` is
    // hand-tuned at 1.6m/s to match its 1.85m/1.16s hip travel).
    // The 1.5m hip-path threshold in extractTrackHipDelta decides
    // whether to extract+zero the hip (path >= 1.5m → extract;
    // smaller → leave in pose so micro-sway is visible).
    Velocity,
    // World translation comes from PoseSampler::consumedHipDelta(),
    // applied to actor pos every frame. Hip is ALWAYS extract+zeroed
    // regardless of path length. velocity_xz is forced to zero while
    // this is the active loco source so it doesn't fight the clip's
    // authored motion. Used by every AI actor's locomotion, by all
    // one-shots (dodge/attack), and by lock-on combat directional
    // walks — wherever clip cadence IS the design intent for world
    // speed.
    RootMotion,
};

// Parse "velocity" / "root_motion" → enum. Unknown values default
// to Velocity and log a warning (loadFromFile fires the log).
TranslationSource parseTranslationSource(const std::string& s);
const char* translationSourceName(TranslationSource src);

// Per-clip locomotion metadata. `blend_in_seconds` = inertialization
// decay duration when this clip becomes the active loco_current.
// (Per-joint decay scales further by offset magnitude on top of this
// base.) `translation_source` declares whether this clip drives the
// world or is driven by it (see TranslationSource above). Omit to
// default to "velocity" — matches the legacy walking/running
// behavior. JSON config: games/selva-oscura/config/locomotion.json.
struct LocomotionClipConfig
{
    std::string name;
    float blend_in_seconds = 0.20f;
    std::string translation_source = "velocity";
};
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(LocomotionClipConfig, name, blend_in_seconds,
                                                translation_source);

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
    std::unordered_map<std::string, TranslationSource> source_by_clip;
    // Global loco playback rate. Mirror of tun.loco_playback_rate;
    // gameplay's per-frame tick keeps this in sync so the sampler
    // (which doesn't reach into Tunables) has a single source.
    float global_playback_rate = 1.0f;

    // Load from a JSON file. Returns true on success; on failure, the
    // map stays empty and queries fall through to the default. Logs to
    // stderr.
    bool loadFromFile(const std::string& path);

    // Returns the per-clip blend-in duration, or `default_seconds` if
    // the clip isn't in the registry. Callers pass tun.anim_blend_seconds
    // as the fallback so non-locomotion clips (or unconfigured ones)
    // still get a sensible value.
    float blendInSeconds(const std::string& clip_name, float default_seconds) const;

    // Per-clip translation source. Missing entry returns the default
    // (Velocity) — matches legacy behavior for clips not in the JSON.
    TranslationSource translationSource(const std::string& clip_name) const;
};

} // namespace selva::anim
