#include "Engine.h"
#include "Tunables.h"
#include "anim/AnimationClip.h"
#include "anim/PoseSampler.h"
#include "anim/SkeletalMesh.h"
#include "anim/SkeletalRenderer.h"
#include "anim/Skeleton.h"
#include "gl/ShaderUtils.h"

#include <imgui.h>

// Tell SDL not to redefine `main` to its WinMain shim — the engine owns SDL
// init, this file just uses input/state APIs. Must be before <SDL.h>.
#define SDL_MAIN_HANDLED
#include <SDL.h>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <glad/glad.h>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <string>

// ---------------------------------------------------------------------------
// Shaders
// ---------------------------------------------------------------------------

// Vertex shader: 3D position + per-vertex grayscale shade. uModel transforms
// the vertex to world space; uViewProj projects world to clip space. Splitting
// model from view-projection lets one shader draw many objects per frame —
// each draw call updates uModel, uViewProj is set once per frame.
static const char* kSceneVertexShader = R"glsl(
#version 330 core
layout(location = 0) in vec3 aPos;
layout(location = 1) in float aShade;

uniform mat4 uModel;
uniform mat4 uViewProj;

out float vShade;

void main()
{
    gl_Position = uViewProj * uModel * vec4(aPos, 1.0);
    vShade = aShade;
}
)glsl";

// Fragment shader: outputs the interpolated grayscale shade with full alpha.
// Multiplied by uTint so the same geometry can render in different shades
// (scene cube vs player cube) without duplicating vertex buffers.
static const char* kSceneFragmentShader = R"glsl(
#version 330 core
in float vShade;
out vec4 fragColor;

uniform float uTint;

void main()
{
    float g = clamp(vShade * uTint, 0.0, 1.0);
    fragColor = vec4(g, g, g, 1.0);
}
)glsl";

// GPU shader program (vertex + fragment linked). 0 = not built / failed.
static GLuint sSceneProgram = 0;

// Cached uniform locations. Resolved once at init.
static GLint sUniModelLoc = -1;
static GLint sUniViewProjLoc = -1;
static GLint sUniTintLoc = -1;

// ---------------------------------------------------------------------------
// Cube geometry (shared between the tumbling scene cube and the player cube)
// ---------------------------------------------------------------------------

static GLuint sCubeVao = 0;
static GLuint sCubeVbo = 0;
static GLuint sCubeEbo = 0;

static void initCube()
{
    // 8 corners of a unit cube centered at origin, half-extent 0.5. Each
    // vertex is position + grayscale shade. Per-corner shading makes faces
    // gradient between corners so the 3D shape reads when it tumbles —
    // poor-man's lighting until real lighting lands.
    // clang-format off
    static constexpr float kVertices[] = {
        // back face corners (z = -0.5)
        -0.5f, -0.5f, -0.5f,   0.30f, // 0
         0.5f, -0.5f, -0.5f,   0.55f, // 1
         0.5f,  0.5f, -0.5f,   0.80f, // 2
        -0.5f,  0.5f, -0.5f,   0.55f, // 3
        // front face corners (z = +0.5)
        -0.5f, -0.5f,  0.5f,   0.55f, // 4
         0.5f, -0.5f,  0.5f,   0.80f, // 5
         0.5f,  0.5f,  0.5f,   1.00f, // 6
        -0.5f,  0.5f,  0.5f,   0.80f, // 7
    };

    static constexpr unsigned int kIndices[] = {
        // back face (looking down -Z)
        0, 2, 1,   0, 3, 2,
        // front face (looking down +Z)
        4, 5, 6,   4, 6, 7,
        // left face
        0, 4, 7,   0, 7, 3,
        // right face
        1, 2, 6,   1, 6, 5,
        // bottom face
        0, 1, 5,   0, 5, 4,
        // top face
        3, 7, 6,   3, 6, 2,
    };
    // clang-format on

    glGenVertexArrays(1, &sCubeVao);
    glGenBuffers(1, &sCubeVbo);
    glGenBuffers(1, &sCubeEbo);

    glBindVertexArray(sCubeVao);

    glBindBuffer(GL_ARRAY_BUFFER, sCubeVbo);
    glBufferData(GL_ARRAY_BUFFER, sizeof(kVertices), kVertices, GL_STATIC_DRAW);

    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, sCubeEbo);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, sizeof(kIndices), kIndices, GL_STATIC_DRAW);

    constexpr int stride = 4 * sizeof(float);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, stride, nullptr);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(1, 1, GL_FLOAT, GL_FALSE, stride,
                          reinterpret_cast<void*>(3 * sizeof(float)));
    glEnableVertexAttribArray(1);

    glBindVertexArray(0);
}

