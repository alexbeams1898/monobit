#include "render/WorldRenderer.h"

#include "AppStateGlobal.h"
#include "Tunables.h"
#include "WallClock.h"
#include "debug/Flags.h"
#include "gameplay/Actor.h"
#include "gl/ShaderUtils.h"
#include "physics/PhysicsWorld.h"
#include "render/Camera.h"
#include "render/EquippedWeapon.h"
#include "render/RegionGeometry.h"
#include "render/RegionShaders.h"
#include "render/ShadowPass.h"
#include "render/TerrainShader.h"
#include "render/TreeShader.h"
#include "world/Collision.h"
#include "world/CryptLayout.h"
#include "world/Door.h"
#include "world/JsonRegion.h"
#include "world/Lights.h"
#include "world/PhysicsRegion.h"
#include "world/Region.h"
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
#include <cstring>
#include <vector>

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
} // namespace

const glm::mat4& lastView()
{
    return sLastView;
}

namespace
{
// Per-frame snapshot of every value the camera-pull-in debug log
// wants — bundled so the writer takes one parameter, not 14.
struct CameraPullInFrame
{
    glm::vec3 player_pos;
    float yaw;
    float pitch;
    glm::vec3 look_fwd;
    glm::vec3 look_at;
    glm::vec3 cam_dir;
    glm::vec3 ideal_offset;
    float desired_separation;
    float follow_distance;
    engine::physics::RayHit hit;
    float target_separation;
    float smoothed_separation;
    glm::vec3 cam_pos;
    float sphere_radius;
};

// Open the log file lazily; on first open, dump a one-shot
// enumeration of every Jolt body so the "what bodies exist + where"
// question is answerable from the log alone.
FILE* openCameraPullInLog()
{
    static FILE* sPullInLog = nullptr;
    static bool sOpenAttempted = false;
    if (sPullInLog != nullptr || sOpenAttempted)
        return sPullInLog;
    sOpenAttempted = true;
    sPullInLog = std::fopen("camera-debug.log", "w");
    if (sPullInLog == nullptr)
        return nullptr;
    std::vector<engine::physics::BodyDebugInfo> bodies;
    engine::physics::enumerateBodies(bodies);
    std::fprintf(sPullInLog, "=== physics body enumeration (%zu bodies) ===\n", bodies.size());
    for (const auto& b : bodies)
    {
        const char* nm = engine::physics::bodyDebugName(b.body);
        const char* kind = b.kind == engine::physics::BodyKind::Character       ? "Character"
                           : b.kind == engine::physics::BodyKind::StaticBox     ? "Box"
                           : b.kind == engine::physics::BodyKind::StaticTrimesh ? "Tri"
                                                                                : "?";
        std::fprintf(sPullInLog,
                     "  id=%u kind=%s tag=%d aabb=[(%.2f,%.2f,%.2f)-(%.2f,%.2f,%.2f)] "
                     "name='%s'\n",
                     b.body.id, kind, static_cast<int>(b.tag), b.world_aabb_min.x,
                     b.world_aabb_min.y, b.world_aabb_min.z, b.world_aabb_max.x, b.world_aabb_max.y,
                     b.world_aabb_max.z, nm);
    }
    std::fprintf(sPullInLog, "=== end enumeration ===\n");
    std::fflush(sPullInLog);
    return sPullInLog;
}

// Same-side-of-wall probe: independent rays from camera→player and
// player→camera. If either hits a body, there's wall between them.
struct WallBetweenProbe
{
    engine::physics::RayHit cam_to_look;
    engine::physics::RayHit feet_to_cam;
    bool wall_between;
};
WallBetweenProbe probeWallBetween(const glm::vec3& camPos, const glm::vec3& lookAt,
                                  const glm::vec3& player_pos)
{
    WallBetweenProbe p;
    const glm::vec3 cam_to_look = lookAt - camPos;
    const float cam_to_look_dist = glm::length(cam_to_look);
    if (cam_to_look_dist > 1e-4f)
    {
        const glm::vec3 dir = cam_to_look / cam_to_look_dist;
        p.cam_to_look = engine::physics::raycast(camPos, dir, cam_to_look_dist + 0.01f);
    }
    const glm::vec3 feet_to_cam = camPos - player_pos;
    const float feet_to_cam_dist = glm::length(feet_to_cam);
    if (feet_to_cam_dist > 1e-4f)
    {
        const glm::vec3 dir = feet_to_cam / feet_to_cam_dist;
        p.feet_to_cam = engine::physics::raycast(player_pos, dir, feet_to_cam_dist + 0.01f);
    }
    p.wall_between = p.cam_to_look.hit || p.feet_to_cam.hit;
    return p;
}

// Six-axis nearest-wall probe from the camera. Used to disambiguate
// the see-outside hypotheses (camera inside/outside/none).
struct WallNearestProbe
{
    float dist = 999.0f;
    const char* name = "";
    int axis = -1;
};
WallNearestProbe probeWallNearest(const glm::vec3& camPos)
{
    WallNearestProbe p;
    const glm::vec3 axis_dirs[6] = {{1, 0, 0},  {-1, 0, 0}, {0, 1, 0},
                                    {0, -1, 0}, {0, 0, 1},  {0, 0, -1}};
    for (int ax = 0; ax < 6; ++ax)
    {
        const auto h = engine::physics::raycast(camPos, axis_dirs[ax], 2.0f);
        if (h.hit && h.distance < p.dist)
        {
            p.dist = h.distance;
            p.name = engine::physics::bodyDebugName(h.body);
            p.axis = ax;
        }
    }
    return p;
}

// "Where is the player actually standing" probes — for diagnosing
// dual-source-of-truth bugs between Jolt's foot Y and the visible floor.
struct PlayerFloorProbe
{
    const char* ground_name = "(none)";
    engine::physics::RayHit feet_drop;
    float feet_drop_y = -999.0f;
    const char* feet_drop_name = "";
    std::size_t contact_count = 0;
    char contacts_buf[256] = "";
};
PlayerFloorProbe probePlayerFloor(const glm::vec3& player_pos, const glm::vec3& lookAt)
{
    PlayerFloorProbe p;
    const engine::physics::BodyHandle pbody = selva::world::playerBody();
    if (pbody != engine::physics::kInvalidBody)
    {
        const engine::physics::BodyHandle ground = engine::physics::characterGroundBody(pbody);
        if (ground != engine::physics::kInvalidBody)
            p.ground_name = engine::physics::bodyDebugName(ground);
    }
    const glm::vec3 drop_origin(player_pos.x, lookAt.y, player_pos.z);
    p.feet_drop = engine::physics::raycast(drop_origin, glm::vec3(0, -1, 0), 50.0f);
    if (p.feet_drop.hit)
    {
        p.feet_drop_y = lookAt.y - p.feet_drop.distance;
        p.feet_drop_name = engine::physics::bodyDebugName(p.feet_drop.body);
    }
    static std::vector<engine::physics::BodyHandle> sContacts;
    sContacts.clear();
    if (pbody != engine::physics::kInvalidBody)
        engine::physics::characterActiveContacts(pbody, sContacts);
    p.contact_count = sContacts.size();
    int written = 0;
    for (std::size_t i = 0; i < sContacts.size() && i < 6; ++i)
    {
        const char* nm = engine::physics::bodyDebugName(sContacts[i]);
        const int n = std::snprintf(p.contacts_buf + written,
                                    sizeof(p.contacts_buf) - static_cast<std::size_t>(written),
                                    "%s%s", i == 0 ? "" : ",", nm);
        if (n <= 0 || static_cast<std::size_t>(written) + static_cast<std::size_t>(n) >=
                          sizeof(p.contacts_buf))
            break;
        written += n;
    }
    return p;
}

void debugWriteCameraPullInLog(const CameraPullInFrame& f)
{
    FILE* log = openCameraPullInLog();
    if (log == nullptr)
        return;

    static int sFrame = 0;
    static glm::vec3 sPrevPlayerPos = f.player_pos;

    const bool overlap = engine::physics::sphereOverlap(f.cam_pos, f.sphere_radius);
    const float cam_y_above_chest = f.cam_pos.y - f.look_at.y;
    const float cam_x_from_player = f.cam_pos.x - f.player_pos.x;
    const float cam_z_from_player = f.cam_pos.z - f.player_pos.z;
    const float cam_dist_xz =
        std::sqrt(cam_x_from_player * cam_x_from_player + cam_z_from_player * cam_z_from_player);
    const glm::vec3 player_vel = f.player_pos - sPrevPlayerPos;
    const float player_speed = glm::length(player_vel);

    const WallBetweenProbe wbp = probeWallBetween(f.cam_pos, f.look_at, f.player_pos);
    const char* probe_c2l_name =
        wbp.cam_to_look.hit ? engine::physics::bodyDebugName(wbp.cam_to_look.body) : "";
    const char* probe_f2c_name =
        wbp.feet_to_cam.hit ? engine::physics::bodyDebugName(wbp.feet_to_cam.body) : "";
    const WallNearestProbe wn = probeWallNearest(f.cam_pos);
    const PlayerFloorProbe pf = probePlayerFloor(f.player_pos, f.look_at);

    std::fprintf(log,
                 "[%d] pPos=(%.2f,%.2f,%.2f) pVel=(%.3f,%.3f,%.3f) pSpd=%.3f "
                 "yaw=%.3f pitch=%.3f lookFwd=(%.3f,%.3f,%.3f) "
                 "lookAt=(%.2f,%.2f,%.2f) cam_dir=(%.3f,%.3f,%.3f) "
                 "ideal_off=(%.2f,%.2f,%.2f) ideal_off_len=%.2f "
                 "follow_dist=%.2f "
                 "hit=%d hit_dist=%.3f target_sep=%.3f smoothed_sep=%.3f "
                 "camPos=(%.2f,%.2f,%.2f) cam_y_above_chest=%.3f "
                 "cam_xz_from_player=%.3f overlap=%d "
                 "wall_between=%d probe_c2l=(hit=%d dist=%.3f name='%s') "
                 "probe_f2c=(hit=%d dist=%.3f name='%s') "
                 "ground='%s' feet_drop=(hit=%d y=%.3f gap=%.3f name='%s') "
                 "active_contacts=%zu [%s] "
                 "wall_near=(dist=%.3f axis=%d name='%s')\n",
                 sFrame, f.player_pos.x, f.player_pos.y, f.player_pos.z, player_vel.x, player_vel.y,
                 player_vel.z, player_speed, f.yaw, f.pitch, f.look_fwd.x, f.look_fwd.y,
                 f.look_fwd.z, f.look_at.x, f.look_at.y, f.look_at.z, f.cam_dir.x, f.cam_dir.y,
                 f.cam_dir.z, f.ideal_offset.x, f.ideal_offset.y, f.ideal_offset.z,
                 f.desired_separation, f.follow_distance, f.hit.hit ? 1 : 0, f.hit.distance,
                 f.target_separation, f.smoothed_separation, f.cam_pos.x, f.cam_pos.y, f.cam_pos.z,
                 cam_y_above_chest, cam_dist_xz, overlap ? 1 : 0, wbp.wall_between ? 1 : 0,
                 wbp.cam_to_look.hit ? 1 : 0, wbp.cam_to_look.distance, probe_c2l_name,
                 wbp.feet_to_cam.hit ? 1 : 0, wbp.feet_to_cam.distance, probe_f2c_name,
                 pf.ground_name, pf.feet_drop.hit ? 1 : 0, pf.feet_drop_y,
                 pf.feet_drop.hit ? (pf.feet_drop_y - f.player_pos.y) : -999.0f, pf.feet_drop_name,
                 pf.contact_count, pf.contacts_buf, wn.dist, wn.axis, wn.name);
    std::fflush(log);
    ++sFrame;
    sPrevPlayerPos = f.player_pos;
}
} // namespace

