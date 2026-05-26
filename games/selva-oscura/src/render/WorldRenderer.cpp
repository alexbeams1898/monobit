#include "render/WorldRenderer.h"

#include "AppStateGlobal.h"
#include "Tunables.h"
#include "WallClock.h"
#include "gl/ShaderUtils.h"
#include "physics/PhysicsWorld.h"
#include "render/Camera.h"
#include "render/SceneGeometry.h"
#include "render/SceneShaders.h"
#include "render/ShadowPass.h"
#include "render/TerrainShader.h"
#include "render/TreeShader.h"
#include "world/Collision.h"
#include "world/CryptLayout.h"
#include "world/JsonScene.h"
#include "world/Scene.h"
#include "world/StaticMeshAssets.h"
#include "world/StructureFootprints.h"
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

// Stencil-write program for the chapel floor mask. Positions-only
// vertex shader (matches StaticMeshPrimitive vertex layout: location 0
// = vec3 aPos), no fragment output. Compiled lazily on first use.
const char* kStencilVS = R"glsl(
#version 330 core
layout(location = 0) in vec3 aPos;
uniform mat4 uModel;
uniform mat4 uViewProj;
void main()
{
    gl_Position = uViewProj * uModel * vec4(aPos, 1.0);
}
)glsl";
const char* kStencilFS = R"glsl(
#version 330 core
void main() {}
)glsl";
GLuint sStencilProgram = 0;
GLint sStencilUniModel = -1;
GLint sStencilUniViewProj = -1;
glm::mat4 sLastViewProjForStencil(1.0f);