// ---------------------------------------------------------------------------
// Floor geometry — large flat quad in the y=0 plane, centered at origin.
// Per-vertex shade gives a faint vignette (darker near edges) so motion
// across the floor is readable.
// ---------------------------------------------------------------------------

static GLuint sFloorVao = 0;
static GLuint sFloorVbo = 0;
static GLuint sFloorEbo = 0;

static constexpr float kFloorHalfSize = 50.0f;

static void initFloor()
{
    // clang-format off
    const float kVertices[] = {
        // four corners of a square in the XZ plane (y=0)
        -kFloorHalfSize, 0.0f, -kFloorHalfSize,   0.10f, // 0: back-left, dark
         kFloorHalfSize, 0.0f, -kFloorHalfSize,   0.10f, // 1: back-right, dark
         kFloorHalfSize, 0.0f,  kFloorHalfSize,   0.25f, // 2: front-right, lighter
        -kFloorHalfSize, 0.0f,  kFloorHalfSize,   0.25f, // 3: front-left, lighter
    };

    static constexpr unsigned int kIndices[] = {
        0, 2, 1,   0, 3, 2,
    };
    // clang-format on

    glGenVertexArrays(1, &sFloorVao);
    glGenBuffers(1, &sFloorVbo);
    glGenBuffers(1, &sFloorEbo);

    glBindVertexArray(sFloorVao);

    glBindBuffer(GL_ARRAY_BUFFER, sFloorVbo);
    glBufferData(GL_ARRAY_BUFFER, sizeof(kVertices), kVertices, GL_STATIC_DRAW);

    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, sFloorEbo);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, sizeof(kIndices), kIndices, GL_STATIC_DRAW);

    constexpr int stride = 4 * sizeof(float);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, stride, nullptr);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(1, 1, GL_FLOAT, GL_FALSE, stride,
                          reinterpret_cast<void*>(3 * sizeof(float)));
    glEnableVertexAttribArray(1);

    glBindVertexArray(0);
}

// ---------------------------------------------------------------------------
// Skeletal character assets — Soldier.glb + skeleton.ozz + Idle.ozz. Loaded
// once at startup via initSkeletalAssets(); torn down via shutdownSkeletalAssets()
// before the GL context dies. The pose sampler caches per-clip interpolation
// state across frames; reusing it (vs constructing a new one each frame) is
// what makes sampling allocation-free in steady state.
// ---------------------------------------------------------------------------

static selva::anim::Skeleton sSkeleton;
static selva::anim::SkeletalMesh sSoldierMesh;
static selva::anim::AnimationClip sIdleClip;
static selva::anim::AnimationClip sWalkClip;
static selva::anim::AnimationClip sRunClip; // stand-in for Roll until we have a proper roll clip
static selva::anim::PoseSampler sSampler;

// Animation time bookkeeping now lives inside the PoseSampler, which
// manages its own per-clip clocks and a cross-fade between two
// concurrent tracks. main.cpp just calls sSampler.update(clip, dt, fade).