namespace
{
// FPV head-position smoother: tracks PLAYER position 1:1 and lightly
// smooths the head's LOCAL offset (bob). >100ms gap = fresh FPV
// session, snap to current head pos. ~30ms tau (~2 frames) so real
// head bob tracks 1:1 — only sub-frame jitter is swallowed.
glm::vec3 smoothFpvHeadPos(const glm::vec3& player_pos, const glm::vec3& head_world_pos)
{
    constexpr float kHeadBobTau = 0.03f;
    const glm::vec3 head_local = head_world_pos - player_pos;
    static glm::vec3 sSmoothedHeadLocal = head_local;
    static bool sSmoothedInit = false;
    static Uint64 sSmoothedPrevTicks = SDL_GetTicks64();
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
    return player_pos + sSmoothedHeadLocal;
}

// Post-roll fade tracker. Returns anim_weight in [0,1]: 1.0 = pure
// anim orientation (inside roll), 0.0 = pure mouse orientation
// (settled), in-between values lerp. Avoids the "legs visible for a
// moment" artifact when a roll one-shot ends.
float computeFpvAnimWeight(bool use_anim_orientation_in)
{
    constexpr float kAnimOrientationFade = 0.35f;
    static float sFadeStartTime = -1.0f;
    static bool sPrevAnimIn = false;
    const float now_seconds = static_cast<float>(SDL_GetTicks64()) * 0.001f;
    if (use_anim_orientation_in)
        sFadeStartTime = -1.0f;
    else if (sPrevAnimIn)
        sFadeStartTime = now_seconds;
    sPrevAnimIn = use_anim_orientation_in;
    const float fade_t =
        sFadeStartTime < 0.0f
            ? 0.0f
            : std::min(1.0f, (now_seconds - sFadeStartTime) / kAnimOrientationFade);
    const bool fade_in_progress = (sFadeStartTime >= 0.0f) && (fade_t < 1.0f);
    return use_anim_orientation_in ? 1.0f : (fade_in_progress ? (1.0f - fade_t) : 0.0f);
}

struct FpvCameraPose
{
    glm::vec3 cam_fwd;
    glm::vec3 cam_up;
    glm::vec3 cam_pos;
};

// Pick camera pose based on anim_weight: pure anim (>=1.0), pure
// mouse (<=0.0), or blended fade window in between. Position and
// orientation are blended together so there's no orientation snap.
FpvCameraPose composeFpvCameraPose(const glm::vec3& head_world_pos,
                                   const glm::vec3& smoothed_head_pos, const glm::vec3& lookFwd,
                                   const glm::vec3& head_fwd_diag, const glm::vec3& head_up_diag,
                                   float anim_weight, float kEyeForwardOffset, float kEyeUpOffset)
{
    FpvCameraPose pose;
    if (anim_weight >= 1.0f)
    {
        pose.cam_fwd = head_fwd_diag;
        pose.cam_up = head_up_diag;
        pose.cam_pos =
            head_world_pos + pose.cam_fwd * kEyeForwardOffset + pose.cam_up * kEyeUpOffset;
        return pose;
    }
    if (anim_weight <= 0.0f)
    {
        pose.cam_fwd = lookFwd;
        pose.cam_up = glm::vec3(0.0f, 1.0f, 0.0f);
        pose.cam_pos = smoothed_head_pos + pose.cam_fwd * kEyeForwardOffset +
                       glm::vec3(0.0f, kEyeUpOffset, 0.0f);
        return pose;
    }
    const glm::vec3 mouse_up(0.0f, 1.0f, 0.0f);
    pose.cam_fwd = glm::normalize(head_fwd_diag * anim_weight + lookFwd * (1.0f - anim_weight));
    pose.cam_up = glm::normalize(head_up_diag * anim_weight + mouse_up * (1.0f - anim_weight));
    const glm::vec3 pos_anim =
        head_world_pos + head_fwd_diag * kEyeForwardOffset + head_up_diag * kEyeUpOffset;
    const glm::vec3 pos_mouse =
        smoothed_head_pos + lookFwd * kEyeForwardOffset + glm::vec3(0.0f, kEyeUpOffset, 0.0f);
    pose.cam_pos = pos_anim * anim_weight + pos_mouse * (1.0f - anim_weight);
    return pose;
}

struct FpvRollLogRow
{
    bool use_anim_orientation_in;
    float anim_weight;
    glm::vec3 player_pos;
    float player_yaw;
    glm::vec3 head_world_pos;
    glm::vec3 head_fwd_diag;
    glm::vec3 head_up_diag;
    glm::vec3 smoothed_head_pos;
    glm::vec3 cam_pos;
    glm::vec3 cam_fwd;
    glm::vec3 cam_up;
    const char* one_shot_name;
};

void writeFpvRollLog(const FpvRollLogRow& r)
{
    const bool is_roll_window = r.use_anim_orientation_in || r.anim_weight > 0.0f;
    if (!selva::debug::flags().fpv_roll_log || !is_roll_window)
        return;
    static FILE* sFpvRollLog = nullptr;
    static int sFpvRollFrame = 0;
    if (sFpvRollLog == nullptr)
        sFpvRollLog = std::fopen("fpv-roll-debug.log", "w");
    if (sFpvRollLog != nullptr)
    {
        const float now_seconds = static_cast<float>(SDL_GetTicks64()) * 0.001f;
        std::fprintf(
            sFpvRollLog,
            "frame=%d t=%.4f clip=%s anim_in=%d anim_w=%.3f "
            "player=(%.3f,%.3f,%.3f) yaw=%.4f "
            "head_world=(%.3f,%.3f,%.3f) "
            "head_fwd=(%.3f,%.3f,%.3f) head_up=(%.3f,%.3f,%.3f) "
            "smoothed_head=(%.3f,%.3f,%.3f) "
            "camPos=(%.3f,%.3f,%.3f) cam_fwd=(%.3f,%.3f,%.3f) "
            "cam_up=(%.3f,%.3f,%.3f)\n",
            sFpvRollFrame, now_seconds, (r.one_shot_name != nullptr) ? r.one_shot_name : "(none)",
            r.use_anim_orientation_in ? 1 : 0, r.anim_weight, r.player_pos.x, r.player_pos.y,
            r.player_pos.z, r.player_yaw, r.head_world_pos.x, r.head_world_pos.y,
            r.head_world_pos.z, r.head_fwd_diag.x, r.head_fwd_diag.y, r.head_fwd_diag.z,
            r.head_up_diag.x, r.head_up_diag.y, r.head_up_diag.z, r.smoothed_head_pos.x,
            r.smoothed_head_pos.y, r.smoothed_head_pos.z, r.cam_pos.x, r.cam_pos.y, r.cam_pos.z,
            r.cam_fwd.x, r.cam_fwd.y, r.cam_fwd.z, r.cam_up.x, r.cam_up.y, r.cam_up.z);
        std::fflush(sFpvRollLog);
    }
    ++sFpvRollFrame;
}

// First-person view: camera aligned to head bone's world transform
// (orientation included) with mouse-look pitch layered on top. Roll
// one-shots fully override; settled state is mouse-driven with smoothed
// head bob. See buildViewProj() for the dispatch.
glm::mat4 buildFirstPersonViewProj(const glm::vec3& player_pos, const glm::vec3& head_world_pos,
                                   const glm::mat4& head_world_mat, bool use_anim_orientation_in,
                                   float player_yaw, const char* one_shot_name,
                                   const glm::vec3& lookFwd)
{
    const auto& tun = selva::tuning::current();
    const auto& settings = selva::saveData().settings;
    const float kEyeForwardOffset = tun.fpv_eye_fwd_offset;
    const float kEyeUpOffset = tun.fpv_eye_up_offset;
    const float aspect =
        windowHeight() > 0 ? static_cast<float>(windowWidth()) / static_cast<float>(windowHeight())
                           : 1.0f;
    // FPV near plane 0.2 — tighter than TPV (0.5) because the camera
    // sits inside the player capsule and can be close to arm/hand
    // geometry.
    const glm::mat4 proj =
        glm::perspective(glm::radians(settings.fov_degrees_first_person), aspect, 0.2f, 2000.0f);

    const glm::vec3 smoothed_head_pos = smoothFpvHeadPos(player_pos, head_world_pos);
    const float anim_weight = computeFpvAnimWeight(use_anim_orientation_in);
    const glm::vec3 head_fwd_diag = glm::normalize(glm::vec3(head_world_mat[2]));
    const glm::vec3 head_up_diag = glm::normalize(glm::vec3(head_world_mat[1]));

    const FpvCameraPose pose =
        composeFpvCameraPose(head_world_pos, smoothed_head_pos, lookFwd, head_fwd_diag,
                             head_up_diag, anim_weight, kEyeForwardOffset, kEyeUpOffset);
    sLastView = glm::lookAt(pose.cam_pos, pose.cam_pos + pose.cam_fwd, pose.cam_up);

    writeFpvRollLog({use_anim_orientation_in, anim_weight, player_pos, player_yaw, head_world_pos,
                     head_fwd_diag, head_up_diag, smoothed_head_pos, pose.cam_pos, pose.cam_fwd,
                     pose.cam_up, one_shot_name});

    return proj * sLastView;
}

// Smooth the OFFSET above ground (not absolute Y) so dynamic poses
// (knockdown, get-up) absorb the hip-Y motion without making the
// camera lurch when the player walks up a slope.
float smoothTpvLookAtY(float ground_y, float target_lookat_y)
{
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
    return ground_y + sSmoothedOffset;
}

// Push-out: shrink target separation until sphere doesn't overlap.
// Sphere catches walls perpendicular to the forward ray (corner-pocket).
float spherePushOutSeparation(const glm::vec3& lookAt, const glm::vec3& cam_dir,
                              float target_separation, float sphere_radius, float min_separation)
{
    if (sphere_radius <= 0.0f || target_separation <= min_separation)
        return target_separation;
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
    return try_sep;
}

// ASYMMETRIC smoothing: snap on pull-IN (camera arm crossed into a
// wall — smoothing would leave it inside for a few frames, near-
// clipping the wall and revealing outside), smooth on pull-OUT for
// the glide-back feel. The legacy commit (95e9238) failed because
// indoor-enforcement was producing step-changes the snap amplified;
// today's target_separation is a continuous raycast/sphere-overlap
// value, so the snap has no step input to amplify.
float smoothTpvSeparation(float target_separation, float pull_in_tau, float desired_separation)
{
    static float sSmoothedSeparation = desired_separation;
    static bool sSeparationInit = false;
    static Uint64 sPrevSepTicks = SDL_GetTicks64();
    const Uint64 nowSepTicks = SDL_GetTicks64();
    const float sep_dt = static_cast<float>(nowSepTicks - sPrevSepTicks) * 0.001f;
    sPrevSepTicks = nowSepTicks;
    if (!sSeparationInit)
    {
        sSmoothedSeparation = target_separation;
        sSeparationInit = true;
    }
    else if (target_separation < sSmoothedSeparation)
    {
        sSmoothedSeparation = target_separation;
    }
    else
    {
        const float tau = std::max(1e-3f, pull_in_tau);
        const float alpha = 1.0f - std::exp(-sep_dt / tau);
        sSmoothedSeparation += (target_separation - sSmoothedSeparation) * alpha;
    }
    return sSmoothedSeparation;
}

// Crosshair raycast log: walks 6 hits forward along the view ray,
// logs each body name + distance. Aim at the surface, wait 1s, read
// the log to identify mesh primitives at the flicker spot.
void writeCrosshairRaycastLog(const glm::vec3& camPos, const glm::vec3& lookFwd)
{
    static FILE* sXLog = nullptr;
    static int sXFrame = 0;
    if (sXLog == nullptr)
        sXLog = std::fopen("crosshair-debug.log", "w");
    if (sXLog == nullptr || (sXFrame++ % 60) != 0)
        return;
    std::fprintf(sXLog, "[frame %d] camPos=(%.2f,%.2f,%.2f) lookFwd=(%.3f,%.3f,%.3f)\n", sXFrame,
                 camPos.x, camPos.y, camPos.z, lookFwd.x, lookFwd.y, lookFwd.z);
    glm::vec3 origin = camPos;
    for (int i = 0; i < 6; ++i)
    {
        const auto hit_i = engine::physics::raycast(origin, lookFwd, 200.0f);
        if (!hit_i.hit)
        {
            std::fprintf(sXLog, "  hit %d: (no hit)\n", i);
            break;
        }
        const char* nm = engine::physics::bodyDebugName(hit_i.body);
        std::fprintf(sXLog,
                     "  hit %d: dist=%.3f pos=(%.2f,%.2f,%.2f) normal=(%.2f,%.2f,%.2f) body='%s'\n",
                     i, hit_i.distance, hit_i.position.x, hit_i.position.y, hit_i.position.z,
                     hit_i.normal.x, hit_i.normal.y, hit_i.normal.z, nm ? nm : "(unnamed)");
        origin = hit_i.position + lookFwd * 0.05f;
    }
    std::fflush(sXLog);
}

// Third-person view: orbital camera anchored at player chest height,
// arm length shrinks when walls block the ray (raycast + sphere
// push-out). Asymmetric smoothing on the arm length (see helpers).
glm::mat4 buildThirdPersonViewProj(const glm::vec3& player_pos, float target_lookat_y,
                                   const glm::vec3& lookFwd)
{
    const auto& tun = selva::tuning::current();
    const auto& settings = selva::saveData().settings;
    const float yaw = cameraYaw();
    const float pitch = cameraPitch();

    // Ground reference Y = the player's feet (= sampled terrain Y).
    // Camera height + lookAt anchor both rest on this; climbing a hill
    // moves them together, no relative drift.
    const float smoothed_lookat_y = smoothTpvLookAtY(player_pos.y, target_lookat_y);
    const glm::vec3 lookAt(player_pos.x, smoothed_lookat_y, player_pos.z);

    // Pitch-coupled ideal offset. Pull-in pipeline below shrinks the
    // actual arm length when geometry blocks the ray — no separate
    // "indoor follow distance" flag needed.
    const glm::vec3 ideal_offset =
        -lookFwd * tun.follow_distance + glm::vec3(0.0f, tun.follow_height, 0.0f);
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
    target_separation =
        spherePushOutSeparation(lookAt, cam_dir, target_separation, sphere_radius, min_separation);

    const float smoothed_separation =
        smoothTpvSeparation(target_separation, tun.camera_pull_in_tau, desired_separation);
    const glm::vec3 camPos = lookAt + cam_dir * smoothed_separation;

    if (selva::debug::flags().crosshair_raycast_log)
        writeCrosshairRaycastLog(camPos, lookFwd);

    if (selva::debug::flags().camera_pull_in_log)
        debugWriteCameraPullInLog({player_pos, yaw, pitch, lookFwd, lookAt, cam_dir, ideal_offset,
                                   desired_separation, tun.follow_distance, hit, target_separation,
                                   smoothed_separation, camPos, sphere_radius});

    sLastView = glm::lookAt(camPos, lookAt, glm::vec3(0.0f, 1.0f, 0.0f));

    const float aspect =
        windowHeight() > 0 ? static_cast<float>(windowWidth()) / static_cast<float>(windowHeight())
                           : 1.0f;
    const glm::mat4 proj =
        glm::perspective(glm::radians(settings.fov_degrees_third_person), aspect, 0.1f, 2000.0f);
    return proj * sLastView;
}
} // namespace

