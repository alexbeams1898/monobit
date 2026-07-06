#include "gameplay/Appearance.h"

#include "gl/SrgbColor.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cstdio>
#include <filesystem>
#include <fstream>

namespace selva::gameplay
{

const char* bodyTypeSkeletonId(BodyType t)
{
    switch (t)
    {
    case BodyType::Type2:
        return "humanoid_female";
    case BodyType::Type1:
    default:
        return "humanoid_male";
    }
}

float* appearanceFieldByName(Appearance& a, const std::string& id)
{
    if (id == "body_scale")
        return &a.body_scale;
    if (id == "head_scale")
        return &a.head_scale;
    if (id == "arm_scale")
        return &a.arm_scale;
    if (id == "leg_scale")
        return &a.leg_scale;
    if (id == "torso_scale")
        return &a.torso_scale;
    return nullptr;
}

const float* appearanceFieldByName(const Appearance& a, const std::string& id)
{
    // Same lookup; cast-away-const + re-add to share the table.
    return appearanceFieldByName(const_cast<Appearance&>(a), id);
}

float* appearanceMorphWeight(Appearance& a, const std::string& morph_name)
{
    // operator[] inserts a default-constructed (0.0f) entry if missing,
    // which is the contract -- callers immediately drag the slider,
    // mutating the freshly-created 0.0 entry.
    return &a.morph_weights[morph_name];
}

float appearanceMorphWeight(const Appearance& a, const std::string& morph_name)
{
    const auto it = a.morph_weights.find(morph_name);
    return it == a.morph_weights.end() ? 0.0f : it->second;
}

namespace
{

// Read an sRGB-authored 3-tuple, clamp per channel to [0, 1], and
// linearize for the shader. Callers pass a JSON array node that's
// already confirmed to be a 3-element array of numbers. `log_source`
// / `log_key` fire only when a clamp actually happens -- authoring
// error signal, not noise on the normal path.
glm::vec3 readClampedSrgbTriple(const nlohmann::json& j_arr, const char* log_source,
                                const char* log_key)
{
    glm::vec3 srgb(0.0f);
    for (int i = 0; i < 3; ++i)
    {
        const float raw = j_arr[i].get<float>();
        const float clamped = std::clamp(raw, 0.0f, 1.0f);
        if (clamped != raw)
        {
            std::fprintf(stderr,
                         "[appearance] %s: %s[%d]=%.3f clamped to %.3f "
                         "(expected normalized 0..1)\n",
                         log_source, log_key, i, raw, clamped);
            std::fflush(stderr);
        }
        srgb[i] = clamped;
    }
    return engine::gl::linearize(srgb);
}

} // namespace

void readAppearanceFromJson(const nlohmann::json& j, Appearance& out,
                            const std::string& source_desc)
{
    if (j.contains("body_type") && j["body_type"].is_number_integer())
    {
        const int v = j["body_type"].get<int>();
        out.body_type = (v == 2) ? BodyType::Type2 : BodyType::Type1;
    }
    if (j.contains("body_scale") && j["body_scale"].is_number())
        out.body_scale = j["body_scale"].get<float>();
    if (j.contains("head_scale") && j["head_scale"].is_number())
        out.head_scale = j["head_scale"].get<float>();
    if (j.contains("arm_scale") && j["arm_scale"].is_number())
        out.arm_scale = j["arm_scale"].get<float>();
    if (j.contains("leg_scale") && j["leg_scale"].is_number())
        out.leg_scale = j["leg_scale"].get<float>();
    if (j.contains("torso_scale") && j["torso_scale"].is_number())
        out.torso_scale = j["torso_scale"].get<float>();
    if (j.contains("hair_style_id") && j["hair_style_id"].is_string())
        out.hair_style_id = j["hair_style_id"].get<std::string>();
    if (j.contains("hair_tint") && j["hair_tint"].is_array() && j["hair_tint"].size() == 3)
        out.hair_tint = readClampedSrgbTriple(j["hair_tint"], source_desc.c_str(), "hair_tint");
    if (j.contains("eye_tint") && j["eye_tint"].is_array() && j["eye_tint"].size() == 3)
        out.eye_tint = readClampedSrgbTriple(j["eye_tint"], source_desc.c_str(), "eye_tint");
    if (j.contains("morphs") && j["morphs"].is_object())
    {
        // Vertex-morph weights. Keys are glTF morph-target names; values
        // are floats (typically [0,1]). Missing in JSON = absent in map
        // = weight 0 at draw time. No clamping -- the shader tolerates
        // any float (negative subtracts the morph, >1 over-applies).
        for (auto it = j["morphs"].begin(); it != j["morphs"].end(); ++it)
            if (it.value().is_number())
                out.morph_weights[it.key()] = it.value().get<float>();
    }
    if (j.contains("color") && j["color"].is_array() && j["color"].size() == 3)
    {
        const glm::vec3 linear = readClampedSrgbTriple(j["color"], source_desc.c_str(), "color");
        out.color.x = linear.x;
        out.color.y = linear.y;
        out.color.z = linear.z;
    }
}

void writeAppearanceToJson(const Appearance& appearance, nlohmann::json& out)
{
    out["body_type"] = static_cast<int>(appearance.body_type);
    out["body_scale"] = appearance.body_scale;
    out["head_scale"] = appearance.head_scale;
    out["arm_scale"] = appearance.arm_scale;
    out["leg_scale"] = appearance.leg_scale;
    out["torso_scale"] = appearance.torso_scale;
    out["color"] = {appearance.color.x, appearance.color.y, appearance.color.z};
    if (!appearance.hair_style_id.empty())
        out["hair_style_id"] = appearance.hair_style_id;
    out["hair_tint"] = {appearance.hair_tint.x, appearance.hair_tint.y, appearance.hair_tint.z};
    out["eye_tint"] = {appearance.eye_tint.x, appearance.eye_tint.y, appearance.eye_tint.z};
    if (!appearance.morph_weights.empty())
    {
        nlohmann::json morphs = nlohmann::json::object();
        for (const auto& kv : appearance.morph_weights)
            morphs[kv.first] = kv.second;
        out["morphs"] = std::move(morphs);
    }
}

Appearance loadAppearance(const std::string& path)
{
    Appearance out;
    if (path.empty())
        return out;
    if (!std::filesystem::exists(path))
    {
        std::fprintf(stderr, "[appearance] config not found: %s (using default body_scale=1.0)\n",
                     path.c_str());
        std::fflush(stderr);
        return out;
    }
    std::ifstream f(path);
    if (!f.is_open())
    {
        std::fprintf(stderr, "[appearance] failed to open: %s\n", path.c_str());
        std::fflush(stderr);
        return out;
    }
    nlohmann::json j;
    try
    {
        f >> j;
    }
    catch (const std::exception& e)
    {
        std::fprintf(stderr, "[appearance] parse error in %s: %s\n", path.c_str(), e.what());
        std::fflush(stderr);
        return out;
    }
    readAppearanceFromJson(j, out, path);
    return out;
}

bool saveAppearance(const std::string& path, const Appearance& appearance)
{
    if (path.empty())
    {
        std::fprintf(stderr, "[appearance] save called with empty path\n");
        std::fflush(stderr);
        return false;
    }
    nlohmann::json j;
    writeAppearanceToJson(appearance, j);
    std::ofstream f(path);
    if (!f.is_open())
    {
        std::fprintf(stderr, "[appearance] failed to open for write: %s\n", path.c_str());
        std::fflush(stderr);
        return false;
    }
    f << j.dump(2) << '\n';
    return true;
}

namespace
{

// Process-wide cache for transform snapshots. Snapshot files are
// authored and immutable at runtime; loading them per-frame would
// be silly. Keyed by path; cleared never (assets live for process
// lifetime, like the rest of selva's config-loaded data).
//
// Not thread-safe -- selva's tick is single-threaded; if that
// changes the map needs a lock around the find+emplace.
std::unordered_map<std::string, Appearance>& snapshotCache()
{
    static std::unordered_map<std::string, Appearance> cache;
    return cache;
}

const Appearance& cachedSnapshot(const std::string& path)
{
    auto& cache = snapshotCache();
    const auto it = cache.find(path);
    if (it != cache.end())
        return it->second;
    return cache.emplace(path, loadAppearance(path)).first->second;
}

// Per-axis lerp helper. Strength=0 -> base unchanged; strength=1 ->
// target. Used for scalar axes (body_scale, head_scale, ...).
inline float lerpAxis(float base, float target, float strength)
{
    return base + (target - base) * strength;
}

inline glm::vec3 lerpVec3(const glm::vec3& base, const glm::vec3& target, float strength)
{
    return base + (target - base) * strength;
}

} // namespace

Appearance resolveLiveAppearance(const Appearance& base)
{
    if (base.transforms.empty())
        return base; // fast path -- no transforms, render the base

    // Start from a copy of the base, then fold each active transform's
    // delta on top. This composes multiple transforms additively per
    // axis (e.g. sangue_bloat at strength 0.5 plus violence_muscle at
    // strength 0.3 both write to torso_scale; their deltas sum).
    //
    // Trade-off note: this is "additive over base" not "additive over
    // previous transform" -- each transform's delta is computed
    // against the BASE, not against the running accumulator. This
    // means transforms commute (order-independent), which matches the
    // reconciler's "transforms list is a set, not a sequence" model.
    // The cost is that two transforms each writing torso_scale=+0.5
    // sum to +1.0 over base (not +1.5 which an over-previous compose
    // would give). The clamping pass at the slider registry catches
    // out-of-range sums.
    Appearance out = base;

    for (const auto& xf : base.transforms)
    {
        if (xf.strength <= 1e-4f || xf.target_snapshot_path.empty())
            continue;
        const Appearance& target = cachedSnapshot(xf.target_snapshot_path);
        const float s = xf.strength;

        // Bone scales -- target stores absolute values; lerp from base
        // (which is `out`'s starting state, i.e. the pristine base
        // before any other transform's delta) toward target.
        out.body_scale += (target.body_scale - base.body_scale) * s;
        out.head_scale += (target.head_scale - base.head_scale) * s;
        out.arm_scale += (target.arm_scale - base.arm_scale) * s;
        out.leg_scale += (target.leg_scale - base.leg_scale) * s;
        out.torso_scale += (target.torso_scale - base.torso_scale) * s;

        // Color tint -- per-channel lerp.
        out.color = out.color + (target.color - base.color) * s;
        // Hair tint -- per-channel lerp, same shape as body color.
        // hair_style_id is discrete and never lerps (base's style
        // wins; transforms can't cross-fade between hair meshes).
        out.hair_tint = out.hair_tint + (target.hair_tint - base.hair_tint) * s;
        // Eye tint -- per-channel lerp, same shape as hair tint.
        out.eye_tint = out.eye_tint + (target.eye_tint - base.eye_tint) * s;

        // Morph weights -- key-union lerp. For every key the target
        // snapshot mentions, push the resolved weight toward
        // (base[key] + s * (target[key] - base[key])). Keys the
        // target doesn't mention are left at the base's value (the
        // "snapshot only touches the axes it lists" contract).
        for (const auto& [name, target_w] : target.morph_weights)
        {
            const auto base_it = base.morph_weights.find(name);
            const float base_w = base_it == base.morph_weights.end() ? 0.0f : base_it->second;
            out.morph_weights[name] += (target_w - base_w) * s;
        }

        // body_type is discrete; transforms cannot lerp across
        // skeletons. The base body_type wins. (If a transform needs
        // to swap skeletons -- e.g. a full archetype swap -- it's
        // handled by applyArchetypeSwap, not by the resolver.)
    }

    return out;
}

} // namespace selva::gameplay