static bool initSkeletalAssets()
{
    sSkeleton = selva::anim::loadSkeleton("assets/characters/soldier/skeleton.ozz");
    if (!sSkeleton.isLoaded())
        return false;
    sIdleClip = selva::anim::loadAnimationClip("assets/characters/soldier/Idle.ozz");
    if (!sIdleClip.isLoaded())
        return false;
    sWalkClip = selva::anim::loadAnimationClip("assets/characters/soldier/Walk.ozz");
    sRunClip = selva::anim::loadAnimationClip("assets/characters/soldier/Run.ozz");
    // Walk and Run are non-fatal if missing (game still runs; the soldier
    // just plays Idle in all states). Idle is required.
    sSoldierMesh =
        selva::anim::loadSkeletalMesh("assets/characters/soldier/Soldier.glb", sSkeleton);
    if (!sSoldierMesh.isLoaded())
        return false;
    // The sampler is bound to the (skeleton, mesh) pair at construction so
    // its bone-palette space is guaranteed to match the mesh's baked
    // vertex space. No "remember to wire the root transform" step.
    sSampler = selva::anim::createPoseSampler(sSkeleton, sSoldierMesh);
    if (!selva::anim::initSkeletalRenderer())
        return false;

    // Pre-warm: sample the first clip at t=0 so the bone palette is
    // populated before the first frame renders. Without this, the very
    // first render uses an uninitialized palette (zeros), producing a
    // flash of broken geometry. Partial fix only; see
    // docs/BACKLOG.md "First-frame pop / init flash" for the full
    // story (camera + player prev-state lerps still pop on frame 0).
    // dt=0 advances no time; blend=0 snaps without fading. Pre-warm fills
    // the bone palette with the Idle pose at frame 0 so the first render
    // doesn't see zero matrices.
    sSampler.update(sIdleClip, 0.0f, 0.0f);
    return true;
}

static void shutdownSkeletalAssets()
{
    selva::anim::shutdownSkeletalRenderer();
    // sSoldierMesh's destructor frees its GPU buffers. The other ozz-owned
    // structs free heap memory in their destructors — no GL involvement.
}

// ---------------------------------------------------------------------------
// Resource cleanup
// ---------------------------------------------------------------------------

static void shutdownGeometry()
{
    glDeleteBuffers(1, &sFloorEbo);
    glDeleteBuffers(1, &sFloorVbo);
    glDeleteVertexArrays(1, &sFloorVao);
    glDeleteBuffers(1, &sCubeEbo);
    glDeleteBuffers(1, &sCubeVbo);
    glDeleteVertexArrays(1, &sCubeVao);
    glDeleteProgram(sSceneProgram);
    sFloorEbo = sFloorVbo = sFloorVao = 0;
    sCubeEbo = sCubeVbo = sCubeVao = 0;
    sSceneProgram = 0;
}

// ---------------------------------------------------------------------------
// Player + camera state
// ---------------------------------------------------------------------------

// Numeric "feel" parameters live in selva::tuning::Tunables (Tunables.h),
// loaded from config/tunables.json at startup, edited at runtime via the
// F1 ImGui panel. Gameplay code and ProceduralDriver both read from the
// same global — see selva::tuning::current().

struct PlayerState
{
    glm::vec3 pos = glm::vec3(0.0f, 0.5f, 0.0f);
    float yaw = 0.0f;       // facing yaw in radians; 0 = facing -Z
    bool sprinting = false; // held-Space sprint flag (Elden Ring style)
};

// One global player. When this scales (multiple controllable entities, NPCs
// using the same locomotion code), promote to ECS.
static PlayerState sPlayer;

// Pick the animation clip that should drive the soldier this frame, based
// on the player's gameplay state. Falls back to Idle if a more specific
// clip isn't loaded (graceful degradation on a fresh checkout where only
// some animations have been processed). Returns null only if even Idle
// isn't loaded — the caller should skip sampling in that case.
//
// This is the seam between gameplay and animation. Combat will extend it
// with attack / parry / hit-react states; each new state maps to a clip
// pointer here, and the AnimationDriver's procedural offsets compose on
// top. Keeping the selection in one named function makes the mapping
// inspectable and cheap to grep when debugging "why is this state
// playing the wrong clip."
static const selva::anim::AnimationClip* selectClipForPlayer(const PlayerState& p,
                                                             const glm::vec3& move_intent)
{
    const bool is_moving = glm::length(move_intent) > 0.0001f;
    if (is_moving && p.sprinting && sRunClip.isLoaded())
        return &sRunClip;
    if (is_moving && sWalkClip.isLoaded())
        return &sWalkClip;
    return &sIdleClip;
}

// Camera orientation. Yaw rotates around world-up (Y), pitch tilts up/down.
// Yaw=0 looks down -Z; positive yaw rotates CCW looking down (right-handed).
// Pitch is clamped to avoid gimbal flip at the poles.
static float sCamYaw = 0.0f;
static float sCamPitch = -0.25f; // start slightly looking down

