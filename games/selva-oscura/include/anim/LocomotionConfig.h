#pragma once

#include <nlohmann/json.hpp>

#include <string>
#include <unordered_map>

namespace selva::anim
{

// How world translation is produced while this clip is the active
// loco track or one-shot. Three explicit modes -- the data layer
// owns the choice per clip; the runtime never guesses. (Prior to
// the InPlace addition, an unauthored clip with hip path between
// 0.5m and 1.5m got auto-classified TRAVELING by a heuristic, and
// any "step into the bite" attack clip silently slid the actor
// forward each fire. The heuristic survives only as a fallback for
// clips with no JSON entry.)
enum class TranslationSource
{
    // World translation comes from `velocity_xz * dt`. Calibrated by
    // `walk_speed` / `jog_speed` / `sprint_speed` tunables to match the clip's
    // authored cadence (`walking` is hand-tuned at 1.6m/s to match
    // its 1.85m/1.16s hip travel). When no JSON entry exists, the
    // 1.5m hip-path heuristic in extractTrackHipDelta picks for you
    // -- but new clips should declare explicitly.
    Velocity,
    // World translation comes from PoseSampler::consumedHipDelta(),
    // applied to actor pos every frame. Hip is extract+zeroed each
    // frame. velocity_xz is forced to zero so it doesn't fight the
    // clip's authored motion. Used by directional walks, dodges,
    // attacks whose authored step IS the design intent (pounce
    // leap), wherever clip cadence drives world speed.
    RootMotion,
    // Hip stays in the pose, no extraction, no zeroing. World
    // translation does NOT come from this clip at all -- whatever
    // velocity_xz integrates is the only world motion. Used by
    // attacks whose authored hip travel is purely visual (a bite
    // where the head lunges forward but the body should NOT slide).
    // Different from Velocity: InPlace promises no extraction even
    // if hip path exceeds the heuristic threshold. Different from
    // RootMotion: velocity_xz is NOT zeroed (gameplay still drives
    // motion if it wants to).
    InPlace,
};

// Parse "velocity" / "root_motion" / "in_place" → enum. Unknown values
// default to Velocity and log a warning (loadFromFile fires the log).
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
    // Per-clip time scale. Multiplied with global_playback_rate; a
    // root-motion clip plays faster AND translates proportionally
    // faster (foot cadence stays locked to translation, no skating).
    // 1.0 = author cadence; >1 = faster.
    float playback_rate = 1.0f;
};
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(LocomotionClipConfig, name, blend_in_seconds,
                                                translation_source, playback_rate);

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
    std::unordered_map<std::string, float> playback_rate_by_clip;
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

    // Per-clip playback rate. Missing entry returns 1.0 (author cadence).
    // Combine with global_playback_rate at the consume site.
    float playbackRate(const std::string& clip_name) const;
};

} // namespace selva::anim
