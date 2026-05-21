#include "render/WorldRenderer.h"

#include "AppStateGlobal.h"
#include "Tunables.h"
#include "WallClock.h"
#include "render/Camera.h"
#include "render/SceneGeometry.h"
#include "render/SceneShaders.h"
#include "render/ShadowPass.h"
#include "render/TerrainShader.h"
#include "render/TreeShader.h"
#include "world/Collision.h"
#include "world/Terrain.h"
#include "world/TreeAssets.h"

#include <glm/gtc/matrix_transform.hpp>

#include <SDL.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>

#include <glad/glad.h>

namespace selva::render
{

namespace
{
glm::mat4 sLastView(1.0f);
} // namespace

const glm::mat4& lastView()
{
    return sLastView;
}

glm::mat4 buildViewProj(const glm::vec3& player_pos, float target_lookat_y,
                        const glm::vec3& head_world_pos, const glm::mat4& head_world_mat,
                        bool use_anim_orientation_in, float player_yaw,
                        const char* one_shot_name)
{
    const auto& tun = selva::tuning::current();
    const auto& settings = selva::saveData().settings;
    const float yaw = cameraYaw();
    const float pitch = cameraPitch();
    const glm::vec3 lookFwd(std::cos(pitch) * -std::sin(yaw), std::sin(pitch),
                            std::cos(pitch) * -std::cos(yaw));

    // First-person path: camera is *aligned* to the head bone's full
    // world transform (orientation included), with mouse-look pitch
    // applied as an offset on top. This means:
    //  - Head breathing / lean / tilt from the animation rides the
    //    camera so you don't see your own torso bobbing in front of
    //    you in idle/combat.
    //  - Mouse yaw still rotates the actor (which rotates the head
    //    via the body), so look-around works.
    //  - Mouse pitch is layered on top of the head's pitch.
    //  - Roll/dodge clips override completely (full anim-driven).
    if (cameraMode() == CameraMode::FirstPerson)
    {
        const float kEyeForwardOffset = tun.fpv_eye_fwd_offset;
        const float kEyeUpOffset = tun.fpv_eye_up_offset;
        const float aspect = windowHeight() > 0
                                 ? static_cast<float>(windowWidth()) /
                                       static_cast<float>(windowHeight())
                                 : 1.0f;
        const glm::mat4 proj = glm::perspective(
            glm::radians(settings.fov_degrees_first_person), aspect, 0.1f, 200.0f);

        // FPV head-position model: track the PLAYER position 1:1
        // (the camera follows the player's bulk motion without lag),
        // and lightly smooth the head's LOCAL offset (the bob).
        // Short tau (~30ms = ~2 frames) so REAL head bob tracks the
        // eye 1:1 - the player should feel up-down motion with the
        // neck during run cycles. The smoothing only swallows sub-
        // frame numeric jitter, not authored animation motion.
        constexpr float kHeadBobTau = 0.03f;
        const glm::vec3 head_local = head_world_pos - player_pos;
        static glm::vec3 sSmoothedHeadLocal = head_local;
        static bool sSmoothedInit = false;
        static Uint64 sSmoothedPrevTicks = SDL_GetTicks64();
        if (!sSmoothedInit)
        {
            sSmoothedHeadLocal = head_local;
            sSmoothedInit = true;
            sSmoothedPrevTicks = SDL_GetTicks64();
        }
        else
        {
            const Uint64 now = SDL_GetTicks64();
            const float dt = static_cast<float>(now - sSmoothedPrevTicks) * 0.001f;
            sSmoothedPrevTicks = now;
            const float alpha = 1.0f - std::exp(-dt / kHeadBobTau);
            sSmoothedHeadLocal += (head_local - sSmoothedHeadLocal) * alpha;
        }
        const glm::vec3 sSmoothedHeadPos = player_pos + sSmoothedHeadLocal;

        // Smooth cross-fade between anim-orientation (during rolls)
        // and mouse-orientation (everything else). When the roll
        // one-shot ends we don't snap - we LERP the camera's fwd/up
        // from the head bone's orientation to the mouse-driven
        // orientation over kAnimOrientationFade seconds. This avoids
        // the "legs visible for a moment" artifact that comes from
        // the clip's tail frames having the head pointed down (the
        // character looking at the ground they're rolling on) while
        // the body has already come back to standing.
        constexpr float kAnimOrientationFade = 0.35f;
        static float sFadeStartTime = -1.0f;
        const float now_seconds = static_cast<float>(SDL_GetTicks64()) * 0.001f;
        if (use_anim_orientation_in)
            sFadeStartTime = -1.0f; // still in roll; no fade yet
        else if (sFadeStartTime < 0.0f)
            sFadeStartTime = now_seconds; // first frame after roll ended
        // Fade parameter: 0 just after roll ends, 1 when fade complete.
        const float fade_t =
            sFadeStartTime < 0.0f
                ? 0.0f
                : std::min(1.0f, (now_seconds - sFadeStartTime) / kAnimOrientationFade);
        // anim_active is true throughout the fade window (so the log
        // captures the transition), but the orientation itself is a
        // blend, not a hard switch.
        const bool use_anim_orientation = use_anim_orientation_in || fade_t < 1.0f;
        const float anim_weight = use_anim_orientation_in
                                      ? 1.0f
                                      : (1.0f - fade_t); // 1=full anim, 0=full mouse

        // Compute final camera position + forward + up. During the
        // post-roll fade window both branches blend by anim_weight:
        // 1.0 = pure anim orientation (during roll), 0.0 = pure
        // mouse orientation (after fade complete), in-between values
        // smoothly interpolate so there's no orientation snap.
        // Head bone basis for the diagnostic log (always computed; cheap).
        const glm::vec3 head_fwd_diag = glm::normalize(glm::vec3(head_world_mat[2]));
        const glm::vec3 head_up_diag = glm::normalize(glm::vec3(head_world_mat[1]));

        glm::vec3 cam_fwd;
        glm::vec3 cam_up;
        glm::vec3 camPos;
        if (anim_weight >= 1.0f)
        {
            // Pure anim orientation - inside the roll itself.
            cam_fwd = head_fwd_diag;
            cam_up = head_up_diag;
            camPos = head_world_pos + cam_fwd * kEyeForwardOffset + cam_up * kEyeUpOffset;
        }
        else if (anim_weight <= 0.0f)
        {
            // Pure mouse orientation - settled mouse-driven mode.
            cam_fwd = lookFwd;
            cam_up = glm::vec3(0.0f, 1.0f, 0.0f);
            camPos = sSmoothedHeadPos + cam_fwd * kEyeForwardOffset +
                     glm::vec3(0.0f, kEyeUpOffset, 0.0f);
        }
        else
        {
            // Post-roll fade window. Lerp fwd/up between anim and
            // mouse orientations; position lerps between head bone
            // (raw) and smoothed head position similarly.
            const glm::vec3 mouse_up(0.0f, 1.0f, 0.0f);
            cam_fwd = glm::normalize(head_fwd_diag * anim_weight + lookFwd * (1.0f - anim_weight));
            cam_up = glm::normalize(head_up_diag * anim_weight + mouse_up * (1.0f - anim_weight));
            const glm::vec3 pos_anim =
                head_world_pos + head_fwd_diag * kEyeForwardOffset + head_up_diag * kEyeUpOffset;
            const glm::vec3 pos_mouse = sSmoothedHeadPos + lookFwd * kEyeForwardOffset +
                                        glm::vec3(0.0f, kEyeUpOffset, 0.0f);
            camPos = pos_anim * anim_weight + pos_mouse * (1.0f - anim_weight);
        }
        sLastView = glm::lookAt(camPos, camPos + cam_fwd, cam_up);

        // FPV roll diagnostic log (gated). Writes only while a roll
        // one-shot is active or during the tail-hold window after.
        // Logged columns are enough to reconstruct the camera vs body
        // mismatch frame-by-frame.
        const bool is_roll_window = use_anim_orientation_in || anim_weight > 0.0f;
        if (selva::tuning::current().debug_fpv_roll_log && is_roll_window)
        {
            static FILE* sFpvRollLog = nullptr;
            static int sFpvRollFrame = 0;
            if (sFpvRollLog == nullptr)
                sFpvRollLog = std::fopen("fpv-roll-debug.log", "w");
            if (sFpvRollLog != nullptr)
            {
                std::fprintf(sFpvRollLog,
                             "frame=%d t=%.4f clip=%s anim_in=%d anim_w=%.3f "
                             "player=(%.3f,%.3f,%.3f) yaw=%.4f "
                             "head_world=(%.3f,%.3f,%.3f) "
                             "head_fwd=(%.3f,%.3f,%.3f) head_up=(%.3f,%.3f,%.3f) "
                             "smoothed_head=(%.3f,%.3f,%.3f) "
                             "camPos=(%.3f,%.3f,%.3f) cam_fwd=(%.3f,%.3f,%.3f) "
                             "cam_up=(%.3f,%.3f,%.3f)\n",
                             sFpvRollFrame, now_seconds,
                             (one_shot_name != nullptr) ? one_shot_name : "(none)",
                             use_anim_orientation_in ? 1 : 0, anim_weight,
                             player_pos.x, player_pos.y, player_pos.z, player_yaw,
                             head_world_pos.x, head_world_pos.y, head_world_pos.z,
                             head_fwd_diag.x, head_fwd_diag.y, head_fwd_diag.z,
                             head_up_diag.x, head_up_diag.y, head_up_diag.z,
                             sSmoothedHeadPos.x, sSmoothedHeadPos.y, sSmoothedHeadPos.z,
                             camPos.x, camPos.y, camPos.z, cam_fwd.x, cam_fwd.y, cam_fwd.z,
                             cam_up.x, cam_up.y, cam_up.z);
                std::fflush(sFpvRollLog);
            }
            ++sFpvRollFrame;
        }

        return proj * sLastView;
    }

    // Ground reference Y = the player's feet (= sampled terrain Y).
    // Camera height + lookAt anchor both rest on this; climbing a
    // hill moves them together, no relative drift.
    const float ground_y = player_pos.y;

    // LookAt anchor = ground + chest-height-offset. We smooth the
    // OFFSET above ground (target_lookat_y - ground_y), not the
    // absolute Y. Dynamic poses (knockdown, get-up) move the hip
    // off-ground temporarily; the smoothing absorbs that without
    // making the camera lurch when the player walks up a slope.
    const float target_offset = target_lookat_y - ground_y;
    static float sSmoothedOffset = target_offset;
    static bool sOffsetInit = false;
    if (!sOffsetInit)
    {
        sSmoothedOffset = target_offset;
        sOffsetInit = true;
    }
    else
    {
        constexpr float kLookAtTau = 0.4f;
        static Uint64 sPrevTicks = SDL_GetTicks64();
        const Uint64 nowTicks = SDL_GetTicks64();
        const float frame_dt = static_cast<float>(nowTicks - sPrevTicks) * 0.001f;
        sPrevTicks = nowTicks;
        const float alpha = 1.0f - std::exp(-frame_dt / kLookAtTau);
        sSmoothedOffset += (target_offset - sSmoothedOffset) * alpha;
    }
    const float smoothed_lookat_y = ground_y + sSmoothedOffset;

    glm::vec3 camPos =
        player_pos - lookFwd * tun.follow_distance + glm::vec3(0.0f, tun.follow_height, 0.0f);

    // Ground-clearance clamp: lift the camera above terrain at its
    // XZ position so it never descends below the surface. Without
    // this, on a downward slope the camera's offset-from-player
    // places it below the terrain surface and rays leave the world
    // through the side of the hill — visible as see-through ground.
    constexpr float kCamMinClearance = 1.0f;
    const float terrain_at_cam = selva::world::sampleHeight(camPos.x, camPos.z);
    if (camPos.y < terrain_at_cam + kCamMinClearance)
        camPos.y = terrain_at_cam + kCamMinClearance;

    const glm::vec3 lookAt(player_pos.x, smoothed_lookat_y, player_pos.z);
    sLastView = glm::lookAt(camPos, lookAt, glm::vec3(0.0f, 1.0f, 0.0f));

    const float aspect =
        windowHeight() > 0 ? static_cast<float>(windowWidth()) / static_cast<float>(windowHeight())
                           : 1.0f;
    const glm::mat4 proj =
        glm::perspective(glm::radians(settings.fov_degrees_third_person), aspect, 0.1f, 200.0f);
    return proj * sLastView;
}

namespace
{
// Deterministic hash from (x, z) -> [0, 1). Used to derive per-tree
// variant pick, yaw, scale, wind phase — all stable across runs.
float hashXZ(float x, float z, std::uint32_t salt)
{
    std::uint32_t h = static_cast<std::uint32_t>(static_cast<int>(x * 1000.0f)) * 374761393u +
                      static_cast<std::uint32_t>(static_cast<int>(z * 1000.0f)) * 668265263u + salt;
    h = (h ^ (h >> 13)) * 1274126177u;
    h = h ^ (h >> 16);
    return static_cast<float>(h & 0xFFFFFFu) / static_cast<float>(0xFFFFFF);
}

void drawTreeMesh(const selva::world::TreeMesh& m)
{
    if (m.vao == 0)
        return;
    selva::render::setTreeBaseColor(m.base_color_tex);
    selva::render::setTreeAlphaCutoff(m.alpha_cutoff);
    glBindVertexArray(m.vao);
    glDrawElements(GL_TRIANGLES, m.index_count, GL_UNSIGNED_INT, nullptr);
}
} // namespace

void renderTerrain()
{
    // Terrain shader is bound by the caller (selvaRenderWorld) so
    // atmosphere uniforms can be set per-frame consistently with sky
    // and trees.
    for (int i = 0; i < selva::world::terrainRegionCount(); ++i)
    {
        const auto& r = selva::world::terrainRegion(i);
        selva::render::setTerrainBaseColor(
            glm::vec3(r.base_color[0], r.base_color[1], r.base_color[2]));
        // v0 unstained palette — see docs/design/wood.md "Floor color".
        selva::render::setTerrainTones(glm::vec3(0.08f, 0.07f, 0.06f),
                                       glm::vec3(0.20f, 0.13f, 0.09f));
        glBindVertexArray(r.vao);
        glDrawElements(GL_TRIANGLES, r.index_count, GL_UNSIGNED_INT, nullptr);
    }
    glBindVertexArray(0);
}

void renderGroundDecals()
{
    // Disc shadows removed: they were a flat-XZ-plane decal that
    // pokes through sloped terrain as a visible ring. Real cascaded
    // shadow maps will replace fake contact shadows later.
}

void renderTrees()
{
    const int variant_count = selva::world::treeVariantCount();
    if (variant_count <= 0)
        return;

    selva::render::setTreeTime(selva::wallClock());

    // Translucent canopies need alpha test (already in the shader)
    // and back-face NOT culled (foliage planes are double-sided in
    // the source mesh). Alpha-to-coverage gives smooth foliage edges
    // by converting alpha into per-sample MSAA coverage — kills the
    // hard-alpha-test gaps between leaf cards that read as "stray
    // bare sticks" between clusters of foliage. Honors the glTF
    // material's BLEND alpha mode without needing back-to-front
    // sort.
    glDisable(GL_CULL_FACE);
    glEnable(GL_SAMPLE_ALPHA_TO_COVERAGE);

    // Variant layout: indices 0..kTreeVariantEnd are hero trees,
    // indices [kTreeVariantEnd..variant_count) are rocks. Keep in
    // sync with the loader order in world/TreeAssets.cpp.
    constexpr int kTreeVariantEnd = 4;
    const int tree_variants = std::min(kTreeVariantEnd, variant_count);
    const int rock_variants = std::max(0, variant_count - tree_variants);

    for (const auto& c : selva::world::currentScene().cylinders)
    {
        const float h_variant = hashXZ(c.center.x, c.center.z, 0x1u);
        const float h_yaw = hashXZ(c.center.x, c.center.z, 0x2u);
        const float h_scale = hashXZ(c.center.x, c.center.z, 0x3u);
        const float h_phase = hashXZ(c.center.x, c.center.z, 0x4u);

        const int variant_idx =
            static_cast<int>(h_variant * static_cast<float>(tree_variants)) % tree_variants;
        const float yaw = h_yaw * 6.2831853f;
        const float scale = 0.85f + h_scale * 0.45f; // 0.85..1.30
        const float wind_phase = h_phase * 6.2831853f;

        const selva::world::TreeVariant& v = selva::world::treeVariant(variant_idx);

        const float ground_y = selva::world::sampleHeight(c.center.x, c.center.z);
        glm::mat4 model =
            glm::translate(glm::mat4(1.0f), glm::vec3(c.center.x, ground_y, c.center.z));
        model = glm::rotate(model, yaw, glm::vec3(0.0f, 1.0f, 0.0f));
        model = glm::scale(model, glm::vec3(scale, scale, scale));

        selva::render::setTreeModel(model);
        selva::render::setTreeWindPhase(wind_phase);

        drawTreeMesh(v.trunk);
        drawTreeMesh(v.branches);
    }

    // Rocks deferred. Inner Wood is hand-authored (per wood.md), not
    // procedurally scattered. Rock meshes still load via TreeAssets
    // so they're ready for a future authored-placement pass.
    (void)rock_variants;

    glDisable(GL_SAMPLE_ALPHA_TO_COVERAGE);
    glEnable(GL_CULL_FACE);
}

void renderTerrainDepth()
{
    for (int i = 0; i < selva::world::terrainRegionCount(); ++i)
    {
        const auto& r = selva::world::terrainRegion(i);
        glBindVertexArray(r.vao);
        glDrawElements(GL_TRIANGLES, r.index_count, GL_UNSIGNED_INT, nullptr);
    }
    glBindVertexArray(0);
}

void renderTreesDepth()
{
    const int variant_count = selva::world::treeVariantCount();
    if (variant_count <= 0)
        return;

    setTreeDepthWind(selva::wallClock(), 0.0f); // wind_phase set per-instance below

    // Leaf cards are double-sided alpha-cutout: the main pass
    // disables cull entirely so both faces show. The depth pass
    // MUST also disable cull - if we cull front (the default in
    // beginDepthPass for opaque-acne reduction), only one face of a
    // leaf card writes depth and the shadow flickers based on
    // tree yaw vs sun direction. Disabling cull here OVERRIDES the
    // beginDepthPass cull setting for this draw block; we restore
    // GL_FRONT cull at the end so subsequent depth-pass casters
    // (skeletal) get the opaque-acne reduction.
    glDisable(GL_CULL_FACE);

    constexpr int kTreeVariantEnd = 4;
    const int tree_variants = std::min(kTreeVariantEnd, variant_count);

    for (const auto& c : selva::world::currentScene().cylinders)
    {
        const float h_variant = hashXZ(c.center.x, c.center.z, 0x1u);
        const float h_yaw = hashXZ(c.center.x, c.center.z, 0x2u);
        const float h_scale = hashXZ(c.center.x, c.center.z, 0x3u);
        const float h_phase = hashXZ(c.center.x, c.center.z, 0x4u);
        const int variant_idx =
            static_cast<int>(h_variant * static_cast<float>(tree_variants)) % tree_variants;
        const float yaw = h_yaw * 6.2831853f;
        const float scale = 0.85f + h_scale * 0.45f;
        const float wind_phase = h_phase * 6.2831853f;
        const selva::world::TreeVariant& v = selva::world::treeVariant(variant_idx);
        const float ground_y = selva::world::sampleHeight(c.center.x, c.center.z);
        glm::mat4 model =
            glm::translate(glm::mat4(1.0f), glm::vec3(c.center.x, ground_y, c.center.z));
        model = glm::rotate(model, yaw, glm::vec3(0.0f, 1.0f, 0.0f));
        model = glm::scale(model, glm::vec3(scale, scale, scale));
        setTreeDepthModel(model);
        setTreeDepthWind(selva::wallClock(), wind_phase);

        // Trunk
        if (v.trunk.vao != 0)
        {
            glActiveTexture(GL_TEXTURE0);
            glBindTexture(GL_TEXTURE_2D, v.trunk.base_color_tex);
            setTreeDepthAlphaCutoff(v.trunk.alpha_cutoff);
            glBindVertexArray(v.trunk.vao);
            glDrawElements(GL_TRIANGLES, v.trunk.index_count, GL_UNSIGNED_INT, nullptr);
        }
        // Branches (alpha-cutout)
        if (v.branches.vao != 0)
        {
            glActiveTexture(GL_TEXTURE0);
            glBindTexture(GL_TEXTURE_2D, v.branches.base_color_tex);
            setTreeDepthAlphaCutoff(v.branches.alpha_cutoff);
            glBindVertexArray(v.branches.vao);
            glDrawElements(GL_TRIANGLES, v.branches.index_count, GL_UNSIGNED_INT, nullptr);
        }
    }

    // Restore front-face cull (beginDepthPass default) so subsequent
    // opaque depth casters get the acne-reduction trick.
    glEnable(GL_CULL_FACE);
    glCullFace(GL_FRONT);
    glBindVertexArray(0);
}

} // namespace selva::render