// One-shot input edge detection. SDL's keyboard state is "is this key down
// right now"; for actions like the F1 panel toggle we need the rising
// edge — was up last frame, down this frame.
static bool sPrevF1 = false;

// In-game tuning panel toggle. Off by default; F1 flips it.
static bool sShowTuningPanel = false;

// Path to the live tunables config, relative to the working directory.
static const std::string kTunablesPath = "config/tunables.json";

// Window size (read at init for the projection's aspect ratio; updated on
// resize via the engine onResize callback).
static int sWindowW = 0;
static int sWindowH = 0;

static void onWindowResize(Engine& /*engine*/, int new_w, int new_h)
{
    sWindowW = new_w;
    sWindowH = new_h;
}

// ---------------------------------------------------------------------------
// Yaw helpers
// ---------------------------------------------------------------------------

// Wrap a yaw delta into [-pi, +pi] so rotation always takes the short path.
// Used by smooth turn-to-direction logic; if we're at 170° and target is
// -170°, the unwrapped delta is -340° (going the long way), but the wrapped
// delta is +20° (going the short way through 180°).
static float wrapAngleSigned(float delta)
{
    while (delta > glm::pi<float>())
        delta -= glm::two_pi<float>();
    while (delta < -glm::pi<float>())
        delta += glm::two_pi<float>();
    return delta;
}

// Map a unit ground-plane vector (X, _, Z) to a yaw matching our convention:
// yaw=0 faces -Z, positive yaw rotates CCW looking down. atan2(-x, -z) is
// the inverse of (sin(yaw), -, -cos(yaw))-style forward-vector formulas.
static float yawFromGroundDir(const glm::vec3& dir)
{
    return std::atan2(-dir.x, -dir.z);
}

// ---------------------------------------------------------------------------
// Per-frame update — runs at wall-clock rate.
// ---------------------------------------------------------------------------

static void selvaPerFrame(Engine& engine, EntityManager& /*em*/, double dt_d)
{
    const float dt = static_cast<float>(dt_d);

    // Title bar — game name + FPS. EMA-smoothed so the number doesn't flicker.
    const int fps = static_cast<int>(std::lround(1.0 / engine.lastFrameTime()));
    engine.setWindowTitle("Selva Oscura  |  FPS " + std::to_string(fps));

    // Mouse look. SDL_GetRelativeMouseState drains accumulated deltas.
    // Mouse-left rotates the camera left (Souls/Elden Ring/FPS convention).
    int mdx = 0;
    int mdy = 0;
    SDL_GetRelativeMouseState(&mdx, &mdy);
    const auto& tun = selva::tuning::current();
    // Mouse-look only when the tuning panel is closed — otherwise the
    // mouse is being used to drag sliders, not to aim the camera.
    if (!sShowTuningPanel)
    {
        sCamYaw -= static_cast<float>(mdx) * tun.mouse_sensitivity;
        sCamPitch -= static_cast<float>(mdy) * tun.mouse_sensitivity;
        if (sCamPitch < tun.pitch_min)
            sCamPitch = tun.pitch_min;
        if (sCamPitch > tun.pitch_max)
            sCamPitch = tun.pitch_max;
    }

    const Uint8* keys = SDL_GetKeyboardState(nullptr);

    // Quit on Escape — handy until there's a pause menu.
    if (keys[SDL_SCANCODE_ESCAPE])
        engine.requestQuit();

    // F1: toggle the tuning panel (rising-edge detect). Releases the
    // cursor while the panel is open so ImGui can receive clicks; the
    // game's mouse-look pauses for that duration. Restores capture when
    // the panel closes.
    const bool f1Now = keys[SDL_SCANCODE_F1] != 0;
    if (f1Now && !sPrevF1)
    {
        sShowTuningPanel = !sShowTuningPanel;
        SDL_SetRelativeMouseMode(sShowTuningPanel ? SDL_FALSE : SDL_TRUE);
        // Drain accumulated relative motion so the camera doesn't snap
        // when capture is re-acquired.
        SDL_GetRelativeMouseState(nullptr, nullptr);
    }
    sPrevF1 = f1Now;

    // Compute camera-relative movement intent every frame; both walking and
    // dodge-init read it. Forward axis is -Z rotated by camera yaw; right
    // axis is perpendicular in the XZ plane.
    glm::vec3 moveIntent(0.0f);
    const glm::vec3 camFwd(-std::sin(sCamYaw), 0.0f, -std::cos(sCamYaw));
    const glm::vec3 camRight(std::cos(sCamYaw), 0.0f, -std::sin(sCamYaw));
    if (keys[SDL_SCANCODE_W])
        moveIntent += camFwd;
    if (keys[SDL_SCANCODE_S])
        moveIntent -= camFwd;
    if (keys[SDL_SCANCODE_D])
        moveIntent += camRight;
    if (keys[SDL_SCANCODE_A])
        moveIntent -= camRight;

    // Space input — Souls-style buffering. Rising edge sets the buffer to
    // Held-Space sprint (Elden Ring convention). While Space is held, the
    // soldier moves at sprint speed and plays the Run clip. Release →
    // back to Walk speed and the Walk clip. No state machine, no input
    // buffering — sprint is a continuous modifier, not a discrete action.
    sPlayer.sprinting = keys[SDL_SCANCODE_SPACE] != 0;

    if (glm::length(moveIntent) > 0.0001f)
    {
        moveIntent = glm::normalize(moveIntent);
        const float speed = tun.move_speed * (sPlayer.sprinting ? tun.sprint_multiplier : 1.0f);
        sPlayer.pos += moveIntent * speed * dt;

        // Smoothly rotate the player toward the movement direction.
        const float targetYaw = yawFromGroundDir(moveIntent);
        float delta = wrapAngleSigned(targetYaw - sPlayer.yaw);
        const float maxStep = tun.turn_rate * dt;
        if (delta > maxStep)
            delta = maxStep;
        else if (delta < -maxStep)
            delta = -maxStep;
        sPlayer.yaw += delta;
    }

    // Pick + advance the sampler. selectClipForPlayer chooses the clip
    // for this frame based on locomotion + sprint state; the sampler
    // cross-fades on its own when the choice changes. Done last so it
    // sees post-input, post-movement state.
    const selva::anim::AnimationClip* clip = selectClipForPlayer(sPlayer, moveIntent);
    if (clip != nullptr && clip->isLoaded())
        sSampler.update(*clip, dt, tun.anim_blend_seconds);
}

