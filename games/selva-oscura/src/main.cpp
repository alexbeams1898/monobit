#include "Engine.h"
#include "Tunables.h"
#include "anim/AnimationClip.h"
#include "anim/AnimationDriver.h"
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
static selva::anim::PoseSampler sSampler;

// Wall-clock animation time (seconds). Wraps inside the clip's duration.
// Will eventually be driven by the AnimationDriver state machine, but for
// the first "is this even working" pass we just play Idle on a loop.
static float sAnimTime = 0.0f;

static bool initSkeletalAssets()
{
    sSkeleton = selva::anim::loadSkeleton("assets/characters/soldier/skeleton.ozz");
    if (!sSkeleton.isLoaded())
        return false;
    sIdleClip = selva::anim::loadAnimationClip("assets/characters/soldier/Idle.ozz");
    if (!sIdleClip.isLoaded())
        return false;
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

// Dodge state machine. Idle → Rolling/Backstep (committed) → Recovering → Idle.
// State machine is owned here; the *visual + positional shape* of each
// committed state is delegated to the AnimationDriver. Direction and start
// position live on PlayerState so the driver's translation offset can be
// added to the start position each frame.
enum class DodgePhase
{
    Idle,
    Rolling,    // committed: WASD locked, position = start + driver(DodgeRoll)
    Backstep,   // committed: same, but driver(DodgeBackstep) — no hop, no tumble
    Recovering, // can move again, but Space is locked
};

struct PlayerState
{
    glm::vec3 pos = glm::vec3(0.0f, 0.5f, 0.0f);
    float yaw = 0.0f; // facing yaw in radians; 0 = facing -Z

    // Dodge state.
    DodgePhase dodge_phase = DodgePhase::Idle;
    glm::vec3 dodge_dir = glm::vec3(0.0f, 0.0f, -1.0f); // unit vector, ground plane
    glm::vec3 dodge_start_pos =
        glm::vec3(0.0f);         // pos at dodge start; driver offset added each frame
    float dodge_timer = 0.0f;    // seconds elapsed in current phase
    float dodge_duration = 0.0f; // seconds total for current phase
};

// One global player. When this scales (multiple controllable entities, NPCs
// using the same locomotion code), promote to ECS.
static PlayerState sPlayer;

// Camera orientation. Yaw rotates around world-up (Y), pitch tilts up/down.
// Yaw=0 looks down -Z; positive yaw rotates CCW looking down (right-handed).
// Pitch is clamped to avoid gimbal flip at the poles.
static float sCamYaw = 0.0f;
static float sCamPitch = -0.25f; // start slightly looking down

// One-shot input edge detection. SDL's keyboard state is "is this key down
// right now"; for actions like "dodge fires on press, not on hold" we need
// the rising edge — was up last frame, down this frame.
static bool sPrevSpace = false;
static bool sPrevF1 = false;

// Dodge input buffer. When Space is pressed while the player can't act,
// the press is held for tunables.dodge_buffer_window seconds and fires
// the moment they reach Idle. Souls input philosophy: presses are never
// dropped silently; they're queued briefly so timing is forgiving. Direction
// is sampled at fire time (current WASD), not at press time.
static float sDodgeBuffer = 0.0f;

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
// Dodge — start, advance, finish.
// ---------------------------------------------------------------------------

// Begin a dodge. moveIntent is the player's current ground-plane movement
// vector this frame (zero if standing still); used to decide roll vs
// backstep and which direction. Caller is responsible for guarding against
// already-committed dodges (dodge_phase != Idle).
static void startDodge(PlayerState& p, const glm::vec3& moveIntent)
{
    const bool moving = glm::length(moveIntent) > 0.0001f;

    const auto& tun = selva::tuning::current();
    if (moving)
    {
        // Roll: commit to the input direction. Player snaps to face that
        // direction immediately (skips the smooth turn) so the roll motion
        // and visual tumble agree.
        p.dodge_dir = glm::normalize(moveIntent);
        p.yaw = yawFromGroundDir(p.dodge_dir);
        p.dodge_phase = DodgePhase::Rolling;
        p.dodge_duration = tun.roll_duration;
    }
    else
    {
        // Backstep: commit backward relative to current facing (Elden Ring
        // neutral-stance behavior). Shorter, faster, no tumble.
        const glm::vec3 facingFwd(-std::sin(p.yaw), 0.0f, -std::cos(p.yaw));
        p.dodge_dir = -facingFwd;
        p.dodge_phase = DodgePhase::Backstep;
        p.dodge_duration = tun.backstep_duration;
    }
    p.dodge_start_pos = p.pos;
    p.dodge_timer = 0.0f;
}

// Map a committed dodge phase to its AnimState. Helper so render and
// advance both agree on which driver state to evaluate.
static selva::anim::AnimState dodgeAnimState(DodgePhase phase)
{
    switch (phase)
    {
    case DodgePhase::Rolling:
        return selva::anim::AnimState::DodgeRoll;
    case DodgePhase::Backstep:
        return selva::anim::AnimState::DodgeBackstep;
    case DodgePhase::Recovering:
        return selva::anim::AnimState::DodgeRecover;
    case DodgePhase::Idle:
        break;
    }
    return selva::anim::AnimState::None;
}

// Advance the dodge state machine by dt. Updates position when in Rolling
// or Backstep by querying the animation driver for an absolute offset from
// dodge_start_pos. Transitions to Recovering at the end of the active phase,
// and back to Idle after the recovery window. Returns true if the player
// is still committed (i.e. WASD should be ignored this frame).
static bool advanceDodge(PlayerState& p, float dt)
{
    if (p.dodge_phase == DodgePhase::Idle)
        return false;

    p.dodge_timer += dt;

    if (p.dodge_phase == DodgePhase::Rolling || p.dodge_phase == DodgePhase::Backstep)
    {
        // Phase progress in [0, 1]; ask the driver for the cumulative
        // offset from dodge_start_pos and compose. Driver owns the
        // translation curve shape (ease-out, hop arc); gameplay code
        // owns timing and state transitions.
        const float phase = p.dodge_duration > 0.0f
                                ? glm::clamp(p.dodge_timer / p.dodge_duration, 0.0f, 1.0f)
                                : 1.0f;
        selva::anim::AnimDriverInput in;
        in.state = dodgeAnimState(p.dodge_phase);
        in.phase = phase;
        in.params.dodge_dir = p.dodge_dir;
        const selva::anim::AnimDriverOutput out = selva::anim::evaluate(in);
        p.pos = p.dodge_start_pos + out.translation;

        if (p.dodge_timer >= p.dodge_duration)
        {
            p.dodge_phase = DodgePhase::Recovering;
            p.dodge_duration = selva::tuning::current().dodge_recovery;
            p.dodge_timer = 0.0f;
            return false; // movement allowed in recovery
        }
        return true;
    }

    // Recovering — movement allowed; we just count down the lockout window.
    if (p.dodge_timer >= p.dodge_duration)
    {
        p.dodge_phase = DodgePhase::Idle;
        p.dodge_timer = 0.0f;
        p.dodge_duration = 0.0f;
    }
    return false;
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

    // Advance the animation clock and sample the Idle clip into the bone
    // palette. Wall-clock-rate (not fixed-step) for smooth playback on
    // high-refresh displays. When the AnimationDriver gets a Skeletal
    // implementation, this whole block becomes "ask the driver for the
    // current clip + phase, then sample"; for now we just play Idle.
    if (sIdleClip.isLoaded())
    {
        sAnimTime += dt;
        const float dur = sIdleClip.duration();
        if (dur > 0.0f && sAnimTime >= dur)
            sAnimTime = std::fmod(sAnimTime, dur);
        sSampler.sample(sIdleClip, sAnimTime);
    }

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
    // tun.dodge_buffer_window seconds. The buffer decays each frame; if it
    // reaches zero before the player is Idle, the input is dropped. When
    // the player IS idle and the buffer is non-zero, the dodge fires using
    // the *current* moveIntent (direction at fire time, not press time —
    // the "rotate-the-stick mid-buffer" forgiveness).
    const bool spaceNow = keys[SDL_SCANCODE_SPACE] != 0;
    const bool spacePressed = spaceNow && !sPrevSpace;
    sPrevSpace = spaceNow;
    if (spacePressed)
        sDodgeBuffer = tun.dodge_buffer_window;
    else if (sDodgeBuffer > 0.0f)
        sDodgeBuffer = std::max(0.0f, sDodgeBuffer - dt);

    if (sDodgeBuffer > 0.0f && sPlayer.dodge_phase == DodgePhase::Idle)
    {
        startDodge(sPlayer, moveIntent);
        sDodgeBuffer = 0.0f; // consumed
    }

    // Advance dodge state machine. While committed (Rolling/Backstep) WASD
    // is locked out; in Recovering and Idle it's honored.
    const bool dodgeCommitted = advanceDodge(sPlayer, dt);

    if (!dodgeCommitted && glm::length(moveIntent) > 0.0001f)
    {
        moveIntent = glm::normalize(moveIntent);
        const float speed =
            tun.move_speed * (keys[SDL_SCANCODE_LSHIFT] ? tun.sprint_multiplier : 1.0f);
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
    const glm::vec3 lookAt = sPlayer.pos + glm::vec3(0.0f, 0.5f, 0.0f);
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

    // 3. Player — smaller, slightly brighter cube at the player's position,
    //    rotated to face the last movement direction. During a committed
    //    dodge the animation driver supplies pitch/yaw/roll offsets
    //    (currently the roll's tumble; backstep returns zero). Position
    //    is already updated by advanceDodge — we only consume rotations
    //    here. Tint kept just above 1.0 so the per-corner gradient survives.
    glm::mat4 playerBaseModel = glm::rotate(glm::translate(glm::mat4(1.0f), sPlayer.pos),
                                            sPlayer.yaw, glm::vec3(0.0f, 1.0f, 0.0f));
    if (sPlayer.dodge_phase == DodgePhase::Rolling || sPlayer.dodge_phase == DodgePhase::Backstep)
    {
        const float phase =
            sPlayer.dodge_duration > 0.0f
                ? glm::clamp(sPlayer.dodge_timer / sPlayer.dodge_duration, 0.0f, 1.0f)
                : 0.0f;
        selva::anim::AnimDriverInput in;
        in.state = dodgeAnimState(sPlayer.dodge_phase);
        in.phase = phase;
        in.params.dodge_dir = sPlayer.dodge_dir;
        const selva::anim::AnimDriverOutput out = selva::anim::evaluate(in);
        if (out.pitch_offset != 0.0f)
            playerBaseModel =
                glm::rotate(playerBaseModel, out.pitch_offset, glm::vec3(1.0f, 0.0f, 0.0f));
        if (out.yaw_offset != 0.0f)
            playerBaseModel =
                glm::rotate(playerBaseModel, out.yaw_offset, glm::vec3(0.0f, 1.0f, 0.0f));
        if (out.roll_offset != 0.0f)
            playerBaseModel =
                glm::rotate(playerBaseModel, out.roll_offset, glm::vec3(0.0f, 0.0f, 1.0f));
    }

    {
        const glm::mat4 model = glm::scale(playerBaseModel, glm::vec3(0.6f, 1.0f, 0.6f));
        drawObject(sCubeVao, 36, model, 1.1f);
    }

    // 3b. Player face mark — a tiny dark cube poking out of the player's
    //     forward face. Lives in playerBaseModel space, so it tumbles
    //     with the body during a roll.
    {
        glm::mat4 model = glm::translate(playerBaseModel, glm::vec3(0.0f, 0.10f, -0.32f));
        model = glm::scale(model, glm::vec3(0.10f, 0.10f, 0.04f));
        drawObject(sCubeVao, 36, model, 0.25f);
    }

    glBindVertexArray(0);
    glUseProgram(0);

    // 4. Soldier (skinned) — drawn 3 units to the right of the player so
    //    we can compare the cube vs the rigged character side-by-side
    //    while the skeletal pipeline beds in. Once verified, the cube
    //    goes away and the soldier draws at sPlayer.pos directly.
    if (sSoldierMesh.isLoaded() && !sSampler.bone_palette.empty())
    {
        // uModel is pure placement. Asset-level scale and orientation
        // fixes are baked into the mesh's vertex positions (load time)
        // and the bone palette (via PoseSampler.root_transform).
        //
        // The Y offset is `-foot_offset_y` — measured at load time as
        // the lowest vertex Y in the baked rest pose. Subtracting it
        // plants the character's feet on the floor regardless of where
        // in the bind pose the rig's origin sits (Mixamo near toe-level,
        // other rigs at hips/waist/etc.).
        const glm::vec3 soldier_pos(sPlayer.pos.x + 3.0f, -sSoldierMesh.foot_offset_y,
                                    sPlayer.pos.z);
        const glm::mat4 soldier_model = glm::translate(glm::mat4(1.0f), soldier_pos);
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

    if (ImGui::CollapsingHeader("Dodge — timing", ImGuiTreeNodeFlags_DefaultOpen))
    {
        tunedSlider("Roll duration (s)", &tun.roll_duration, 0.10f, 1.50f, 0.05f, "%.2f");
        tunedSlider("Backstep duration (s)", &tun.backstep_duration, 0.10f, 1.00f, 0.05f, "%.2f");
        tunedSlider("Recovery (s)", &tun.dodge_recovery, 0.0f, 1.00f, 0.05f, "%.2f");
        tunedSlider("Input buffer window (s)", &tun.dodge_buffer_window, 0.0f, 0.50f, 0.025f,
                    "%.3f");
    }

    if (ImGui::CollapsingHeader("Dodge — shape", ImGuiTreeNodeFlags_DefaultOpen))
    {
        tunedSlider("Roll distance", &tun.roll_distance, 0.5f, 8.0f, 0.5f, "%.1f");
        tunedSlider("Roll hop height", &tun.roll_hop_height, 0.0f, 2.0f, 0.05f, "%.2f");
        tunedSlider("Roll tumble revolutions", &tun.roll_tumble_revs, 0.0f, 3.0f, 0.25f, "%.2f");
        tunedSlider("Roll tumble ease", &tun.roll_tumble_ease, 1.0f, 3.0f, 0.1f, "%.1f");
        tunedSlider("Backstep distance", &tun.backstep_distance, 0.5f, 5.0f, 0.5f, "%.1f");
    }

    if (ImGui::CollapsingHeader("Dodge — sub-curve windows", ImGuiTreeNodeFlags_DefaultOpen))
    {
        ImGui::TextWrapped("Each sub-curve runs in its own [start, end] window inside the overall "
                           "roll phase. Hop: vertical arc. Tumble: rotation. Translation: forward "
                           "motion. Adjust to layer them like a Souls roll.");
        tunedSlider("Hop start", &tun.roll_hop_start, 0.0f, 1.0f, 0.05f, "%.2f");
        tunedSlider("Hop end", &tun.roll_hop_end, 0.0f, 1.0f, 0.05f, "%.2f");
        ImGui::TextDisabled("(hop peak is fixed at window midpoint — gravity arc)");
        tunedSlider("Tumble start", &tun.roll_tumble_start, 0.0f, 1.0f, 0.05f, "%.2f");
        tunedSlider("Tumble end", &tun.roll_tumble_end, 0.0f, 1.0f, 0.05f, "%.2f");
        tunedSlider("Translation start", &tun.roll_translation_start, 0.0f, 1.0f, 0.05f, "%.2f");
        tunedSlider("Translation end", &tun.roll_translation_end, 0.0f, 1.0f, 0.05f, "%.2f");
    }

    if (ImGui::CollapsingHeader("Dodge — pre-tumble lean", ImGuiTreeNodeFlags_DefaultOpen))
    {
        ImGui::TextWrapped("Forward-pitch posture that runs alongside the hop and composes with "
                           "the tumble. Lets the character tip into the roll before the full "
                           "rotation kicks in (animator's anticipation). 0° angle disables it.");
        tunedSlider("Lean angle (rad)", &tun.roll_lean_angle, 0.0f, 1.5f, 0.05f, "%.2f");
        tunedSlider("Lean start", &tun.roll_lean_start, 0.0f, 1.0f, 0.05f, "%.2f");
        tunedSlider("Lean end", &tun.roll_lean_end, 0.0f, 1.0f, 0.05f, "%.2f");
        tunedSlider("Lean peak (within window)", &tun.roll_lean_peak_phase, 0.05f, 0.95f, 0.05f,
                    "%.2f");
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