GLuint ensureStencilProgram()
{
    if (sStencilProgram == 0)
    {
        sStencilProgram = engine::gl::compileProgram(kStencilVS, kStencilFS);
        if (sStencilProgram != 0)
        {
            sStencilUniModel = glGetUniformLocation(sStencilProgram, "uModel");
            sStencilUniViewProj = glGetUniformLocation(sStencilProgram, "uViewProj");
        }
    }
    return sStencilProgram;
}
const glm::mat4& cryptModelMatrix();  // forward-decl; defined below in a later anon namespace
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
        // near plane matches the third-person path; see comment there.
        // FPV uses 0.2 (a bit tighter than 0.5 because FPV camera
        // sits inside the player capsule and can be very close to
        // arm/hand geometry).
        const glm::mat4 proj = glm::perspective(
            glm::radians(settings.fov_degrees_first_person), aspect, 0.2f, 200.0f);

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
        // FPV entry detection: if more than 100ms since the smoother
        // was last updated, this is a fresh FPV session (toggle from
        // TPV or first entry). Reset the smoother so the camera
        // snaps to the correct head position instead of panning in
        // from a stale offset.
        const Uint64 now_ticks = SDL_GetTicks64();
        if (now_ticks - sSmoothedPrevTicks > 100)
            sSmoothedInit = false;
        if (!sSmoothedInit)
        {
            sSmoothedHeadLocal = head_local;
            sSmoothedInit = true;
            sSmoothedPrevTicks = now_ticks;
        }
        else
        {
            const float dt = static_cast<float>(now_ticks - sSmoothedPrevTicks) * 0.001f;
            sSmoothedPrevTicks = now_ticks;
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
        static bool sPrevAnimIn = false;
        const float now_seconds = static_cast<float>(SDL_GetTicks64()) * 0.001f;
        if (use_anim_orientation_in)
            sFadeStartTime = -1.0f; // still in roll; no fade yet
        else if (sPrevAnimIn)
            sFadeStartTime = now_seconds; // falling edge: roll just ended
        sPrevAnimIn = use_anim_orientation_in;
        // Fade parameter: 0 just after roll ends, 1 when fade complete.
        const float fade_t =
            sFadeStartTime < 0.0f
                ? 0.0f
                : std::min(1.0f, (now_seconds - sFadeStartTime) / kAnimOrientationFade);
        // anim_active is true throughout the fade window (so the log
        // captures the transition), but the orientation itself is a
        // blend, not a hard switch. Fade is "in progress" only when
        // sFadeStartTime >= 0 AND fade_t hasn't reached 1.
        const bool fade_in_progress = (sFadeStartTime >= 0.0f) && (fade_t < 1.0f);
        const bool use_anim_orientation = use_anim_orientation_in || fade_in_progress;
        const float anim_weight =
            use_anim_orientation_in ? 1.0f : (fade_in_progress ? (1.0f - fade_t) : 0.0f);

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

    const glm::vec3 lookAt(player_pos.x, smoothed_lookat_y, player_pos.z);

    // Indoor follow-distance lerp. Indoor-ness is a scene-kind attribute
    // now (uniform across the whole scene) — set per scene in scene.json.
    const engine::world::Scene* scene_ptr = engine::world::currentScenePtr();
    const bool indoors = scene_ptr != nullptr &&
                         scene_ptr->kind() == engine::world::SceneKind::Interior;
    const float target_follow_distance =
        indoors ? tun.follow_distance_indoor : tun.follow_distance;
    static float sSmoothedFollowDistance = tun.follow_distance;
    static bool sFollowDistInit = false;
    {
        static Uint64 sPrevFollowTicks = SDL_GetTicks64();
        const Uint64 nowFollowTicks = SDL_GetTicks64();
        const float follow_dt = static_cast<float>(nowFollowTicks - sPrevFollowTicks) * 0.001f;
        sPrevFollowTicks = nowFollowTicks;
        if (!sFollowDistInit)
        {
            sSmoothedFollowDistance = target_follow_distance;
            sFollowDistInit = true;
        }
        else
        {
            const float follow_tau = std::max(1e-3f, tun.follow_distance_indoor_tau);
            const float alpha = 1.0f - std::exp(-follow_dt / follow_tau);
            sSmoothedFollowDistance +=
                (target_follow_distance - sSmoothedFollowDistance) * alpha;
        }
    }

    // Pitch-coupled ideal camera offset. Y cap prevents pitch-down
    // from flinging the camera arbitrarily high.
    const glm::vec3 ideal_offset =
        -lookFwd * sSmoothedFollowDistance + glm::vec3(0.0f, tun.follow_height, 0.0f);
    const float desired_separation = glm::length(ideal_offset);
    const glm::vec3 cam_dir = desired_separation > 1e-4f ? ideal_offset / desired_separation
                                                         : glm::vec3(0.0f, 1.0f, 0.0f);
    const float margin = tun.camera_pull_in_margin;
    const float sphere_radius = tun.camera_pull_in_radius;
    const engine::physics::RayHit hit =
        engine::physics::raycast(lookAt, cam_dir, desired_separation + margin);

    const float min_separation = std::max(0.0f, tun.camera_pull_in_min_separation);
    float target_separation =
        hit.hit ? std::max(min_separation, hit.distance - margin) : desired_separation;

    // Push-out: shrink target separation until sphere doesn't overlap.
    // Sphere catches walls perpendicular to the forward ray (corner-pocket).
    if (sphere_radius > 0.0f && target_separation > min_separation)
    {
        constexpr int kMaxPushOutSteps = 24;
        constexpr float kPushStep = 0.15f;
        float try_sep = target_separation;
        for (int i = 0; i < kMaxPushOutSteps; ++i)
        {
            const glm::vec3 try_pos = lookAt + cam_dir * try_sep;
            if (!engine::physics::sphereOverlap(try_pos, sphere_radius))
                break;
            if (try_sep <= min_separation)
                break;
            try_sep -= kPushStep;
            if (try_sep < min_separation)
                try_sep = min_separation;
        }
        target_separation = try_sep;
    }

    // Note: the legacy "if-player-indoors-shrink-until-camera-indoors"
    // enforcement is gone. Indoor-ness is now a per-scene attribute, so
    // the camera is always in the same scene as the player — no risk of
    // the camera "exiting" the chapel mid-frame. Walls of the active
    // scene are real Jolt bodies, so the raycast + sphere-overlap above
    // already keep the camera on the player's side of any wall.

    // SYMMETRIC smoothing both directions. Per Alex's spec: "each side
    // lerp the same to the chest." Wall safety is handled by the
    // forward-raycast + push-out folding into target_separation BEFORE
    // smoothing, so the smoothing target is always wall-clear; the
    // camera lerping through it visually is a non-issue because the
    // target moves continuously (raycast updates per-frame).
    static float sSmoothedSeparation = desired_separation;
    static bool sSeparationInit = false;
    {
        static Uint64 sPrevSepTicks = SDL_GetTicks64();
        const Uint64 nowSepTicks = SDL_GetTicks64();
        const float sep_dt = static_cast<float>(nowSepTicks - sPrevSepTicks) * 0.001f;
        sPrevSepTicks = nowSepTicks;
        if (!sSeparationInit)
        {
            sSmoothedSeparation = target_separation;
            sSeparationInit = true;
        }
        else
        {
            const float tau = std::max(1e-3f, tun.camera_pull_in_tau);
            const float alpha = 1.0f - std::exp(-sep_dt / tau);
            sSmoothedSeparation += (target_separation - sSmoothedSeparation) * alpha;
        }
    }
    glm::vec3 camPos = lookAt + cam_dir * sSmoothedSeparation;

    // Crosshair raycast log: aim camera at flickering surface, this
    // walks 5 hits forward along the view ray, logs each body name +
    // distance. Aim, wait 1s for the log, then read it to identify
    // which mesh primitives are at the flicker spot.
    if (tun.debug_crosshair_raycast_log)
    {
        static FILE* sXLog = nullptr;
        static int sXFrame = 0;
        if (sXLog == nullptr) sXLog = std::fopen("crosshair-debug.log", "w");
        if (sXLog != nullptr && (sXFrame++ % 60) == 0)
        {
            const glm::vec3 ray_dir = lookFwd;  // camera forward (mouse look)
            std::fprintf(sXLog, "[frame %d] camPos=(%.2f,%.2f,%.2f) lookFwd=(%.3f,%.3f,%.3f)\n",
                         sXFrame, camPos.x, camPos.y, camPos.z, ray_dir.x, ray_dir.y, ray_dir.z);
            glm::vec3 origin = camPos;
            for (int i = 0; i < 6; ++i)
            {
                const auto hit_i = engine::physics::raycast(origin, ray_dir, 200.0f);
                if (!hit_i.hit) {
                    std::fprintf(sXLog, "  hit %d: (no hit)\n", i);
                    break;
                }
                const char* nm = engine::physics::bodyDebugName(hit_i.body);
                std::fprintf(sXLog,
                             "  hit %d: dist=%.3f pos=(%.2f,%.2f,%.2f) normal=(%.2f,%.2f,%.2f) body='%s'\n",
                             i, hit_i.distance, hit_i.position.x, hit_i.position.y, hit_i.position.z,
                             hit_i.normal.x, hit_i.normal.y, hit_i.normal.z, nm ? nm : "(unnamed)");
                // Step ray origin slightly past this hit so the next cast picks up the next surface.
                origin = hit_i.position + ray_dir * 0.05f;
            }
            std::fflush(sXLog);
        }
    }

    // Ground-clearance clamp. Skip when the player is indoors (inside
    // the chapel + descent shaft): the camera is following the player
    // BELOW terrain plateau Y into the underground descent, and the
    // raw terrain sample at the camera XZ is still at plateau Y up
    // top (terrain doesn't know about the descent shaft). Clamping
    // to terrain Y holds the camera at plateau height and yanks it
    // away from the descending player. Using player Y as the floor
    // instead lets the camera follow the descent.
    constexpr float kCamMinClearance = 1.0f;
    const float terrain_floor_y = selva::world::sampleHeight(camPos.x, camPos.z);
    const float floor_y = indoors ? player_pos.y : terrain_floor_y;
    const float camY_before_clamp = camPos.y;
    if (camPos.y < floor_y + kCamMinClearance)
        camPos.y = floor_y + kCamMinClearance;
    const float camY_after_clamp = camPos.y;

    // Descent diagnosis log: when the player is below chapel-floor
    // level, capture every input to the camera Y decision so we can
    // see what's pinning the camera up (terrain sample, clamp, smooth
    // separation, scene-kind, lookAt Y).
    if (player_pos.y < 31.0f)
    {
        static FILE* sDescentLog = nullptr;
        static int sDescentFrame = 0;
        if (sDescentLog == nullptr)
            sDescentLog = std::fopen("camera-descent-debug.log", "w");
        if (sDescentLog != nullptr)
        {
            const char* scene_name =
                (scene_ptr != nullptr) ? scene_ptr->sceneId().c_str() : "<none>";
            std::fprintf(sDescentLog,
                         "[%d] pY=%.3f lookAtY=%.3f camY_pre=%.3f camY_post=%.3f "
                         "floor_y=%.3f terrain_y=%.3f indoors=%d scene='%s' "
                         "sep=%.3f cam_dir.y=%.3f hit=%d hit_dist=%.3f "
                         "smoothed_offset=%.3f\n",
                         sDescentFrame, player_pos.y, lookAt.y, camY_before_clamp,
                         camY_after_clamp, floor_y, terrain_floor_y,
                         indoors ? 1 : 0, scene_name, sSmoothedSeparation,
                         cam_dir.y, hit.hit ? 1 : 0, hit.distance,
                         sSmoothedOffset);
            std::fflush(sDescentLog);
            ++sDescentFrame;
        }
    }

    if (tun.debug_camera_pull_in_log)
    {
        static FILE* sPullInLog = nullptr;
        static int sPullInFrame = 0;
        static glm::vec3 sPrevPlayerPos = player_pos;
        if (sPullInLog == nullptr)
            sPullInLog = std::fopen("camera-debug.log", "w");
        if (sPullInLog != nullptr)
        {
            const bool overlap = engine::physics::sphereOverlap(camPos, sphere_radius);
            const float cam_y_above_chest = camPos.y - lookAt.y;
            const float cam_z_from_player = camPos.z - player_pos.z;
            const float cam_x_from_player = camPos.x - player_pos.x;
            const float cam_dist_xz = std::sqrt(cam_x_from_player * cam_x_from_player +
                                                cam_z_from_player * cam_z_from_player);
            const glm::vec3 player_vel = player_pos - sPrevPlayerPos;
            const float player_speed = glm::length(player_vel);
            // cam_indoors == player indoors now: scene-kind is uniform
            // across the whole scene.
            const bool cam_indoors = indoors;
            std::fprintf(
                sPullInLog,
                "[%d] pPos=(%.2f,%.2f,%.2f) pVel=(%.3f,%.3f,%.3f) pSpd=%.3f "
                "yaw=%.3f pitch=%.3f lookFwd=(%.3f,%.3f,%.3f) "
                "lookAt=(%.2f,%.2f,%.2f) cam_dir=(%.3f,%.3f,%.3f) "
                "ideal_off=(%.2f,%.2f,%.2f) ideal_off_len=%.2f "
                "follow_dist=%.2f indoors=%d cam_indoors=%d "
                "hit=%d hit_dist=%.3f target_sep=%.3f smoothed_sep=%.3f "
                "camPos=(%.2f,%.2f,%.2f) cam_y_above_chest=%.3f "
                "cam_xz_from_player=%.3f overlap=%d\n",
                sPullInFrame, player_pos.x, player_pos.y, player_pos.z, player_vel.x,
                player_vel.y, player_vel.z, player_speed, yaw, pitch, lookFwd.x, lookFwd.y,
                lookFwd.z, lookAt.x, lookAt.y, lookAt.z, cam_dir.x, cam_dir.y, cam_dir.z,
                ideal_offset.x, ideal_offset.y, ideal_offset.z, desired_separation,
                sSmoothedFollowDistance, indoors ? 1 : 0, cam_indoors ? 1 : 0,
                hit.hit ? 1 : 0, hit.distance, target_separation, sSmoothedSeparation,
                camPos.x, camPos.y, camPos.z, cam_y_above_chest, cam_dist_xz,
                overlap ? 1 : 0);
            std::fflush(sPullInLog);
            ++sPullInFrame;
            sPrevPlayerPos = player_pos;
        }
    }

    sLastView = glm::lookAt(camPos, lookAt, glm::vec3(0.0f, 1.0f, 0.0f));

    const float aspect =
        windowHeight() > 0 ? static_cast<float>(windowWidth()) / static_cast<float>(windowHeight())
                           : 1.0f;
    // Bumped near plane 0.1 → 0.5 to fix depth-precision flicker on
    // descent corridor walls/ceiling. At 140m corridor length with
    // standard depth precision and near/far ratio 2000, surfaces
    // separated by <3cm z-fight. Bumping near 5× gives ~5× better
    // precision everywhere, eliminating the corridor flicker. Cost:
    // camera can't get within 0.5m of geometry without clipping —
    // acceptable since camera_pull_in_min_separation is 0.5m.
    const glm::mat4 proj =
        glm::perspective(glm::radians(settings.fov_degrees_third_person), aspect, 0.5f, 200.0f);
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

void renderChapelFloorMask()
{
    // Stencil approach removed (was view-dependent and rejected terrain
    // pixels wherever the chapel floor projected to screen). Carve-out
    // is now done by normal depth test: chapel mesh is drawn BEFORE
    // terrain so chapel writes depth values; terrain depth-tests and
    // fails wherever the chapel is in front of it (the chapel-interior
    // floor footprint). Kept as a stub so the call site stays.
}

void renderTerrain()
{
    // Terrain shader is bound by the caller (selvaRenderWorld) so
    // atmosphere uniforms can be set per-frame consistently with sky
    // and trees.
    //
    // Structure footprints drive both physics-side (Hole modifier)
    // AND render-side (shader discard) terrain removal — single
    // source of truth, registered once at scene init. See
    // engine::world::registerStructureFootprint.
    //
    // Terrain shader has 2 active discard rect slots (chapel + descent).
    // First two registered footprints map to those slots; further
    // footprints would need a shader-side array bump.
    const int n_fp = engine::world::structureFootprintCount();
    if (n_fp >= 1)
    {
        const auto& f = engine::world::structureFootprintAt(0);
        selva::render::setTerrainChapelDiscard(f.center_xz, f.half_extents_xz);
    }
    else
    {
        selva::render::setTerrainChapelDiscard(glm::vec2(0.0f), glm::vec2(0.0f));
    }
    selva::render::setTerrainApseDiscard(glm::vec2(0.0f), 0.0f);
    if (n_fp >= 2)
    {
        const auto& f = engine::world::structureFootprintAt(1);
        selva::render::setTerrainDescentDiscard(f.center_xz, f.half_extents_xz);
    }
    else
    {
        selva::render::setTerrainDescentDiscard(glm::vec2(0.0f), glm::vec2(0.0f));
    }

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

namespace
{
// Crypt sits at the center of the colle plateau (per wood.md). Position
// is fixed and the heightmap is immutable at runtime, so cache the
// model matrix on first call rather than re-sampling per frame per pass.
const glm::mat4& cryptModelMatrix()
{
    static glm::mat4 sCached(0.0f);
    static bool sInit = false;
    if (!sInit)
    {
        sCached = glm::translate(glm::mat4(1.0f),
                                 selva::world::crypt_layout::chapelWorldOrigin());
        sInit = true;
    }
    return sCached;
}

void drawStaticPrimitive(const selva::world::StaticMeshPrimitive& p)
{
    if (p.vao == 0)
        return;
    selva::render::setSceneTint(1.0f);
    selva::render::setSceneBaseColor(
        glm::vec3(p.base_color[0], p.base_color[1], p.base_color[2]));
    glBindVertexArray(p.vao);
    glDrawElements(GL_TRIANGLES, p.index_count, GL_UNSIGNED_INT, nullptr);
}
} // namespace

void renderStaticMeshes()
{
    // Active scene owns its static meshes (JsonScene::renderMeshes).
    // Positions are baked in world space by the scene loader, so
    // model matrix is identity; the scene-render call sets it.
    auto* scene = dynamic_cast<selva::world::JsonScene*>(
        engine::world::currentScenePtr());
    if (scene != nullptr)
    {
        scene->renderMeshes();
        return;
    }
    // Fallback: legacy chapel global (used before the scene system
    // takes over, e.g. before any scene is activated at boot).
    const selva::world::StaticMesh* crypt = selva::world::cryptMesh();
    if (crypt == nullptr)
        return;
    setSceneModel(cryptModelMatrix());
    for (const auto& prim : crypt->primitives)
        drawStaticPrimitive(prim);
}

void renderStaticMeshesDepth()
{
    // beginDepthPass set cull-front for self-shadow-acne reduction on
    // open geometry. Enclosed architecture has thick walls with faces
    // on both sides; cull-front omits surfaces perpendicular to the
    // sun from the shadow map, letting sunlight fall on interior
    // walls that should be in shadow (the bright vertical streak
    // bug). Disable culling so both faces of every wall get written;
    // the receiver's normal-offset bias handles self-shadow.
    glDisable(GL_CULL_FACE);

    auto* scene = dynamic_cast<selva::world::JsonScene*>(
        engine::world::currentScenePtr());
    if (scene != nullptr)
    {
        // Scene-owned meshes: positions baked in world space; depth
        // model matrix is identity. Use renderMeshesDepth which does
        // NOT touch color shader state (different shader program is
        // bound by the depth pass).
        selva::render::setSceneDepthModel(glm::mat4(1.0f));
        scene->renderMeshesDepth();
    }
    else
    {
        // Legacy fallback.
        const selva::world::StaticMesh* crypt = selva::world::cryptMesh();
        if (crypt != nullptr)
        {
            const glm::mat4 model = cryptModelMatrix();
            selva::render::setSceneDepthModel(model);
            for (const auto& prim : crypt->primitives)
            {
                if (prim.vao == 0)
                    continue;
                glBindVertexArray(prim.vao);
                glDrawElements(GL_TRIANGLES, prim.index_count, GL_UNSIGNED_INT, nullptr);
            }
        }
    }

    // Restore cull-front for subsequent depth-pass casters (skeletal
    // actors, etc.) per the beginDepthPass contract.
    glEnable(GL_CULL_FACE);
    glCullFace(GL_FRONT);
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
        if (c.collision_only)
            continue;
        const float h_variant = hashXZ(c.center.x, c.center.z, 0x1u);
        const float h_yaw = hashXZ(c.center.x, c.center.z, 0x2u);
        const float h_scale = hashXZ(c.center.x, c.center.z, 0x3u);
        const float h_phase = hashXZ(c.center.x, c.center.z, 0x4u);

        const int hash_variant_idx =
            static_cast<int>(h_variant * static_cast<float>(tree_variants)) % tree_variants;
        const int variant_idx = (c.forced_variant_idx >= 0 && c.forced_variant_idx < tree_variants)
                                    ? c.forced_variant_idx
                                    : hash_variant_idx;
        const float yaw = h_yaw * 6.2831853f;
        const float hash_scale = 0.85f + h_scale * 0.45f; // 0.85..1.30
        const float scale = c.forced_scale > 0.0f ? c.forced_scale : hash_scale;
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
    {
        // Mirror the main-pass footprint-driven discards exactly so
        // terrain inside footprints doesn't cast phantom shadows.
        const int n_fp = engine::world::structureFootprintCount();
        if (n_fp >= 1)
        {
            const auto& f = engine::world::structureFootprintAt(0);
            selva::render::setTerrainDepthChapelDiscard(f.center_xz, f.half_extents_xz);
        }
        else
        {
            selva::render::setTerrainDepthChapelDiscard(glm::vec2(0.0f), glm::vec2(0.0f));
        }
        selva::render::setTerrainDepthApseDiscard(glm::vec2(0.0f), 0.0f);
        if (n_fp >= 2)
        {
            const auto& f = engine::world::structureFootprintAt(1);
            selva::render::setTerrainDepthDescentDiscard(f.center_xz, f.half_extents_xz);
        }
        else
        {
            selva::render::setTerrainDepthDescentDiscard(glm::vec2(0.0f), glm::vec2(0.0f));
        }
    }
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
        if (c.collision_only)
            continue;
        const float h_variant = hashXZ(c.center.x, c.center.z, 0x1u);
        const float h_yaw = hashXZ(c.center.x, c.center.z, 0x2u);
        const float h_scale = hashXZ(c.center.x, c.center.z, 0x3u);
        const float h_phase = hashXZ(c.center.x, c.center.z, 0x4u);
        const int hash_variant_idx =
            static_cast<int>(h_variant * static_cast<float>(tree_variants)) % tree_variants;
        const int variant_idx = (c.forced_variant_idx >= 0 && c.forced_variant_idx < tree_variants)
                                    ? c.forced_variant_idx
                                    : hash_variant_idx;
        const float yaw = h_yaw * 6.2831853f;
        const float hash_scale = 0.85f + h_scale * 0.45f;
        const float scale = c.forced_scale > 0.0f ? c.forced_scale : hash_scale;
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
