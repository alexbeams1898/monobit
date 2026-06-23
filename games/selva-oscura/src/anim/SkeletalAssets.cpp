#include "anim/SkeletalAssets.h"

#include "anim/AnimationClip.h"
#include "anim/ClipRegistry.h"
#include "anim/LocomotionConfig.h"
#include "anim/PoseSampler.h"
#include "anim/SkeletalMesh.h"
#include "anim/SkeletalRenderer.h"
#include "anim/Skeleton.h"
#include "anim/SkeletonJointMap.h"

#include <algorithm>
#include <cstdio>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

namespace selva::anim
{

namespace
{

// Per-skeleton bundle. The PLAYER bundle (key "player") always exists
// after initSkeletalAssets; non-player bundles (key "wolf" etc.) are
// loaded if their config + assets are present and skipped silently
// otherwise (their archetypes log a "missing skeleton" warning at
// spawn time, not at boot).
struct SkeletonBundle
{
    Skeleton skeleton;
    SkeletalMesh mesh;
    ClipRegistry clips;
};

// Construct-On-First-Use for all engine-wide anim singletons. File-
// scope globals were exposed to a static-init-order-fiasco bug:
// TuningPanel.cpp (and any other TU) binds `static ClipRegistry&
// sClips = clips()` at static-init time. With file-scope `sBundles`
// here in SkeletonAssets.cpp's TU, the order in which TuningPanel.cpp
// vs SkeletalAssets.cpp's statics initialize is undefined. The
// crashes we hit (null unique_ptr deref AND divide-by-zero in
// std::unordered_map mod-by-zero-buckets) were both this. Function-
// local statics fix it permanently -- the first call to the accessor
// constructs the global on demand, guaranteed to be ready before any
// caller observes it.
std::unordered_map<std::string, std::unique_ptr<SkeletonBundle>>& sBundles()
{
    static std::unordered_map<std::string, std::unique_ptr<SkeletonBundle>> m;
    return m;
}

// PoseSampler + LocomotionConfig remain process-wide singletons --
// the sampler instance per actor is constructed via createPoseSampler
// (Actor.h holds one per actor). These two are the GAME-WIDE shared
// bits: PoseSampler held here is the PLAYER's sampler (used by the
// existing sampler() accessor and a few render call sites);
// LocomotionConfig is shared across all clips regardless of which
// skeleton's clip it is (translation_source declarations are
// per-clip-name, name-spaces collapse).
PoseSampler& sSampler()
{
    static PoseSampler p;
    return p;
}

LocomotionConfig& sLocomotionConfig()
{
    static LocomotionConfig c;
    return c;
}

// Lazy-init the player bundle on first access. TuningPanel.cpp and
// other TUs bind file-scope `static ClipRegistry& sClips = clips()`
// references at static-init time, BEFORE initSkeletalAssets() has
// run. Pre-multi-skeleton the accessors returned default-constructed
// file-scope globals (always valid, just empty); we have to preserve
// that contract or the reference dangles. initSkeletalAssets later
// fills the SAME bundle's slots in-place (does NOT replace the
// unique_ptr), so the addresses TuningPanel captured stay valid.
SkeletonBundle& playerBundleLazy()
{
    auto& slot = sBundles()[std::string(kPlayerSkeletonKey)];
    if (!slot)
        slot = std::make_unique<SkeletonBundle>();
    return *slot;
}

SkeletonBundle& bundleOrFallback(const std::string& key)
{
    auto& bundles = sBundles();
    auto it = bundles.find(key);
    if (it != bundles.end() && it->second)
        return *it->second;
    // Fallback to player bundle, lazy-creating it if needed so callers
    // at static-init time get a valid (empty) bundle instead of a
    // null-deref crash.
    return playerBundleLazy();
}

// Per-skeleton load. id is the registry key + the asset folder name.
// assets live in assets/characters/<id>/ (skeleton.ozz + mesh .glb +
// clip *.ozz files). Returns true on success; false if any required
// piece is missing.
//
// Loads IN PLACE into the existing bundle slot if one already exists
// (the player's slot is lazy-created by bundleOrFallback at static-
// init time so file-scope references in TuningPanel etc. stay valid
// across the eventual real load).
bool loadBundle(const std::string& id, const std::string& mesh_filename)
{
    auto& slot = sBundles()[id];
    if (!slot)
        slot = std::make_unique<SkeletonBundle>();
    SkeletonBundle& bundle = *slot;
    const std::string folder = "assets/characters/" + id;
    bundle.skeleton = loadSkeleton((folder + "/skeleton.ozz").c_str());
    if (!bundle.skeleton.isLoaded())
    {
        std::fprintf(stderr, "[anim] skeleton.ozz missing for '%s' (path=%s)\n", id.c_str(),
                     folder.c_str());
        return false;
    }
    const int n_clips = bundle.clips.loadDirectory(folder);
    std::fprintf(stderr, "[anim] loaded %d clip(s) from %s\n", n_clips, folder.c_str());
    bundle.mesh = loadSkeletalMesh((folder + "/" + mesh_filename).c_str(), bundle.skeleton);
    if (!bundle.mesh.isLoaded())
    {
        std::fprintf(stderr, "[anim] mesh '%s' failed to load for '%s'\n", mesh_filename.c_str(),
                     id.c_str());
        return false;
    }
    return true;
}

} // namespace

Skeleton& skeleton()
{
    return bundleOrFallback(std::string(kPlayerSkeletonKey)).skeleton;
}

SkeletalMesh& playerMesh()
{
    return bundleOrFallback(std::string(kPlayerSkeletonKey)).mesh;
}

ClipRegistry& clips()
{
    return bundleOrFallback(std::string(kPlayerSkeletonKey)).clips;
}

Skeleton& skeletonByKey(const std::string& key)
{
    return bundleOrFallback(key).skeleton;
}

SkeletalMesh& meshByKey(const std::string& key)
{
    return bundleOrFallback(key).mesh;
}

ClipRegistry& clipsByKey(const std::string& key)
{
    return bundleOrFallback(key).clips;
}

bool hasSkeleton(const std::string& key)
{
    auto& bundles = sBundles();
    return bundles.find(key) != bundles.end();
}

PoseSampler& sampler()
{
    return sSampler();
}

LocomotionConfig& locomotionConfig()
{
    return sLocomotionConfig();
}

const AnimationClip* idleClip()
{
    return clips().get("standard_idle");
}

const AnimationClip* walkClip()
{
    return clips().get("walking");
}

const AnimationClip* runClip()
{
    return clips().get("jogging");
}

bool initSkeletalAssets()
{
    // Player humanoid bundle is required; non-player bundles are
    // optional (their archetype JSONs declare what to load, and
    // missing bundles are caught at spawn).
    if (!loadBundle(std::string(kPlayerSkeletonKey), "humanoid.glb"))
    {
        // Backward-compat: the on-disk layout puts the player rig at
        // assets/characters/humanoid/, not /player/. Try that path
        // before failing. Load IN PLACE into the already-allocated
        // player slot (see playerBundleLazy).
        auto& slot = sBundles()[std::string(kPlayerSkeletonKey)];
        if (!slot)
            slot = std::make_unique<SkeletonBundle>();
        SkeletonBundle& bundle = *slot;
        bundle.skeleton = loadSkeleton("assets/characters/humanoid/skeleton.ozz");
        if (!bundle.skeleton.isLoaded())
        {
            std::fprintf(stderr, "[anim] player skeleton.ozz missing -- character disabled\n");
            return false;
        }
        const int n = bundle.clips.loadDirectory("assets/characters/humanoid");
        std::fprintf(stderr, "[anim] loaded %d clip(s) from assets/characters/humanoid\n", n);
        // Larva clips (Scary Zombie Pack) are retargeted against the
        // humanoid skeleton at bake time, so they live in the
        // humanoid bundle's ClipRegistry. Larva archetypes reference
        // "zombie_walk" / etc. by name and lookupArchetypeClip
        // resolves them through this shared registry. Per
        // [[universal-humanoid-enemy-rule]] every Hell-side enemy
        // reuses the humanoid skeleton; the rig is shared, clips
        // are namespaced by name only.
        const int n_larva = bundle.clips.loadDirectory("assets/characters/larva");
        std::fprintf(stderr, "[anim] loaded %d clip(s) from assets/characters/larva\n", n_larva);
        bundle.mesh = loadSkeletalMesh("assets/characters/humanoid/humanoid.glb", bundle.skeleton);
        if (!bundle.mesh.isLoaded())
        {
            std::fprintf(stderr, "[anim] humanoid.glb failed to load -- character disabled\n");
            return false;
        }
    }
    if (idleClip() == nullptr || !idleClip()->isLoaded())
    {
        std::fprintf(stderr, "[anim] required idle clip missing -- character disabled\n");
        return false;
    }
    // Load per-skeleton joint maps BEFORE constructing PoseSamplers.
    // PoseSampler queries the player's joint map for hardcoded
    // semantic-joint lookups (hips, upleg_left/right, foot_left/right);
    // construction crashes if the map isn't ready.
    loadAllSkeletonJointMaps();
    sSampler() = createPoseSampler(skeleton(), playerMesh());
    if (!initSkeletalRenderer())
        return false;

    // Try to load any non-player skeletons named in the manifest.
    // assets/characters/<id>/ subdirs OTHER than humanoid/player are
    // candidate bundles. For v1 we hardcode the wolf attempt
    // (Phase B asset drop); when more skeletons land, generalize
    // to a config/skeletons/manifest.json listing.
    static const struct
    {
        const char* id;
        const char* mesh;
    } kExtraBundles[] = {
        {"wolf", "wolf.glb"},
    };
    for (const auto& b : kExtraBundles)
        loadBundle(b.id, b.mesh); // best-effort; logs internally

    sSampler().update(*idleClip(), 0.0f, 0.0f);
    return true;
}

void shutdownSkeletalAssets()
{
    shutdownSkeletalRenderer();
    sBundles().clear();
}

void auditClipHipMotion()
{
    const auto& playerClips = clips();
    if (playerClips.by_name.empty() || sSampler().bone_palette.empty())
        return;
    std::vector<std::string> names;
    names.reserve(playerClips.by_name.size());
    for (const auto& kv : playerClips.by_name)
        names.push_back(kv.first);
    std::sort(names.begin(), names.end());

    // Mirror the audit output to clip-audit.log so the data survives
    // across runs without scraping stderr. stderr is still useful for
    // build-time inspection in the IDE pane.
    FILE* audit_log = std::fopen("clip-audit.log", "w");
    // Generic format passthrough -- callers supply literal format strings.
    auto write = [&](const char* fmt, auto... args)
    {
        // NOLINTBEGIN(clang-diagnostic-format-security)
        std::fprintf(stderr, fmt, args...);
        if (audit_log != nullptr)
            std::fprintf(audit_log, fmt, args...);
        // NOLINTEND(clang-diagnostic-format-security)
    };

    write("[clip-audit] hip XZ path length per clip (full clip, not motion-end-clipped):\n");
    write("  %-40s %8s %8s %15s %s\n", "clip", "duration", "hip_path", "authored_speed", "source");
    const auto& cfg = locomotionConfig();
    for (const auto& name : names)
    {
        const auto* clip = playerClips.get(name);
        if (clip == nullptr || !clip->isLoaded())
            continue;
        const auto scan = sSampler().clipHipPathLength(*clip, 60.0f, 0.0f);
        const float dur = clip->duration();
        const float measured_speed = (dur > 1e-3f) ? scan.path_length / dur : 0.0f;
        write("  %-40s %7.2fs %7.3fm %12.3fm/s %s\n", name.c_str(), dur, scan.path_length,
              measured_speed, translationSourceName(cfg.translationSource(name)));
    }

    static const char* kProfileClips[] = {
        "stand_to_roll",
        "standing_dodge_backward",
        "falling_to_roll",
        "sword_and_shield_slash",
        "sword_and_shield_slash_2",
        "sword_and_shield_slash_3",
        "sword_and_shield_slash_4",
        "sword_and_shield_slash_5",
        "sword_and_shield_attack",
        "sword_and_shield_attack_2",
        "sword_and_shield_attack_3",
        "sword_and_shield_attack_4",
        "combat_walk_forward",
        "combat_walk_backward",
        "combat_strafe_left",
        "combat_strafe_right",
        "walking",
        "unarmed_block",
    };
    for (const char* nm : kProfileClips)
    {
        const auto* clip = playerClips.get(nm);
        if (clip == nullptr || !clip->isLoaded())
            continue;
        const float dur = clip->duration();
        write("[clip-profile] %s  dur=%.2fs  hip XZ trajectory:\n", nm, dur);
        write("  %4s %5s %8s %8s %10s %12s\n", "t%", "t(s)", "hip_x", "hip_z", "step",
              "cumul_dist");
        glm::vec2 prev(0.0f);
        float cumul = 0.0f;
        for (int i = 0; i <= 10; ++i)
        {
            const float t = (static_cast<float>(i) / 10.0f) * dur;
            const glm::vec2 hip = sSampler().sampleHipXZAt(*clip, t);
            const float step = (i == 0) ? 0.0f : glm::length(hip - prev);
            cumul += step;
            write("  %3d%% %5.2f %8.3f %8.3f %10.3f %12.3f\n", i * 10, t, hip.x, hip.y, step,
                  cumul);
            prev = hip;
        }
    }
    if (audit_log != nullptr)
        std::fclose(audit_log);
}

} // namespace selva::anim
