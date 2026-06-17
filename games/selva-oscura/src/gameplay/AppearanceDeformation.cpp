#include "gameplay/AppearanceDeformation.h"

#include "anim/PoseSampler.h"
#include "anim/SkeletonJointMap.h"
#include "gameplay/Appearance.h"

#include <glm/gtc/matrix_transform.hpp>
#include <glm/mat4x4.hpp>
#include <glm/vec3.hpp>

#include <cmath>
#include <cstdio>

namespace selva::gameplay
{

namespace
{

// Apply uniform scale S to a single joint's bone palette entry,
// pivoting around the joint's model-space position p. Math:
//   palette = T(+p) * Scale(S) * T(-p) * palette
// Left-multiplying because the shader's product is uModel * palette *
// rest_vertex; pre-multiplying our scale by the palette means it
// applies AFTER skinning to posed-mesh space, around the joint's
// current position. Children of this joint are NOT touched (the
// rare leaf-end joints in Mixamo rigs typically have no vertex
// influence; head-cap vertices skin to the head joint itself).
void scalePaletteAroundJoint(std::vector<glm::mat4>& palette, int joint_idx,
                             const glm::vec3& joint_model_pos, float scale)
{
    glm::mat4 m(1.0f);
    m = glm::translate(m, joint_model_pos);
    m = glm::scale(m, glm::vec3(scale));
    m = glm::translate(m, -joint_model_pos);
    palette[static_cast<std::size_t>(joint_idx)] = m * palette[static_cast<std::size_t>(joint_idx)];
}

} // namespace

void applyAppearanceDeformation(selva::anim::PoseSampler& sampler,
                                const selva::anim::SkeletonJointMap& jmap,
                                const Appearance& appearance)
{
    // head_scale is INDEPENDENT of body_scale per Souls-style
    // character creator convention: "head is X times its bind size"
    // regardless of the body's overall size. Since the body's uniform
    // model matrix already multiplies the head by body_scale, we
    // counter-scale the head bone by head_scale / body_scale so the
    // net visible head is exactly head_scale * bind_size.
    //
    // Identity case: head_scale == body_scale -> ratio = 1.0, no
    // per-bone work needed. Saves the joint lookup + matrix multiply
    // on every actor whose appearance has equal head/body scales
    // (including all default-Appearance actors at 1.0/1.0).
    const float body_scale =
        (std::abs(appearance.body_scale) > 1e-5f) ? appearance.body_scale : 1.0f;
    const float head_ratio = appearance.head_scale / body_scale;
    const bool head_active = std::abs(head_ratio - 1.0f) > 1e-5f;
    if (!head_active)
        return;

    if (sampler.bone_palette.empty())
        return;

    if (!jmap.head.empty())
    {
        const int head_idx = sampler.findJoint(jmap.head.c_str());
        if (head_idx >= 0 && head_idx < static_cast<int>(sampler.bone_palette.size()))
        {
            const glm::vec3 head_pos = sampler.jointWorldPos(head_idx);
            // One-time stderr diagnostic per process so we can verify
            // the first time we see a non-unit head_ratio that the
            // joint lookup hit + the model-space position is
            // reasonable (not at origin / NaN). Per
            // [[feedback_log_first_then_fix]] -- log before iterating
            // blind on perceived visual bugs.
            static bool s_logged = false;
            if (!s_logged)
            {
                std::fprintf(stderr,
                             "[appearance-deform] head_ratio=%.3f (head_scale=%.3f / "
                             "body_scale=%.3f) head_idx=%d model_pos=(%.3f, %.3f, %.3f)\n",
                             head_ratio, appearance.head_scale, body_scale, head_idx, head_pos.x,
                             head_pos.y, head_pos.z);
                std::fflush(stderr);
                s_logged = true;
            }
            scalePaletteAroundJoint(sampler.bone_palette, head_idx, head_pos, head_ratio);
        }
    }
}

} // namespace selva::gameplay
