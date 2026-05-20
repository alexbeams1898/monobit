#include "combat/SpliceDiag.h"

#include "anim/ClipRegistry.h"
#include "combat/CombatLog.h"

#include <vector>

namespace selva::combat
{

namespace
{
glm::vec3 sLastRHWorldPos = glm::vec3(0.0f);
bool sLastRHValid = false;
} // namespace

SpliceDiag captureSpliceDiag(const selva::anim::PoseSampler& sampler)
{
    SpliceDiag d;
    d.rh = sampler.findJoint("mixamorig:RightHand");
    d.lh = sampler.findJoint("mixamorig:LeftHand");
    d.hp = sampler.findJoint("mixamorig:Hips");
    d.lf = sampler.findJoint("mixamorig:LeftFoot");
    d.rf = sampler.findJoint("mixamorig:RightFoot");
    if (d.rh >= 0)
        d.live_rh = sampler.jointWorldPos(d.rh);
    if (d.lh >= 0)
        d.live_lh = sampler.jointWorldPos(d.lh);
    if (d.hp >= 0)
        d.live_hp = sampler.jointWorldPos(d.hp);
    if (d.lf >= 0)
        d.live_lf = sampler.jointWorldPos(d.lf);
    if (d.rf >= 0)
        d.live_rf = sampler.jointWorldPos(d.rf);
    return d;
}

void logSpliceDiag(const SpliceDiag& d, const selva::anim::AnimationClip& new_clip,
                   float start_seconds, const char* prefix, const selva::anim::PoseSampler& sampler)
{
    auto entry = [&](int j)
    { return (j >= 0) ? sampler.sampleJointWorldPos(new_clip, start_seconds, j) : glm::vec3(0); };
    combatLog("%s  RH=%.3fm LH=%.3fm Hip=%.3fm LFoot=%.3fm RFoot=%.3fm\n", prefix,
              glm::length(entry(d.rh) - d.live_rh), glm::length(entry(d.lh) - d.live_lh),
              glm::length(entry(d.hp) - d.live_hp), glm::length(entry(d.lf) - d.live_lf),
              glm::length(entry(d.rf) - d.live_rf));
}

float poseMatchStartFromLoco(const selva::anim::AnimationClip& new_clip, float window_seconds,
                             const selva::anim::PoseSampler& sampler,
                             const selva::anim::ClipRegistry& /*clips*/,
                             const std::string& /*last_loco_clip_name*/)
{
    // Reference pose is the LIVE skinned pose, sampled per-joint in
    // world space. Was previously sampling a single clip at its
    // current time; that's wrong mid-crossfade because the live pose
    // is a blend of (loco_previous + loco_current) and matching
    // against either single clip's frame returns a garbage best_t.
    // Live pose is what's actually on screen; matching against that
    // produces the correct splice point regardless of which clips
    // are blending.
    std::vector<int> joints;
    std::vector<glm::vec3> ref;
    for (const char* name :
         {"mixamorig:RightHand", "mixamorig:LeftHand", "mixamorig:RightFoot", "mixamorig:LeftFoot"})
    {
        const int idx = sampler.findJoint(name);
        if (idx >= 0)
        {
            joints.push_back(idx);
            ref.push_back(sampler.jointWorldPos(idx));
        }
    }
    if (joints.empty())
        return 0.0f;
    return sampler.clipPoseMatchTime(ref, new_clip, joints, 0.0f, window_seconds);
}

bool isMovingLocoClip(const std::string& name)
{
    return name == "walking" || name == "running" || name == "run_to_stop";
}

void cacheRightHandPos(const selva::anim::PoseSampler& sampler)
{
    const int rh = sampler.findJoint("mixamorig:RightHand");
    if (rh >= 0)
    {
        sLastRHWorldPos = sampler.jointWorldPos(rh);
        sLastRHValid = true;
    }
}

glm::vec3 lastRightHandPos()
{
    return sLastRHWorldPos;
}

bool lastRightHandValid()
{
    return sLastRHValid;
}

} // namespace selva::combat