// ---------------------------------------------------------------------------
// Render — issue one draw call per object. View-projection is set once per
// frame; uModel and uTint vary per draw.
// ---------------------------------------------------------------------------

static void drawObject(GLuint vao, GLsizei index_count, const glm::mat4& model, float tint)
{
    glUniformMatrix4fv(sUniModelLoc, 1, GL_FALSE, glm::value_ptr(model));
    glUniform1f(sUniTintLoc, tint);
    glBindVertexArray(vao);
    glDrawElements(GL_TRIANGLES, index_count, GL_UNSIGNED_INT, nullptr);
}

static void selvaRenderWorld(Engine& /*engine*/, EntityManager& /*em*/, float /*camX*/,
                             float /*camY*/, float /*alpha*/)
{
    // Camera is positioned behind the player along its forward axis, lifted
    // by kFollowHeight, looking at the player's chest.
    const glm::vec3 lookFwd(std::cos(sCamPitch) * -std::sin(sCamYaw), std::sin(sCamPitch),
                            std::cos(sCamPitch) * -std::cos(sCamYaw));
    const auto& tun = selva::tuning::current();
    const glm::vec3 camPos =
        sPlayer.pos - lookFwd * tun.follow_distance + glm::vec3(0.0f, tun.follow_height, 0.0f);
    // LookAt height tracks the soldier's chest (~1.3m) since the
    // rigged character is ~1.7m tall. Souls/Elden Ring aim the camera
    // at chest height for the same reason — the player's silhouette
    // sits centered in the frame instead of head-up or feet-down.
    const glm::vec3 lookAt = glm::vec3(sPlayer.pos.x, 1.3f, sPlayer.pos.z);
    const glm::mat4 view = glm::lookAt(camPos, lookAt, glm::vec3(0.0f, 1.0f, 0.0f));

    const float aspect =
        sWindowH > 0 ? static_cast<float>(sWindowW) / static_cast<float>(sWindowH) : 1.0f;
    const glm::mat4 proj = glm::perspective(glm::radians(tun.fov_degrees), aspect, 0.1f, 200.0f);
    const glm::mat4 viewProj = proj * view;

    glUseProgram(sSceneProgram);
    glUniformMatrix4fv(sUniViewProjLoc, 1, GL_FALSE, glm::value_ptr(viewProj));

    // 1. Floor — unrotated, identity model. Rendered first; depth test
    //    handles ordering against everything else.
    drawObject(sFloorVao, 6, glm::mat4(1.0f), 1.0f);

    // 2. Scene cube — tumbles at the origin, half-buried in the floor would
    //    look bad, so lift it. Acts as a fixed landmark.
    {
        const float seconds = static_cast<float>(SDL_GetTicks64()) * 0.001f; // wall-clock seconds
        const float angle = seconds * (glm::two_pi<float>() / 4.0f);
        glm::mat4 model = glm::translate(glm::mat4(1.0f), glm::vec3(0.0f, 1.5f, -8.0f));
        model = glm::rotate(model, angle, glm::normalize(glm::vec3(0.6f, 1.0f, 0.3f)));
        drawObject(sCubeVao, 36, model, 0.7f);
    }

    glBindVertexArray(0);
    glUseProgram(0);

    // 3. Player — drawn as the rigged Soldier mesh, animated by the
    //    PoseSampler. Position comes from sPlayer.pos (X/Z); Y is
    //    -foot_offset_y so feet land on the floor regardless of where
    //    the rig's origin sits in bind pose. Yaw rotates the model
    //    around world-up to face the player's heading.
    if (sSoldierMesh.isLoaded() && !sSampler.bone_palette.empty())
    {
        const glm::vec3 soldier_pos(sPlayer.pos.x, -sSoldierMesh.foot_offset_y, sPlayer.pos.z);
        glm::mat4 soldier_model = glm::translate(glm::mat4(1.0f), soldier_pos);
        soldier_model = glm::rotate(soldier_model, sPlayer.yaw, glm::vec3(0.0f, 1.0f, 0.0f));
        selva::anim::drawSkeletalMesh(sSoldierMesh, soldier_model, viewProj, sSampler.bone_palette,
                                      1.0f);
    }
}