glm::mat4 buildViewProj(const glm::vec3& player_pos, float target_lookat_y,
                        const glm::vec3& head_world_pos, const glm::mat4& head_world_mat,
                        bool use_anim_orientation_in, float player_yaw, const char* one_shot_name)
{
    const float yaw = cameraYaw();
    const float pitch = cameraPitch();
    const glm::vec3 lookFwd(std::cos(pitch) * -std::sin(yaw), std::sin(pitch),
                            std::cos(pitch) * -std::cos(yaw));
    if (cameraMode() == CameraMode::FirstPerson)
        return buildFirstPersonViewProj(player_pos, head_world_pos, head_world_mat,
                                        use_anim_orientation_in, player_yaw, one_shot_name,
                                        lookFwd);
    return buildThirdPersonViewProj(player_pos, target_lookat_y, lookFwd);
}

namespace
{
// Deterministic hash from (x, z) -> [0, 1). Used to derive per-tree
// variant pick, yaw, scale, wind phase — all stable across runs.
float hashXZ(float x, float z, std::uint32_t salt)
{
    std::uint32_t h = static_cast<std::uint32_t>(static_cast<int>(x * 2000.0f)) * 374761393u +
                      static_cast<std::uint32_t>(static_cast<int>(z * 2000.0f)) * 668265263u + salt;
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
    // Structure footprints drive every terrain surface that a
    // structure can pierce: physics floor (Hole modifier), render
    // floor (shader discard), cavern ceiling/walls (quad emission),
    // disc rim (heightmap PNG via dump-world). Single source of
    // truth, registered once at region init in
    // crypt_layout::registerAuthoredWorld.

    const auto& all_lights = engine::world::allLights();
    std::vector<engine::world::LightSource> region_lights;
    region_lights.reserve(all_lights.size());
    const float light_time = selva::wallClock();

    const int fp_count = engine::world::structureFootprintCount();
    std::vector<glm::vec4> region_discards;
    region_discards.reserve(static_cast<size_t>(fp_count));

    for (int i = 0; i < selva::world::terrainRegionCount(); ++i)
    {
        const auto& r = selva::world::terrainRegion(i);
        selva::render::setTerrainBaseColor(
            glm::vec3(r.base_color[0], r.base_color[1], r.base_color[2]));
        selva::render::setTerrainTones(
            glm::vec3(r.tone_dark[0], r.tone_dark[1], r.tone_dark[2]),
            glm::vec3(r.tone_light[0], r.tone_light[1], r.tone_light[2]));
        selva::render::setTerrainLightingEnv(
            r.sun_multiplier, glm::vec3(r.sky_ambient[0], r.sky_ambient[1], r.sky_ambient[2]),
            glm::vec3(r.ground_ambient[0], r.ground_ambient[1], r.ground_ambient[2]));

        // Each region's draw gets globals (region_name=null) plus lights
        // tagged with this region's name. Avoids leaking torches across
        // sealed boundaries (Selva sun-lit, Limbo cavern, etc).
        region_lights.clear();
        for (size_t li = 0; li < all_lights.size(); ++li)
        {
            const auto& L = all_lights[li];
            if (L.region_name == nullptr || std::strcmp(L.region_name, r.name.c_str()) == 0)
            {
                engine::world::LightSource flickered = L;
                flickered.intensity =
                    engine::world::flickerIntensity(static_cast<int>(li), light_time);
                region_lights.push_back(flickered);
            }
        }
        selva::render::setTerrainPointLights(region_lights);

        // Discard rects: every cuts_floor footprint in this region
        // (or unscoped) contributes one rect. Same filter rule as
        // lights — region_name=null applies everywhere.
        region_discards.clear();
        for (int j = 0; j < fp_count; ++j)
        {
            const auto& f = engine::world::structureFootprintAt(j);
            if (!f.cuts_floor)
                continue;
            const bool region_match =
                (f.region_name == nullptr) || (std::strcmp(f.region_name, r.name.c_str()) == 0);
            if (!region_match)
                continue;
            region_discards.emplace_back(f.center_xz.x, f.center_xz.y, f.half_extents_xz.x,
                                         f.half_extents_xz.y);
        }
        selva::render::setTerrainDiscardRects(region_discards);

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

void renderStaticMeshes()
{
    // Multi-resident: iterate EVERY registered region's meshes, not
    // just the current one. Positions are baked in world space by
    // the region loader, so model matrix is identity. This is what
    // lets the player see through the chapel door into chapel
    // interior, and through the descent shaft down into Limbo, even
    // though "current region" (for trigger context) is only one of
    // them. See Region.h doctrine on currentRegion semantics.
    for (int i = 0; i < engine::world::regionCount(); ++i)
    {
        const auto* region = dynamic_cast<const selva::world::JsonRegion*>(
            engine::world::regionPtr(engine::world::regionAt(i)));
        if (region != nullptr)
            region->renderMeshes();
    }
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

    // Multi-resident: same loop as the color pass. Region-owned
    // meshes: positions baked in world space; depth model matrix
    // is identity. Use renderMeshesDepth which does NOT touch
    // color shader state (different shader program is bound by
    // the depth pass).
    selva::render::setSceneDepthModel(glm::mat4(1.0f));
    for (int i = 0; i < engine::world::regionCount(); ++i)
    {
        const auto* region = dynamic_cast<const selva::world::JsonRegion*>(
            engine::world::regionPtr(engine::world::regionAt(i)));
        if (region != nullptr)
            region->renderMeshesDepth();
    }

    // Restore cull-front for subsequent depth-pass casters (skeletal
    // actors, etc.) per the beginDepthPass contract.
    glEnable(GL_CULL_FACE);
    glCullFace(GL_FRONT);
}

void renderDoors()
{
    for (const auto& d : selva::world::doors())
    {
        if (d.visual_mesh.primitives.empty())
            continue;
        const glm::mat4 model = selva::world::doorModelMatrix(d);
        selva::render::setSceneModel(model);
        for (const auto& p : d.visual_mesh.primitives)
        {
            if (p.vao == 0)
                continue;
            if (p.usage == selva::world::StaticMeshUsage::Collision)
                continue;
            selva::render::setSceneTint(1.0f);
            selva::render::setSceneBaseColor(
                glm::vec3(p.base_color[0], p.base_color[1], p.base_color[2]));
            glBindVertexArray(p.vao);
            glDrawElements(GL_TRIANGLES, p.index_count, GL_UNSIGNED_INT, nullptr);
        }
    }
    // Restore identity so subsequent static-mesh draws aren't offset.
    selva::render::setSceneModel(glm::mat4(1.0f));
}

void renderEquippedWeapon()
{
    // Static-mesh pass: scene shader is bound by the caller. Belongs
    // here next to renderDoors() rather than in the skeletal pass
    // because the weapon mesh is unanimated geometry rendered with
    // the scene program. Depth-buffered against the player skin so
    // it composes correctly regardless of draw order.
    drawEquippedWeapon(selva::gameplay::player());
}

void renderDoorsDepth()
{
    glDisable(GL_CULL_FACE);
    for (const auto& d : selva::world::doors())
    {
        if (d.visual_mesh.primitives.empty())
            continue;
        const glm::mat4 model = selva::world::doorModelMatrix(d);
        selva::render::setSceneDepthModel(model);
        for (const auto& p : d.visual_mesh.primitives)
        {
            if (p.vao == 0)
                continue;
            if (p.usage == selva::world::StaticMeshUsage::Collision)
                continue;
            glBindVertexArray(p.vao);
            glDrawElements(GL_TRIANGLES, p.index_count, GL_UNSIGNED_INT, nullptr);
        }
    }
    selva::render::setSceneDepthModel(glm::mat4(1.0f));
    glEnable(GL_CULL_FACE);
    glCullFace(GL_FRONT);
}

void renderTrees()
{
    const int variant_count = selva::world::treeVariantCount();
    if (variant_count <= 0)
        return;

    selva::render::setTreeTime(selva::wallClock());
    // Dead-wood foliage tint per wood.md *Trees stripped of leaves*.
    // The asset is alive-canopy foliage cards; we don't try to strip
    // them. Instead we crush the leaf color toward near-black with
    // a faint cool bias so the canopy reads as "drained of life"
    // rather than "warm autumn-coloured." Trunks render at identity
    // (set per-draw below). Future per-keeper-restoration lifts
    // this tint toward identity as the wood heals.
    // Linear-space; originally sRGB-authored (0.08, 0.09, 0.10) "drained
    // of life, cool-bias near-black." Linearized so the shader+framebuffer
    // round-trip lands at the same perceptual color the author tuned for.
    constexpr glm::vec3 kDeadFoliageTint(0.0072f, 0.0085f, 0.0100f);

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

    for (const auto& c : selva::world::currentRegion().cylinders)
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

        selva::render::setTreeFoliageTint(glm::vec3(1.0f));
        drawTreeMesh(v.trunk);
        selva::render::setTreeFoliageTint(kDeadFoliageTint);
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
    const int fp_count = engine::world::structureFootprintCount();
    std::vector<glm::vec4> region_discards;
    region_discards.reserve(static_cast<size_t>(fp_count));

    for (int i = 0; i < selva::world::terrainRegionCount(); ++i)
    {
        const auto& r = selva::world::terrainRegion(i);

        // Same per-region filter as the color pass — the depth pass
        // MUST drop the same fragments, otherwise terrain inside the
        // structure casts phantom shadow into the interior.
        region_discards.clear();
        for (int j = 0; j < fp_count; ++j)
        {
            const auto& f = engine::world::structureFootprintAt(j);
            if (!f.cuts_floor)
                continue;
            const bool region_match =
                (f.region_name == nullptr) || (std::strcmp(f.region_name, r.name.c_str()) == 0);
            if (!region_match)
                continue;
            region_discards.emplace_back(f.center_xz.x, f.center_xz.y, f.half_extents_xz.x,
                                         f.half_extents_xz.y);
        }
        selva::render::setTerrainDepthDiscardRects(region_discards);

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

    for (const auto& c : selva::world::currentRegion().cylinders)
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
