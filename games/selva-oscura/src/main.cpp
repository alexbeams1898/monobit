#include "Engine.h"
#include "anim/AnimationDriver.h"
#include "gl/ShaderUtils.h"

// Tell SDL not to redefine `main` to its WinMain shim — the engine owns SDL
// init, this file just uses input/state APIs. Must be before <SDL.h>.
#define SDL_MAIN_HANDLED
#include <SDL.h>
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

// Tunables — group together so feel-tuning is one place. Values are
// constexpr so the compiler folds them; not currently exposed to JSON
// because the game doesn't need that yet.
namespace tuning
{
// Locomotion.
constexpr float kPlayerMoveSpeed = 4.0f; // units / second
constexpr float kPlayerSprintMultiplier = 1.8f;

// Mouse-look.
constexpr float kMouseSensitivity = 0.0025f; // radians per pixel
constexpr float kPitchMin = -1.45f;          // ~-83 degrees
constexpr float kPitchMax = 1.45f;           // ~+83 degrees

// Player rotation toward move direction. 9 rad/s: 180° ≈ 0.35s
// (deliberate), 45° ≈ 0.09s (snappy but reads as rotation).
constexpr float kPlayerTurnRate = 9.0f; // radians per second

// Third-person follow camera offsets.
constexpr float kFollowDistance = 6.0f;
constexpr float kFollowHeight = 2.5f;

// Dodge — directional roll when moving, backstep when standing.
// Durations and recovery live here; *spatial shape* (distance, hop arc,
// tumble) lives in the animation driver (procedural curves today,
// skeletal clips later — same interface). Driver doc:
// engines/engine/docs/3D-EXTENSION.md §5b.
constexpr float kRollDuration = 0.55f;     // seconds, roll phase
constexpr float kBackstepDuration = 0.35f; // seconds, backstep phase
constexpr float kDodgeRecovery = 0.20f;    // seconds, lockout after dodge
} // namespace tuning

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

    if (moving)
    {
        // Roll: commit to the input direction. Player snaps to face that
        // direction immediately (skips the smooth turn) so the roll motion
        // and visual tumble agree.
        p.dodge_dir = glm::normalize(moveIntent);
        p.yaw = yawFromGroundDir(p.dodge_dir);
        p.dodge_phase = DodgePhase::Rolling;
        p.dodge_duration = tuning::kRollDuration;
    }
    else
    {
        // Backstep: commit backward relative to current facing (Elden Ring
        // neutral-stance behavior). Shorter, faster, no tumble.
        const glm::vec3 facingFwd(-std::sin(p.yaw), 0.0f, -std::cos(p.yaw));
        p.dodge_dir = -facingFwd;
        p.dodge_phase = DodgePhase::Backstep;
        p.dodge_duration = tuning::kBackstepDuration;
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
            p.dodge_duration = tuning::kDodgeRecovery;
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

    // Mouse look. SDL_GetRelativeMouseState drains accumulated deltas.
    // Mouse-left rotates the camera left (Souls/Elden Ring/FPS convention).
    int mdx = 0;
    int mdy = 0;
    SDL_GetRelativeMouseState(&mdx, &mdy);
    sCamYaw -= static_cast<float>(mdx) * tuning::kMouseSensitivity;
    sCamPitch -= static_cast<float>(mdy) * tuning::kMouseSensitivity;
    if (sCamPitch < tuning::kPitchMin)
        sCamPitch = tuning::kPitchMin;
    if (sCamPitch > tuning::kPitchMax)
        sCamPitch = tuning::kPitchMax;

    const Uint8* keys = SDL_GetKeyboardState(nullptr);

    // Quit on Escape — handy until there's a pause menu.
    if (keys[SDL_SCANCODE_ESCAPE])
        engine.requestQuit();

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

    // One-shot Space: rising edge → start dodge, but only from Idle (cannot
    // dodge during Rolling, Backstep, or Recovering — Souls-feel rule).
    const bool spaceNow = keys[SDL_SCANCODE_SPACE] != 0;
    const bool spacePressed = spaceNow && !sPrevSpace;
    sPrevSpace = spaceNow;
    if (spacePressed && sPlayer.dodge_phase == DodgePhase::Idle)
        startDodge(sPlayer, moveIntent);

    // Advance dodge state machine. While committed (Rolling/Backstep) WASD
    // is locked out; in Recovering and Idle it's honored.
    const bool dodgeCommitted = advanceDodge(sPlayer, dt);

    if (!dodgeCommitted && glm::length(moveIntent) > 0.0001f)
    {
        moveIntent = glm::normalize(moveIntent);
        const float speed = tuning::kPlayerMoveSpeed *
                            (keys[SDL_SCANCODE_LSHIFT] ? tuning::kPlayerSprintMultiplier : 1.0f);
        sPlayer.pos += moveIntent * speed * dt;

        // Smoothly rotate the player toward the movement direction.
        const float targetYaw = yawFromGroundDir(moveIntent);
        float delta = wrapAngleSigned(targetYaw - sPlayer.yaw);
        const float maxStep = tuning::kPlayerTurnRate * dt;
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
    const glm::vec3 camPos = sPlayer.pos - lookFwd * tuning::kFollowDistance +
                             glm::vec3(0.0f, tuning::kFollowHeight, 0.0f);
    const glm::vec3 lookAt = sPlayer.pos + glm::vec3(0.0f, 0.5f, 0.0f);
    const glm::mat4 view = glm::lookAt(camPos, lookAt, glm::vec3(0.0f, 1.0f, 0.0f));

    const float aspect =
        sWindowH > 0 ? static_cast<float>(sWindowW) / static_cast<float>(sWindowH) : 1.0f;
    const glm::mat4 proj = glm::perspective(glm::radians(60.0f), aspect, 0.1f, 200.0f);
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

    engine.setPerFrameUpdate(&selvaPerFrame);
    engine.setRenderWorld(&selvaRenderWorld);
    engine.setOnResize(&onWindowResize);

    engine.run();

    shutdownGeometry();
    engine.shutdown();
    return 0;
}
