#include "render/WorldRenderer.h"

#include "Tunables.h"
#include "render/Camera.h"
#include "render/SceneGeometry.h"
#include "world/Collision.h"

#include <glm/gtc/matrix_transform.hpp>

#include <SDL.h>

#include <cmath>

namespace selva::render
{

glm::mat4 buildViewProj(const glm::vec3& player_pos, float target_lookat_y)
{
    const auto& tun = selva::tuning::current();
    const float yaw = cameraYaw();
    const float pitch = cameraPitch();
    const glm::vec3 lookFwd(std::cos(pitch) * -std::sin(yaw), std::sin(pitch),
                            std::cos(pitch) * -std::cos(yaw));
    const glm::vec3 camPos =
        player_pos - lookFwd * tun.follow_distance + glm::vec3(0.0f, tun.follow_height, 0.0f);

    // Smoothed lookAt-Y: exponential decay toward the supplied target,
    // tau ~0.4s. First call snaps without easing.
    static float sSmoothedLookAtY = target_lookat_y;
    static bool sLookAtYInit = false;
    if (!sLookAtYInit)
    {
        sSmoothedLookAtY = target_lookat_y;
        sLookAtYInit = true;
    }
    else
    {
        constexpr float kLookAtTau = 0.4f;
        static Uint64 sPrevTicks = SDL_GetTicks64();
        const Uint64 nowTicks = SDL_GetTicks64();
        const float frame_dt = static_cast<float>(nowTicks - sPrevTicks) * 0.001f;
        sPrevTicks = nowTicks;
        const float alpha = 1.0f - std::exp(-frame_dt / kLookAtTau);
        sSmoothedLookAtY += (target_lookat_y - sSmoothedLookAtY) * alpha;
    }

    const glm::vec3 lookAt(player_pos.x, sSmoothedLookAtY, player_pos.z);
    const glm::mat4 view = glm::lookAt(camPos, lookAt, glm::vec3(0.0f, 1.0f, 0.0f));

    const float aspect =
        windowHeight() > 0 ? static_cast<float>(windowWidth()) / static_cast<float>(windowHeight())
                           : 1.0f;
    const glm::mat4 proj = glm::perspective(glm::radians(tun.fov_degrees), aspect, 0.1f, 200.0f);
    return proj * view;
}

void renderEnvironment()
{
    drawFloor(glm::mat4(1.0f), 1.0f);
    drawGrid(glm::mat4(1.0f), 1.0f);
    drawAxes(glm::mat4(1.0f), 1.0f);

    // Placeholder trees: one scaled cube per collision cylinder so the
    // colliders are visible while collision is being tuned. A flat dark
    // disc sits at the base as a fake contact shadow so the eye anchors
    // the trunk to the floor. Will swap for proper tree models + real
    // shadows once the hub asset pass starts.
    for (const auto& c : selva::world::currentScene().cylinders)
    {
        glm::mat4 disc = glm::translate(glm::mat4(1.0f), glm::vec3(c.center.x, 0.01f, c.center.z));
        disc = glm::scale(disc, glm::vec3(c.radius * 1.6f, 1.0f, c.radius * 1.6f));
        drawDisc(disc, 0.05f);

        // Unit cube has half-extent 0.5, so scale-Y by 2*half_height to
        // make the trunk span [0, 2*half_height]. Center is half_height
        // above the floor.
        const glm::vec3 trunk_center(c.center.x, c.half_height, c.center.z);
        glm::mat4 model = glm::translate(glm::mat4(1.0f), trunk_center);
        model =
            glm::scale(model, glm::vec3(c.radius * 2.0f, c.half_height * 2.0f, c.radius * 2.0f));
        drawCube(model, 0.55f);
    }
}

} // namespace selva::render