// ---------------------------------------------------------------------------
// Snapped slider — ImGui::SliderFloat with post-hoc rounding to a step. The
// step makes drag-tuning land on round values (0.5, 1.0, 5.0) instead of
// 0.4783 — easier to settle on a value, easier to reason about. Ctrl+click
// still allows fine-grained typed entry; this only affects drag.
//
// fmt should match step's precision (e.g. "%.2f" for step=0.05).
static void tunedSlider(const char* label, float* val, float min, float max, float step,
                        const char* fmt = "%.2f")
{
    if (ImGui::SliderFloat(label, val, min, max, fmt))
    {
        if (step > 0.0f)
            *val = std::round(*val / step) * step;
    }
}

// ---------------------------------------------------------------------------
// In-game tuning panel — draws an ImGui window with sliders for every
// tunable. Live values; edits take effect on the next frame. Adding a new
// tunable: a one-line tunedSlider here and a field on Tunables.
// ---------------------------------------------------------------------------

static void selvaRenderImGui(Engine& /*engine*/, EntityManager& /*em*/)
{
    if (!sShowTuningPanel)
        return;

    auto& tun = selva::tuning::current();
    ImGui::Begin("Selva Oscura Tuning (F1)", &sShowTuningPanel);

    if (ImGui::CollapsingHeader("Locomotion", ImGuiTreeNodeFlags_DefaultOpen))
    {
        tunedSlider("Move speed", &tun.move_speed, 1.0f, 10.0f, 0.1f, "%.2f");
        tunedSlider("Sprint multiplier", &tun.sprint_multiplier, 1.0f, 3.0f, 0.1f, "%.2f");
        tunedSlider("Turn rate (rad/s)", &tun.turn_rate, 1.0f, 30.0f, 0.5f, "%.1f");
    }

    if (ImGui::CollapsingHeader("Mouse-look", ImGuiTreeNodeFlags_DefaultOpen))
    {
        tunedSlider("Sensitivity", &tun.mouse_sensitivity, 0.0005f, 0.01f, 0.0005f, "%.4f");
        tunedSlider("Pitch min", &tun.pitch_min, -1.55f, 0.0f, 0.05f, "%.2f");
        tunedSlider("Pitch max", &tun.pitch_max, 0.0f, 1.55f, 0.05f, "%.2f");
    }

    if (ImGui::CollapsingHeader("Camera", ImGuiTreeNodeFlags_DefaultOpen))
    {
        tunedSlider("Follow distance", &tun.follow_distance, 1.0f, 15.0f, 0.5f, "%.1f");
        tunedSlider("Follow height", &tun.follow_height, 0.0f, 8.0f, 0.5f, "%.1f");
        tunedSlider("FOV (deg)", &tun.fov_degrees, 30.0f, 110.0f, 5.0f, "%.0f");
    }

    if (ImGui::CollapsingHeader("Animation", ImGuiTreeNodeFlags_DefaultOpen))
    {
        tunedSlider("Cross-fade (s)", &tun.anim_blend_seconds, 0.0f, 0.50f, 0.025f, "%.3f");
    }

    ImGui::Separator();
    if (ImGui::Button("Save to config/tunables.json"))
    {
        if (!selva::tuning::saveToFile(kTunablesPath))
            std::fprintf(stderr, "[Tuning] Failed to save %s\n", kTunablesPath.c_str());
    }
    ImGui::SameLine();
    if (ImGui::Button("Reload from disk"))
        selva::tuning::loadFromFile(kTunablesPath);

    ImGui::End();
}

