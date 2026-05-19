#include "anim/SkeletalAssets.h"

#include "anim/AnimationClip.h"
#include "anim/ClipRegistry.h"
#include "anim/LocomotionConfig.h"
#include "anim/PoseSampler.h"
#include "anim/SkeletalMesh.h"
#include "anim/SkeletalRenderer.h"
#include "anim/Skeleton.h"

#include <algorithm>
#include <cstdio>
#include <vector>

namespace selva::anim
{

namespace
{

Skeleton sSkeleton;
SkeletalMesh sPlayerMesh;
ClipRegistry sClips;
PoseSampler sSampler;
LocomotionConfig sLocomotionConfig;

} // namespace

Skeleton& skeleton()
{
    return sSkeleton;
}

SkeletalMesh& playerMesh()
{
    return sPlayerMesh;
}

ClipRegistry& clips()
{
    return sClips;
}

PoseSampler& sampler()
{
    return sSampler;
}

LocomotionConfig& locomotionConfig()
{
    return sLocomotionConfig;
}

const AnimationClip* idleClip()
{
    return sClips.get("standard_idle");
}

const AnimationClip* walkClip()
{
    return sClips.get("walking");
}

const AnimationClip* runClip()
{
    return sClips.get("running");
}

bool initSkeletalAssets()
{
    sSkeleton = loadSkeleton("assets/characters/x_bot/skeleton.ozz");
    if (!sSkeleton.isLoaded())
        return false;
    const int n_clips = sClips.loadDirectory("assets/characters/x_bot");
    std::fprintf(stderr, "[anim] loaded %d clip(s) from assets/characters/x_bot\n", n_clips);
    if (idleClip() == nullptr || !idleClip()->isLoaded())
    {
        std::fprintf(stderr, "[anim] required idle clip missing — character disabled\n");
        return false;
    }
    sPlayerMesh = loadSkeletalMesh("assets/characters/x_bot/X_Bot.glb", sSkeleton);
    if (!sPlayerMesh.isLoaded())
        return false;
    sSampler = createPoseSampler(sSkeleton, sPlayerMesh);
    if (!initSkeletalRenderer())
        return false;

    sSampler.update(*idleClip(), 0.0f, 0.0f);
    // Inertialization scaling intentionally not configured — the
    // game never enrolls inertialization. The engine's defaults
    // remain (used only if a future caller explicitly opts in).
    // See feedback_animation_harmony_rule.md.
    return true;
}

void shutdownSkeletalAssets()
{
    shutdownSkeletalRenderer();
}

void auditClipHipMotion()
{
    if (sClips.by_name.empty() || sSampler.bone_palette.empty())
        return;
    std::vector<std::string> names;
    names.reserve(sClips.by_name.size());
    for (const auto& kv : sClips.by_name)
        names.push_back(kv.first);
    std::sort(names.begin(), names.end());

    // Mirror the audit output to clip-audit.log so the data survives
    // across runs without scraping stderr. stderr is still useful for
    // build-time inspection in the IDE pane.
    FILE* audit_log = std::fopen("clip-audit.log", "w");
    // Generic format passthrough — callers supply literal format strings.
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
        const auto* clip = sClips.get(name);
        if (clip == nullptr || !clip->isLoaded())
            continue;
        const auto scan = sSampler.clipHipPathLength(*clip, 60.0f, 0.0f);
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
        const auto* clip = sClips.get(nm);
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
            const glm::vec2 hip = sSampler.sampleHipXZAt(*clip, t);
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
