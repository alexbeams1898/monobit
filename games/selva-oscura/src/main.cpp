#include "Engine.h"
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

// Player position in world space. Selva Oscura is 3D so y is up. The player
// stands on the floor at y=0 with their cube centered slightly above it.
static glm::vec3 sPlayerPos = glm::vec3(0.0f, 0.5f, 0.0f);

// Player facing yaw, in radians, around world-up. The character settles
// toward the movement direction at a constant angular rate — too fast and
// short cardinal→diagonal turns snap with no perceived rotation; too slow
// and 180° turns feel like a tank. 9 rad/s splits the difference: 180°
// takes ~0.35s (deliberate, weighted), 45° takes ~0.09s (still snappy
// but reads as a rotation). Tune by feel as combat lands.
static float sPlayerYaw = 0.0f;
static constexpr float kPlayerTurnRate = 9.0f; // radians per second

// Camera orientation. Yaw rotates around world-up (Y), pitch tilts up/down.
// Yaw=0 looks down -Z; positive yaw rotates clockwise looking down at the
// scene. Pitch is clamped to avoid gimbal flip at the poles.
static float sCamYaw = 0.0f;
static float sCamPitch = -0.25f; // start slightly looking down

// Third-person follow: camera sits this far behind+above the player along
// the camera's forward axis. Tweak by feel.
static constexpr float kFollowDistance = 6.0f;
static constexpr float kFollowHeight = 2.5f;

// Movement / look feel constants.
static constexpr float kPlayerMoveSpeed = 4.0f; // units / second
static constexpr float kPlayerSprintMultiplier = 1.8f;
static constexpr float kMouseSensitivity = 0.0025f; // radians per pixel
static constexpr float kPitchMin = -1.45f;          // ~-83 degrees
static constexpr float kPitchMax = 1.45f;           // ~+83 degrees

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
// Per-frame update — runs at wall-clock rate. Reads SDL keyboard + relative
// mouse state directly; the engine doesn't need to abstract these for a
// single-game render-callback pattern.
// ---------------------------------------------------------------------------

static void selvaPerFrame(Engine& engine, EntityManager& /*em*/, double dt_d)
{
    const float dt = static_cast<float>(dt_d);

    // Title bar — game name + FPS. Read the engine's EMA-smoothed frame
    // time so the number doesn't flicker.
    const int fps = static_cast<int>(std::lround(1.0 / engine.lastFrameTime()));
    engine.setWindowTitle("Selva Oscura  |  FPS " + std::to_string(fps));

    // Mouse look. SDL_GetRelativeMouseState returns deltas accumulated since
    // the last call; we drain it once per frame here.
    int mdx = 0;
    int mdy = 0;
    SDL_GetRelativeMouseState(&mdx, &mdy);
    // Mouse-left should rotate the camera left (world appears to rotate
    // right) — the Souls/Elden Ring/FPS convention. Our yaw is "rotate
    // counterclockwise looking down" so a left mouse delta (negative mdx)
    // needs to *decrease* yaw, hence the subtraction.
    sCamYaw -= static_cast<float>(mdx) * kMouseSensitivity;
    sCamPitch -= static_cast<float>(mdy) * kMouseSensitivity;
    if (sCamPitch < kPitchMin)
        sCamPitch = kPitchMin;
    if (sCamPitch > kPitchMax)
        sCamPitch = kPitchMax;

    // WASD locomotion in the camera's horizontal plane (yaw only — pitch
    // doesn't affect ground-plane movement, which is what a Souls-style
    // controller wants). Forward axis is -Z rotated by yaw; right axis is
    // perpendicular in the XZ plane.
    const Uint8* keys = SDL_GetKeyboardState(nullptr);

    // Quit on Escape — handy until there's a pause menu.
    if (keys[SDL_SCANCODE_ESCAPE])
        engine.requestQuit();

    glm::vec3 moveIntent(0.0f);
    const glm::vec3 fwd(-std::sin(sCamYaw), 0.0f, -std::cos(sCamYaw));
    const glm::vec3 right(std::cos(sCamYaw), 0.0f, -std::sin(sCamYaw));
    if (keys[SDL_SCANCODE_W])
        moveIntent += fwd;
    if (keys[SDL_SCANCODE_S])
        moveIntent -= fwd;
    if (keys[SDL_SCANCODE_D])
        moveIntent += right;
    if (keys[SDL_SCANCODE_A])
        moveIntent -= right;

    if (glm::length(moveIntent) > 0.0001f)
    {
        moveIntent = glm::normalize(moveIntent);
        const float speed =
            kPlayerMoveSpeed * (keys[SDL_SCANCODE_LSHIFT] ? kPlayerSprintMultiplier : 1.0f);
        sPlayerPos += moveIntent * speed * dt;

        // Rotate the player toward the movement direction. Match yaw=0 to
        // "facing -Z" (the camera's default forward), with positive yaw
        // rotating CCW around world-up Y (OpenGL right-handed convention,
        // matching glm::rotate(angle, vec3(0,1,0))). atan2(-x, -z) maps
        // moveIntent=(0,0,-1) to 0, and (1,0,0) (right) to -pi/2.
        const float targetYaw = std::atan2(-moveIntent.x, -moveIntent.z);
        float delta = targetYaw - sPlayerYaw;
        while (delta > glm::pi<float>())
            delta -= glm::two_pi<float>();
        while (delta < -glm::pi<float>())
            delta += glm::two_pi<float>();

        const float maxStep = kPlayerTurnRate * dt;
        if (delta > maxStep)
            delta = maxStep;
        else if (delta < -maxStep)
            delta = -maxStep;
        sPlayerYaw += delta;
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
    const glm::vec3 camPos =
        sPlayerPos - lookFwd * kFollowDistance + glm::vec3(0.0f, kFollowHeight, 0.0f);
    const glm::vec3 lookAt = sPlayerPos + glm::vec3(0.0f, 0.5f, 0.0f);
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
    //    rotated to face the last movement direction. Tint kept just above
    //    1.0 so the per-corner gradient survives; cranking it higher
    //    saturates against the clamp and the cube reads as a flat white
    //    card. Real lighting will replace this hack.
    const glm::mat4 playerBaseModel = glm::rotate(glm::translate(glm::mat4(1.0f), sPlayerPos),
                                                  sPlayerYaw, glm::vec3(0.0f, 1.0f, 0.0f));
    {
        const glm::mat4 model = glm::scale(playerBaseModel, glm::vec3(0.6f, 1.0f, 0.6f));
        drawObject(sCubeVao, 36, model, 1.1f);
    }

    // 3b. Player face mark — a tiny dark cube poking out of the player's
    //     forward face (the -Z side in player-local space, in front of
    //     player-yaw rotation). Without this it's impossible to tell
    //     which way the player is "facing" when they stop moving.
    //     Positioned at local z = -0.32, just outside the player's scaled
    //     forward surface (at -0.30), so it doesn't z-fight.
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

    // Borderless fullscreen at the desktop's native resolution. The
    // 1280x720 args below are ignored when fullscreen is on.
    engine.setFullscreen(true);

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
