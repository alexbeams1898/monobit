#include "combat/AttackResolution.h"

#include <algorithm>
#include <cstdio>
#include <vector>

#include "Tunables.h"
#include "anim/AnimationClip.h"
#include "anim/ClipRegistry.h"
#include "anim/PoseSampler.h"
#include "combat/CombatLog.h"
#include "combat/WeaponClass.h"

namespace selva::combat
{

void resolveAttackCancelOpenTimes(WeaponClassRegistry& weapon_classes,
                                  const selva::anim::ClipRegistry& clips,
                                  const selva::anim::PoseSampler& sampler)
{
    auto resolve_joints = [&](const std::vector<std::string>& names,
                              bool two_handed) -> std::vector<int>
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
    };

    const float velocity_frac = selva::tuning::current().cancel_open_velocity_fraction;
    auto resolve_chain = [&](std::vector<WeaponAttack>& chain, bool two_handed)
    {
        std::vector<std::vector<int>> joints_per_attack;
        joints_per_attack.reserve(chain.size());
        for (auto& atk : chain)
        {
            const auto* clip = clips.get(atk.clip);
            if (clip == nullptr || !clip->isLoaded())
            {
                atk.resolved_cancel_open_seconds = 0.0f;
                atk.resolved_chain_link_start_seconds = 0.0f;
                joints_per_attack.emplace_back();
                continue;
            }
            const auto joints = resolve_joints(atk.motion_joints, two_handed);
            joints_per_attack.push_back(joints);
            if (atk.cancel_open_seconds >= 0.0f)
                atk.resolved_cancel_open_seconds =
                    std::min(atk.cancel_open_seconds, clip->duration());
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
    };

    auto log_chain = [&](const char* class_id, const char* slot,
                         const std::vector<WeaponAttack>& chain, bool two_handed)
    {
        if (chain.empty())
            return;
        combatLog("[combat:resolve] %s/%s chain (%zu entries):\n", class_id, slot, chain.size());
        for (std::size_t i = 0; i < chain.size(); ++i)
        {
            const auto& atk = chain[i];
            const auto* clip = clips.get(atk.clip);
            const float dur = (clip != nullptr && clip->isLoaded()) ? clip->duration() : 0.0f;
            const float open = atk.resolved_cancel_open_seconds;
            const float start = atk.resolved_chain_link_start_seconds;
            const float open_frac = (dur > 0.0f) ? (open / dur) : 0.0f;
            const float start_frac = (dur > 0.0f) ? (start / dur) : 0.0f;
            const char* role = (i == 0) ? "first" : "chain-link";
            float motion_start = 0.0f;
            float motion_peak = 0.0f;
            if (clip != nullptr && clip->isLoaded())
            {
                const auto joints = resolve_joints(atk.motion_joints, two_handed);
                motion_start = sampler.clipJointMotionStart(*clip, joints);
                motion_peak = sampler.clipJointMotionPeak(*clip, joints);
            }
            const float ms_frac = (dur > 0.0f) ? (motion_start / dur) : 0.0f;
            const float mp_frac = (dur > 0.0f) ? (motion_peak / dur) : 0.0f;
            combatLog("  [%zu] %-30s dur=%.3fs  motion_start=%.3fs (%.0f%%)  "
                      "peak=%.3fs (%.0f%%)  cancel_open=%.3fs (%.0f%%)  "
                      "chain_start=%.3fs (%.0f%%)  role=%s\n",
                      i, atk.clip.c_str(), dur, motion_start, ms_frac * 100.0f, motion_peak,
                      mp_frac * 100.0f, open, open_frac * 100.0f, start, start_frac * 100.0f, role);
        }
    };

    auto log_bookend_alignment = [&](const char* class_id, const char* slot,
                                     const std::vector<WeaponAttack>& chain)
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
            if (p_clip == nullptr || !p_clip->isLoaded() || c_clip == nullptr ||
                !c_clip->isLoaded())
                continue;
            const glm::vec3 prev_rh =
                sampler.sampleJointWorldPos(*p_clip, prev.resolved_cancel_open_seconds, rh);
            const glm::vec3 cur_rh =
                sampler.sampleJointWorldPos(*c_clip, cur.resolved_chain_link_start_seconds, rh);
            const glm::vec3 d = cur_rh - prev_rh;
            combatLog("[combat:bookend] %s/%s [%zu->%zu]  prev=%s @ %.3fs RH=(%.3f,%.3f,%.3f)  "
                      "next=%s @ %.3fs RH=(%.3f,%.3f,%.3f)  delta=(%+.3f,%+.3f,%+.3f) |%.3fm|\n",
                      class_id, slot, i - 1, i, prev.clip.c_str(),
                      prev.resolved_cancel_open_seconds, prev_rh.x, prev_rh.y, prev_rh.z,
                      cur.clip.c_str(), cur.resolved_chain_link_start_seconds, cur_rh.x, cur_rh.y,
                      cur_rh.z, d.x, d.y, d.z, glm::length(d));
        }
    };

    auto resolve_techniques = [&](std::vector<WeaponTechnique>& techs, bool two_handed)
    {
        for (auto& t : techs)
            resolve_chain(t.attacks, two_handed);
    };

    for (auto& kv : weapon_classes.by_id)
    {
        auto& cls = kv.second;
        resolve_techniques(cls.one_handed.light, false);
        resolve_techniques(cls.one_handed.heavy, false);
        resolve_techniques(cls.one_handed.running, false);
        resolve_techniques(cls.two_handed.light, true);
        resolve_techniques(cls.two_handed.heavy, true);
        resolve_techniques(cls.two_handed.running, true);

        if (cls.block_clip_start_seconds < 0.0f)
        {
            const char* block_clip_name = (cls.id == "unarmed") ? "unarmed_block" : nullptr;
            if (block_clip_name != nullptr)
            {
                const auto* bclip = clips.get(block_clip_name);
                if (bclip != nullptr && bclip->isLoaded())
                {
                    const int rh = sampler.findJoint("mixamorig:RightHand");
                    const int lh = sampler.findJoint("mixamorig:LeftHand");
                    std::vector<int> joints;
                    if (rh >= 0)
                        joints.push_back(rh);
                    if (lh >= 0)
                        joints.push_back(lh);
                    const float motion_start = sampler.clipJointMotionStart(*bclip, joints);
                    cls.block_clip_start_seconds =
                        std::clamp(motion_start - 0.05f, 0.0f, bclip->duration());
                    combatLog("[combat:resolve] %s block trim: motion_start=%.3fs -> "
                              "block_clip_start=%.3fs\n",
                              cls.id.c_str(), motion_start, cls.block_clip_start_seconds);
                }
            }
        }
        auto log_techniques = [&](const char* slot, const std::vector<WeaponTechnique>& techs,
                                  bool two_handed)
        {
            for (std::size_t ti = 0; ti < techs.size(); ++ti)
            {
                char label[64];
                std::snprintf(label, sizeof(label), "%s [%zu:%s]", slot, ti, techs[ti].id.c_str());
                log_chain(cls.id.c_str(), label, techs[ti].attacks, two_handed);
                log_bookend_alignment(cls.id.c_str(), label, techs[ti].attacks);
            }
        };
        log_techniques("1H light", cls.one_handed.light, false);
        log_techniques("1H heavy", cls.one_handed.heavy, false);
        log_techniques("1H running", cls.one_handed.running, false);
        log_techniques("2H light", cls.two_handed.light, true);
        log_techniques("2H heavy", cls.two_handed.heavy, true);
        log_techniques("2H running", cls.two_handed.running, true);
    }
}

} // namespace selva::combat
