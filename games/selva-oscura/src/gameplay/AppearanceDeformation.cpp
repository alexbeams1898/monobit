#include "gameplay/AppearanceDeformation.h"

#include "anim/PoseSampler.h"
#include "anim/SkeletonJointMap.h"
#include "gameplay/Appearance.h"

#include <glm/gtc/matrix_transform.hpp>
#include <glm/mat4x4.hpp>
#include <glm/vec3.hpp>

#include <cmath>
#include <cstdio>
#include <vector>

namespace selva::gameplay
{

namespace
{

// Scale a joint subtree as a single rigid unit around the root
// joint's model-space position. One scale matrix applied to every
// joint in the subtree so bone lengths scale uniformly (not
// per-joint pivots, which would only fatten joints without
// stretching bones). `exclusion_roots` mark domain boundaries --
// excluded joints + their descendants don't receive the scale,
// used so torso doesn't bleed into head/arm subtrees.
// ozz stores joints in parent-before-children order, so one linear
// scan catches every descendant.
void scalePaletteSubtreeAroundJoints(std::vector<glm::mat4>& palette,
                                     selva::anim::PoseSampler& sampler, int root, float scale,
                                     const std::vector<int>& exclusion_roots = {})
{
    if (root < 0 || root >= static_cast<int>(palette.size()))
        return;
    const glm::vec3 root_pos = sampler.jointWorldPos(root);
    glm::mat4 s(1.0f);
    s = glm::translate(s, root_pos);
    s = glm::scale(s, glm::vec3(scale));
    s = glm::translate(s, -root_pos);

    const int n = sampler.jointCount();
    std::vector<bool> in_subtree(n, false);
    std::vector<bool> excluded(n, false);
    for (int er : exclusion_roots)
        if (er >= 0 && er < n)
            excluded[er] = true;
    in_subtree[root] = true;
    for (int i = root; i < n; ++i)
    {
        const int parent = sampler.jointParent(i);
        if (parent >= 0 && parent < n && in_subtree[parent] && !excluded[parent])
            in_subtree[i] = true;
        if (excluded[i])
            in_subtree[i] = false;
        if (in_subtree[i] && i < static_cast<int>(palette.size()))
            palette[static_cast<std::size_t>(i)] = s * palette[static_cast<std::size_t>(i)];
    }
}

} // namespace

void applyAppearanceDeformation(selva::anim::PoseSampler& sampler,
                                const selva::anim::SkeletonJointMap& jmap,
                                const Appearance& appearance, std::vector<glm::mat4>& out_palette)
{
    // ALWAYS rewrite out_palette from sampler.bone_palette --
    // in-place deformation compounded across frames when the
    // sampler didn't re-tick (gameplay still draws under pause).
    out_palette = sampler.bone_palette;

    // Per-bone scales are MODIFIERS on top of body_scale. body_scale
    // lives in the model matrix; per-bone sliders multiply on top.
    // head_scale=1.5 -> head grows 50% beyond whatever body_scale
    // produces. Identity case (all 1.0) early-returns with out_palette
    // already holding the source copy.
    (void)appearance.body_scale; // owned by the model matrix
    const float head_ratio = appearance.head_scale;
    const float arm_ratio = appearance.arm_scale;
    const float leg_ratio = appearance.leg_scale;
    const float torso_ratio = appearance.torso_scale;
    const bool head_active = std::abs(head_ratio - 1.0f) > 1e-5f;
    const bool arm_active = std::abs(arm_ratio - 1.0f) > 1e-5f;
    const bool leg_active = std::abs(leg_ratio - 1.0f) > 1e-5f;
    const bool torso_active = std::abs(torso_ratio - 1.0f) > 1e-5f;
    if (!head_active && !arm_active && !leg_active && !torso_active)
        return;

    if (out_palette.empty())
        return;

    // Order: innermost subtrees first (head, arms, legs), then
    // enclosing subtrees (torso). Each stanza pivots around the
    // joint's bind-pose position, which is wrong if a parent stanza
    // already moved it -- so innermost-first keeps every pivot
    // accurate to where the joint visibly sits when scaled.
    if (head_active && !jmap.head.empty())
    {
        const int head_idx = sampler.findJoint(jmap.head.c_str());
        scalePaletteSubtreeAroundJoints(out_palette, sampler, head_idx, head_ratio);
    }

    if (arm_active)
    {
        if (!jmap.uparm_left.empty())
        {
            const int idx = sampler.findJoint(jmap.uparm_left.c_str());
            scalePaletteSubtreeAroundJoints(out_palette, sampler, idx, arm_ratio);
        }
        if (!jmap.uparm_right.empty())
        {
            const int idx = sampler.findJoint(jmap.uparm_right.c_str());
            scalePaletteSubtreeAroundJoints(out_palette, sampler, idx, arm_ratio);
        }
    }

    if (leg_active)
    {
        if (!jmap.upleg_left.empty())
        {
            const int idx = sampler.findJoint(jmap.upleg_left.c_str());
            scalePaletteSubtreeAroundJoints(out_palette, sampler, idx, leg_ratio);
        }
        if (!jmap.upleg_right.empty())
        {
            const int idx = sampler.findJoint(jmap.upleg_right.c_str());
            scalePaletteSubtreeAroundJoints(out_palette, sampler, idx, leg_ratio);
        }
    }

    // Torso excludes head + uparm roots so its scale doesn't bleed
    // into the head/arm subtrees (each slider owns its own domain).
    if (torso_active && !jmap.torso.empty())
    {
        const int torso_idx = sampler.findJoint(jmap.torso.c_str());
        std::vector<int> exclusions;
        if (!jmap.head.empty())
        {
            const int idx = sampler.findJoint(jmap.head.c_str());
            if (idx >= 0)
                exclusions.push_back(idx);
        }
        if (!jmap.uparm_left.empty())
        {
            const int idx = sampler.findJoint(jmap.uparm_left.c_str());
            if (idx >= 0)
                exclusions.push_back(idx);
        }
        if (!jmap.uparm_right.empty())
        {
            const int idx = sampler.findJoint(jmap.uparm_right.c_str());
            if (idx >= 0)
                exclusions.push_back(idx);
        }
        scalePaletteSubtreeAroundJoints(out_palette, sampler, torso_idx, torso_ratio, exclusions);
    }
}

} // namespace selva::gameplay