int main(int /*argc*/, char* /*argv*/[])
{
    Engine engine;

    // 4x MSAA — smooths cube/floor edge silhouettes so they don't crawl
    // when the camera rotates. Tried 8x; visually indistinguishable from
    // 4x at this geometry count, so the extra samples weren't earning
    // their cost. Residual sub-pixel shimmer that MSAA can't fix will be
    // absorbed by the dither/threshold post-process pass when the 1-bit
    // visual identity lands.
    engine.setMSAA(4);

    // Maximized window — full monitor area but keeps title bar / resize
    // handles so the dev can grab and adjust during iteration. The
    // 1280x720 args become the restore size when un-maximized.
    engine.setWindowMode(Engine::WindowMode::Maximized);

    if (!engine.init("Selva Oscura", 1280, 720))
    {
        std::fprintf(stderr, "Engine init failed\n");
        return 1;
    }

    engine.setClearColor(0.0f, 0.0f, 0.0f);

    // Capture the cursor for mouse-look. SDL_SetRelativeMouseMode hides the
    // cursor and feeds back relative deltas via SDL_GetRelativeMouseState.
    SDL_SetRelativeMouseMode(SDL_TRUE);
    // Drain any startup delta so the first frame's look isn't huge.
    SDL_GetRelativeMouseState(nullptr, nullptr);

    sSceneProgram = engine::gl::compileProgram(kSceneVertexShader, kSceneFragmentShader);
    if (sSceneProgram == 0)
    {
        std::fprintf(stderr, "Scene shader compile/link failed\n");
        return 1;
    }
    sUniModelLoc = glGetUniformLocation(sSceneProgram, "uModel");
    sUniViewProjLoc = glGetUniformLocation(sSceneProgram, "uViewProj");
    sUniTintLoc = glGetUniformLocation(sSceneProgram, "uTint");

    sWindowW = engine.windowWidth();
    sWindowH = engine.windowHeight();
    initCube();
    initFloor();
    if (!initSkeletalAssets())
    {
        // Non-fatal: the game stays runnable on a fresh checkout where
        // assets haven't been processed. Cube + floor still render; the
        // soldier just won't appear. Log so we notice if the assets path
        // breaks silently in CI.
        std::fprintf(stderr, "[main] skeletal assets failed to load — soldier disabled\n");
    }

    // Load runtime-tunable values from JSON. Falls back silently to
    // struct defaults if the file is missing or malformed; the in-game
    // ImGui panel can save updated values back to the same path.
    selva::tuning::loadFromFile(kTunablesPath);

    engine.setPerFrameUpdate(&selvaPerFrame);
    engine.setRenderWorld(&selvaRenderWorld);
    engine.setRenderImGui(&selvaRenderImGui);
    engine.setOnResize(&onWindowResize);

    engine.run();

    shutdownSkeletalAssets();
    shutdownGeometry();
    engine.shutdown();
    return 0;
}
