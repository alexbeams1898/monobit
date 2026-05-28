#include "combat/AttackResolution.h"

#include "Tunables.h"
#include "anim/AnimationClip.h"
#include "anim/ClipRegistry.h"
#include "anim/PoseSampler.h"
#include "combat/CombatLog.h"
#include "combat/WeaponClass.h"

#include <algorithm>
#include <cstdio>
#include <vector>

namespace selva::combat
{

namespace
{

// Find motion-watch joints for a single attack. Empty `names` falls
// back to the grip-default set (right hand always; both hands for
// two-handed). Names that don't resolve are silently dropped.
std::vector<int> resolveJoints(const std::vector<std::string>& names, bool two_handed,
                               const selva::anim::PoseSampler& sampler)
{
    std::vector<int> out;
    if (!names.empty())
    {
        out.reserve(names.size());
        for (const auto& n : names)
        {
            const int idx = sampler.findJoint(n.c_str());
            if (idx >= 0)
                out.push_back(idx);
        }
        return out;
    }
    const int rh = sampler.findJoint("mixamorig:RightHand");
    if (rh >= 0)
        out.push_back(rh);
    if (two_handed)
    {
        const int lh = sampler.findJoint("mixamorig:LeftHand");
        if (lh >= 0)
            out.push_back(lh);
    }
    return out;
}

// Compute resolved_cancel_open_seconds + resolved_chain_link_start_seconds
// for one attack. Uses JSON override if set (>= 0); otherwise auto-detects
// via clipJointMotionEnd / clipJointMotionStart.
void resolveOneAttack(WeaponAttack& atk, bool two_handed, const selva::anim::ClipRegistry& clips,
                      const selva::anim::PoseSampler& sampler, float velocity_frac)
{
    const auto* clip = clips.get(atk.clip);
    if (clip == nullptr || !clip->isLoaded())
    {
        atk.resolved_cancel_open_seconds = 0.0f;
        atk.resolved_chain_link_start_seconds = 0.0f;
        return;
    }
    const auto joints = resolveJoints(atk.motion_joints, two_handed, sampler);
    if (atk.cancel_open_seconds >= 0.0f)
        atk.resolved_cancel_open_seconds = std::min(atk.cancel_open_seconds, clip->duration());
    else
        atk.resolved_cancel_open_seconds =
            sampler.clipJointMotionEnd(*clip, joints, 60.0f, velocity_frac);
    if (atk.chain_link_start_seconds >= 0.0f)
    {
        atk.resolved_chain_link_start_seconds =
            std::min(atk.chain_link_start_seconds, clip->duration());
    }
    else
    {
        const float motion_start = sampler.clipJointMotionStart(*clip, joints);
        atk.resolved_chain_link_start_seconds =
            std::clamp(motion_start - 0.05f, 0.0f, clip->duration());
    }
}

void resolveTechniques(std::vector<WeaponTechnique>& techs, bool two_handed,
                       const selva::anim::ClipRegistry& clips,
                       const selva::anim::PoseSampler& sampler, float velocity_frac)
{
    for (auto& t : techs)
        for (auto& atk : t.attacks)
            resolveOneAttack(atk, two_handed, clips, sampler, velocity_frac);
}

// Per-attack log line: dur + motion_start/peak (clip-local seconds and
// fraction of duration) + cancel_open + chain_start + role.
void logChainEntry(std::size_t i, const WeaponAttack& atk, bool two_handed,
                   const selva::anim::ClipRegistry& clips, const selva::anim::PoseSampler& sampler)
{
    const auto* clip = clips.get(atk.clip);
    const float dur = (clip != nullptr && clip->isLoaded()) ? clip->duration() : 0.0f;
    const float open = atk.resolved_cancel_open_seconds;
    const float start = atk.resolved_chain_link_start_seconds;
    const float open_frac = (dur > 0.0f) ? (open / dur) : 0.0f;
    const float start_frac = (dur > 0.0f) ? (start / dur) : 0.0f;
    float motion_start = 0.0f;
    float motion_peak = 0.0f;
    if (clip != nullptr && clip->isLoaded())
    {
        const auto joints = resolveJoints(atk.motion_joints, two_handed, sampler);
        motion_start = sampler.clipJointMotionStart(*clip, joints);
        motion_peak = sampler.clipJointMotionPeak(*clip, joints);
    }
    const float ms_frac = (dur > 0.0f) ? (motion_start / dur) : 0.0f;
    const float mp_frac = (dur > 0.0f) ? (motion_peak / dur) : 0.0f;
    combatLog("  [{}] {:<30} dur={:.3f}s  motion_start={:.3f}s ({:.0f}%)  "
              "peak={:.3f}s ({:.0f}%)  cancel_open={:.3f}s ({:.0f}%)  "
              "chain_start={:.3f}s ({:.0f}%)  role={}",
              i, atk.clip, dur, motion_start, ms_frac * 100.0f, motion_peak, mp_frac * 100.0f, open,
              open_frac * 100.0f, start, start_frac * 100.0f, i == 0 ? "first" : "chain-link");
}

void logChain(const char* class_id, const char* slot, const std::vector<WeaponAttack>& chain,
              bool two_handed, const selva::anim::ClipRegistry& clips,
              const selva::anim::PoseSampler& sampler)
{
    if (chain.empty())
        return;
    combatLog("[combat:resolve] {}/{} chain ({} entries):", class_id, slot, chain.size());
    for (std::size_t i = 0; i < chain.size(); ++i)
        logChainEntry(i, chain[i], two_handed, clips, sampler);
}

// Right-hand world-position delta between the previous attack's
// cancel-open frame and the next attack's chain-link-start frame.
// Big delta = pose mismatch at the splice; rebookending the source
// clip pulls these to ~0.
void logBookendAlignment(const char* class_id, const char* slot,
                         const std::vector<WeaponAttack>& chain,
                         const selva::anim::ClipRegistry& clips,
                         const selva::anim::PoseSampler& sampler)
{
    const int rh = sampler.findJoint("mixamorig:RightHand");
    if (rh < 0)
        return;
    for (std::size_t i = 1; i < chain.size(); ++i)
    {
        const auto& prev = chain[i - 1];
        const auto& cur = chain[i];
        const auto* p_clip = clips.get(prev.clip);
        const auto* c_clip = clips.get(cur.clip);
        if (p_clip == nullptr || !p_clip->isLoaded() || c_clip == nullptr || !c_clip->isLoaded())
            continue;
        const glm::vec3 prev_rh =
            sampler.sampleJointWorldPos(*p_clip, prev.resolved_cancel_open_seconds, rh);
        const glm::vec3 cur_rh =
            sampler.sampleJointWorldPos(*c_clip, cur.resolved_chain_link_start_seconds, rh);
        const glm::vec3 d = cur_rh - prev_rh;
        combatLog("[combat:bookend] {}/{} [{}->{}]  prev={} @ {:.3f}s RH=({:.3f},{:.3f},{:.3f})  "
                  "next={} @ {:.3f}s RH=({:.3f},{:.3f},{:.3f})  "
                  "delta=({:+.3f},{:+.3f},{:+.3f}) |{:.3f}m|",
                  class_id, slot, i - 1, i, prev.clip, prev.resolved_cancel_open_seconds, prev_rh.x,
                  prev_rh.y, prev_rh.z, cur.clip, cur.resolved_chain_link_start_seconds, cur_rh.x,
                  cur_rh.y, cur_rh.z, d.x, d.y, d.z, glm::length(d));
    }
}

void logTechniques(const char* class_id, const char* slot,
                   const std::vector<WeaponTechnique>& techs, bool two_handed,
                   const selva::anim::ClipRegistry& clips, const selva::anim::PoseSampler& sampler)
{
    for (std::size_t ti = 0; ti < techs.size(); ++ti)
    {
        char label[64];
        std::snprintf(label, sizeof(label), "%s [%zu:%s]", slot, ti, techs[ti].id.c_str());
        logChain(class_id, label, techs[ti].attacks, two_handed, clips, sampler);
        logBookendAlignment(class_id, label, techs[ti].attacks, clips, sampler);
    }
}

// Auto-detect the block clip's trim window. block_clip_start: drop
// leading idle frames. block_clip_end: where the hands settle into
// the peak-block pose (so a held block freezes there, not at the
// clip's neutral end-pose).
void resolveBlockClipStart(WeaponClass& cls, const selva::anim::ClipRegistry& clips,
                           const selva::anim::PoseSampler& sampler)
{
    const bool need_start = cls.block_clip_start_seconds < 0.0f;
    const bool need_end = cls.block_clip_end_seconds < 0.0f;
    if (!need_start && !need_end)
        return;
    const char* block_clip_name = (cls.id == "unarmed") ? "unarmed_block" : nullptr;
    if (block_clip_name == nullptr)
        return;
    const auto* bclip = clips.get(block_clip_name);
    if (bclip == nullptr || !bclip->isLoaded())
        return;
    const int rh = sampler.findJoint("mixamorig:RightHand");
    const int lh = sampler.findJoint("mixamorig:LeftHand");
    std::vector<int> joints;
    if (rh >= 0)
        joints.push_back(rh);
    if (lh >= 0)
        joints.push_back(lh);
    const float dur = bclip->duration();
    if (need_start)
    {
        const float motion_start = sampler.clipJointMotionStart(*bclip, joints);
        cls.block_clip_start_seconds = std::clamp(motion_start - 0.05f, 0.0f, dur);
    }
    if (need_end)
    {
        const float motion_end = sampler.clipJointMotionEnd(*bclip, joints);
        cls.block_clip_end_seconds = std::clamp(motion_end, 0.0f, dur);
    }
    combatLog("[combat:resolve] {} block trim: start={:.3f}s end={:.3f}s (clip duration={:.3f}s)",
              cls.id, cls.block_clip_start_seconds, cls.block_clip_end_seconds, dur);
}

void resolveOneClass(WeaponClass& cls, const selva::anim::ClipRegistry& clips,
                     const selva::anim::PoseSampler& sampler, float velocity_frac)
{
    resolveTechniques(cls.one_handed.light, false, clips, sampler, velocity_frac);
    resolveTechniques(cls.one_handed.heavy, false, clips, sampler, velocity_frac);
    resolveTechniques(cls.one_handed.running, false, clips, sampler, velocity_frac);
    resolveTechniques(cls.two_handed.light, true, clips, sampler, velocity_frac);
    resolveTechniques(cls.two_handed.heavy, true, clips, sampler, velocity_frac);
    resolveTechniques(cls.two_handed.running, true, clips, sampler, velocity_frac);

    resolveBlockClipStart(cls, clips, sampler);

    logTechniques(cls.id.c_str(), "1H light", cls.one_handed.light, false, clips, sampler);
    logTechniques(cls.id.c_str(), "1H heavy", cls.one_handed.heavy, false, clips, sampler);
    logTechniques(cls.id.c_str(), "1H running", cls.one_handed.running, false, clips, sampler);
    logTechniques(cls.id.c_str(), "2H light", cls.two_handed.light, true, clips, sampler);
    logTechniques(cls.id.c_str(), "2H heavy", cls.two_handed.heavy, true, clips, sampler);
    logTechniques(cls.id.c_str(), "2H running", cls.two_handed.running, true, clips, sampler);
}

} // namespace

void resolveAttackCancelOpenTimes(WeaponClassRegistry& weapon_classes,
                                  const selva::anim::ClipRegistry& clips,
                                  const selva::anim::PoseSampler& sampler)
{
    const float velocity_frac = selva::tuning::current().cancel_open_velocity_fraction;
    for (auto& kv : weapon_classes.by_id)
        resolveOneClass(kv.second, clips, sampler, velocity_frac);
}

} // namespace selva::combat
